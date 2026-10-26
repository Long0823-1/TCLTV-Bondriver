// Second independent tuner. All vendor structure layouts are specific to the
// firmware hashes checked by the APK installer. No key bytes are inspected.
#include <stddef.h>
#include <sys/stat.h>
#include "tsd_psi.h"
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
typedef uintptr_t (*d2_api_fn)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static int d2_cw_active;

#include <link.h>
#include <elf.h>
#include <sys/mman.h>
static uintptr_t *d2_cw_slot,d2_cw_original;
static unsigned d2_cw_counts[2],d2_cw_failures[2];
static int d2_cw_protection;
static uintptr_t d2_cw_hook(uintptr_t h,uintptr_t odd,uintptr_t even) {
 unsigned bank=h>=512;int count=bank==0||__atomic_load_n(&d2_cw_active,__ATOMIC_SEQ_CST);if(count)__sync_fetch_and_add(&d2_cw_counts[bank],1);
 uintptr_t result=((d2_api_fn)d2_cw_original)(h,odd,even,0,0,0);
 if(result&&count)__sync_fetch_and_add(&d2_cw_failures[bank],1);
 return result;
}
static int d2_cw_mapping_protection(uintptr_t address) {
 FILE *f=fopen("/proc/self/maps","r");if(!f)return -1;char line[512],perms[8];unsigned lo,hi;int result=-1;
 while(fgets(line,sizeof line,f))if(sscanf(line,"%x-%x %7s",&lo,&hi,perms)==3&&address>=lo&&address<hi){result=(perms[0]=='r'?PROT_READ:0)|(perms[1]=='w'?PROT_WRITE:0)|(perms[2]=='x'?PROT_EXEC:0);break;}
 fclose(f);return result;
}
static uintptr_t d2_cw_pointer(uintptr_t base,uintptr_t p){return p>=base?p:base+p;}
static int d2_cw_find(struct dl_phdr_info *info,size_t length,void *unused) {
 (void)length;(void)unused;if(!strstr(info->dlpi_name,"libisdb.so"))return 0;
 uintptr_t base=info->dlpi_addr;Elf32_Dyn *dyn=NULL;Elf32_Sym *symbols=NULL;const char *strings=NULL;Elf32_Rel *rels=NULL;size_t count=0;
 for(unsigned i=0;i<info->dlpi_phnum;i++)if(info->dlpi_phdr[i].p_type==PT_DYNAMIC)dyn=(Elf32_Dyn *)(base+info->dlpi_phdr[i].p_vaddr);
 if(!dyn)return 0;
 for(;dyn->d_tag!=DT_NULL;dyn++)switch(dyn->d_tag){case DT_SYMTAB:symbols=(Elf32_Sym *)d2_cw_pointer(base,dyn->d_un.d_ptr);break;case DT_STRTAB:strings=(char *)d2_cw_pointer(base,dyn->d_un.d_ptr);break;case DT_JMPREL:rels=(Elf32_Rel *)d2_cw_pointer(base,dyn->d_un.d_ptr);break;case DT_PLTRELSZ:count=dyn->d_un.d_val/sizeof(Elf32_Rel);break;}
 if(!symbols||!strings||!rels)return 0;
 for(size_t i=0;i<count;i++)if(!strcmp(strings+symbols[ELF32_R_SYM(rels[i].r_info)].st_name,"descramble_set_cw_key")){d2_cw_slot=(uintptr_t *)(base+rels[i].r_offset);return 1;}
 return 0;
}
static int d2_cw_observe_install(void) {
 if(d2_cw_slot&&*d2_cw_slot==(uintptr_t)d2_cw_hook)return 0;d2_cw_slot=NULL;dl_iterate_phdr(d2_cw_find,NULL);if(!d2_cw_slot)return -1;
 d2_cw_original=*d2_cw_slot;d2_cw_protection=d2_cw_mapping_protection((uintptr_t)d2_cw_slot);if(d2_cw_protection<0)return -2;
 size_t page=(size_t)sysconf(_SC_PAGESIZE);void *address=(void *)((uintptr_t)d2_cw_slot&~(page-1));
 if(mprotect(address,page,d2_cw_protection|PROT_WRITE))return -3;
 __atomic_store_n(d2_cw_slot,(uintptr_t)d2_cw_hook,__ATOMIC_SEQ_CST);mprotect(address,page,d2_cw_protection);return 0;
}

// Both tuner workers can initialize concurrently. Serialize the shared GOT
// observer installation so a hook never becomes its own original function.
static pthread_mutex_t d2_cw_install_lock=PTHREAD_MUTEX_INITIALIZER;
static int d2_cw_observe_start(void){
 pthread_mutex_lock(&d2_cw_install_lock);int r=d2_cw_observe_install();
 pthread_mutex_unlock(&d2_cw_install_lock);return r;
}
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
static int d2_dual_listener=-1,d2_dual_client=-1;
static unsigned long long d2_dual_sent;
static unsigned d2_dual_clients,d2_dual_send_errors;
static int d2_dual_stream_start(void) {
 d2_dual_sent=0;d2_dual_clients=d2_dual_send_errors=0;d2_dual_listener=socket(AF_INET,SOCK_STREAM,0);if(d2_dual_listener<0)return -1;
 int one=1;setsockopt(d2_dual_listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(40558),.sin_addr.s_addr=htonl(INADDR_ANY)};
 if(bind(d2_dual_listener,(void *)&address,sizeof address)||listen(d2_dual_listener,1)){close(d2_dual_listener);d2_dual_listener=-1;return -2;}
 fcntl(d2_dual_listener,F_SETFL,O_NONBLOCK);return 0;
}
static void d2_dual_stream_send(const void *data,size_t length) {
 epg_emit(1,data,length);
 if(d2_dual_client<0){d2_dual_client=accept(d2_dual_listener,NULL,NULL);if(d2_dual_client>=0){struct timeval timeout={0,100000};int buffer=1048576;setsockopt(d2_dual_client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);setsockopt(d2_dual_client,SOL_SOCKET,SO_SNDBUF,&buffer,sizeof buffer);d2_dual_clients++;epg_viewer(1,1);}}
 if(d2_dual_client<0)return;
 size_t offset=0;while(offset<length){ssize_t n=send(d2_dual_client,(const char *)data+offset,length-offset,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0){d2_dual_send_errors++;close(d2_dual_client);d2_dual_client=-1;epg_viewer(1,0);return;}offset+=(size_t)n;d2_dual_sent+=(unsigned long long)n;}
}

static void *d2_resolve_api(const char *name) { return resolve(name); }
static uintptr_t d2_invoke(const char *name,uintptr_t a,uintptr_t b,uintptr_t c) {
 d2_api_fn fn=(d2_api_fn)d2_resolve_api(name);return fn?fn(a,b,c,0,0,0):0xffffffff;
}
#define DO(name,a,b,c) d2_invoke(name,(uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c))
static uint64_t d2_now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
struct d2_allocation {char tag[32];uint32_t size,mode,secure,padding;uint64_t physical;uint8_t reserved[32];};
struct d2_recorder {const char *name;uint32_t tsio,reserved0,reserved1;uint64_t physical;uint32_t size,packet;uint32_t callback,context,reserved2,interval,reserved3,flag;};
_Static_assert(offsetof(struct d2_allocation,physical)==48,"IOMMU ABI");
_Static_assert(sizeof(struct d2_recorder)==56 && offsetof(struct d2_recorder,physical)==16,"TSR ABI");
static pthread_mutex_t d2_dual_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t d2_dual_cond=PTHREAD_COND_INITIALIZER;
static pthread_t d2_dual_worker;
static int d2_dual_has_worker,d2_dual_stop_flag;
static struct {
 uint32_t frequency;uint16_t onid,tsid,sid,pmt;
 int running,ready,error,phase,parsed,video_clear,audio_clear;
 uint64_t bytes,sent;unsigned sync_errors,cc_errors,tei,ecm_errors;
 struct tsd_service_pids service;
 struct tsd_psi pat_parser,pmt_parser,ecm_parser;
} d2_dual_state;
// Queue payloads are pointers. Reuse only after the serial vendor worker has
// reached a later PVR key-setting operation. The two PVR AV handles produce
// two calls per ECM; counting calls never reads the keys. If the worker stalls,
// the bounded pool fills and newer ECMs are rejected instead of overwriting it.
static unsigned d2_cw_per_ecm=2;
static uint8_t d2_ecm_frames[64][4096];
static uint32_t d2_ecm_frame_sequence[64],d2_ecm_issued,d2_ecm_last_crc;
static int d2_ecm_have_last;
static void d2_on_ecm(void *unused,const uint8_t *section,size_t length) {
 (void)unused;if(length<12 || length>4096 || section[0]!=0x82)return;
 uint32_t crc=(uint32_t)section[length-4]<<24|(uint32_t)section[length-3]<<16|(uint32_t)section[length-2]<<8|section[length-1];
 if(d2_ecm_have_last && crc==d2_ecm_last_crc)return;
 unsigned completed=__atomic_load_n(&d2_cw_counts[1],__ATOMIC_SEQ_CST),slot=64;
 for(unsigned i=0;i<64;i++)if(!d2_ecm_frame_sequence[i]||d2_ecm_frame_sequence[i]<completed){slot=i;break;}
 if(slot==64){pthread_mutex_lock(&d2_dual_lock);d2_dual_state.ecm_errors++;pthread_mutex_unlock(&d2_dual_lock);return;}
 uint8_t *parsed=d2_ecm_frames[slot];memset(parsed,0,4096);
 if(DO("tvhsipsi_parse_ecm",parsed,section,0) || DO("bcas_task_sendmsg_ex",0xa003,parsed,1)){pthread_mutex_lock(&d2_dual_lock);d2_dual_state.ecm_errors++;pthread_mutex_unlock(&d2_dual_lock);return;}
 d2_ecm_issued+=d2_cw_per_ecm;d2_ecm_frame_sequence[slot]=d2_ecm_issued;d2_ecm_last_crc=crc;d2_ecm_have_last=1;
}
static void d2_dual_section(void *unused,const uint8_t *section,size_t length) {
 (void)unused;
 if(d2_dual_state.phase==1 && !tsd_pat_pmt(section,length,d2_dual_state.tsid,d2_dual_state.sid,&d2_dual_state.pmt))d2_dual_state.parsed=1;
 else if(d2_dual_state.phase==2 && !tsd_pmt_pids(section,length,d2_dual_state.sid,&d2_dual_state.service) && !tsd_pid_add(&d2_dual_state.service,d2_dual_state.pmt))d2_dual_state.parsed=1;
}
static int d2_dual_stopping(void){return __atomic_load_n(&d2_dual_stop_flag,__ATOMIC_SEQ_CST);}
static void d2_dual_signal_error(int error) {
 pthread_mutex_lock(&d2_dual_lock);d2_dual_state.error=error;d2_dual_state.running=0;pthread_cond_broadcast(&d2_dual_cond);pthread_mutex_unlock(&d2_dual_lock);
}
static void d2_dual_save(void) {
 if(__atomic_load_n(&epg_scan_channel[1],__ATOMIC_SEQ_CST))return;
 FILE *f=fopen("/data/local/tmp/tsd_last_channel_1.new","w");if(!f)return;
 fprintf(f,"%u %u %u\n",d2_dual_state.onid,d2_dual_state.tsid,d2_dual_state.sid);
 int bad=ferror(f);if(fclose(f))bad=1;
 if(!bad){chmod("/data/local/tmp/tsd_last_channel_1.new",0600);rename("/data/local/tmp/tsd_last_channel_1.new","/data/local/tmp/tsd_last_channel_1");}
}
#include "tsd_mux.inc"
static pthread_mutex_t d2_init_lock=PTHREAD_MUTEX_INITIALIZER;
static int d2_tsr_initialized,d2_primary_input_ready;
static uint32_t d2_primary_owned_tsio;
static int d2_record_init(void){
 pthread_mutex_lock(&d2_init_lock);int r=0;uint32_t path[2]={0};
 if(!d2_tsr_initialized){uint32_t params[4]={0};r=DO("MI_TSR_Init",params,0,0);if(r)goto done;d2_tsr_initialized=1;}
 if(d2_primary_input_ready)goto done;
 r=DO("mif_demux_get_path_params",0,path,0);if(r)goto done;
 if(path[0]==0xab000000&&path[1]==0xae100000){d2_primary_input_ready=1;goto done;}
 if(path[0]||path[1]){r=43;goto done;}
 if(!d2_primary_owned_tsio){
  struct {const char *name;uint32_t path;} input={"TCL_RECORD_PRIMARY",0};
  r=DO("MI_TSIO_Open",&input,&d2_primary_owned_tsio,0);if(r)goto done;
  if(d2_primary_owned_tsio!=0xab000000){DO("MI_TSIO_Close",d2_primary_owned_tsio,0,0);d2_primary_owned_tsio=0;r=43;goto done;}
 }
 uint8_t mode=0;r=DO("MI_TSIO_ConnectInput",d2_primary_owned_tsio,0xae100000,&mode);if(r)goto done;
 r=DO("mif_demux_set_path_params",0,d2_primary_owned_tsio,0xae100000);if(!r)d2_primary_input_ready=1;
 done:pthread_mutex_unlock(&d2_init_lock);return r;
}
static int d2_dual_capture(void) {
 uint32_t cfg[]={1,4,d2_dual_state.frequency,2,0,0,0},tsio=0,record=0,source=0xae100001,saved[2]={0};
 int tuned=0,connected=0,allocated=0,mapped=0,cas=0,result=0;uint8_t mode=0,all=1;
 struct d2_allocation memory={0};strcpy(memory.tag,"tsr_down");memory.size=0x200000;memory.mode=1;
 uint8_t data[48128],last_cc[8192];memset(last_cc,255,sizeof last_cc);
 result=d2_record_init();if(result)goto done;
 result=DO("fpi_demod_start_connect",cfg,0,0);if(result)goto done;tuned=1;
 uint64_t deadline=d2_now_ms()+10000,last_data=d2_now_ms();uint8_t locked=0;
 do {if(d2_dual_stopping())goto done;DO("fpi_demod_get_lock_status",1,&locked,0);if(locked)break;usleep(50000);}while(d2_now_ms()<deadline);
 if(!locked){result=20;goto done;}
 struct {const char *name;uint32_t path;} input={"TCL_BONDRIVER_TUNER1",0};
 result=DO("MI_TSIO_Open",&input,&tsio,0);if(result)goto done;
 result=DO("MI_TSIO_ConnectInput",tsio,source,&mode);if(result)goto done;connected=1;
 result=DO("MI_OS_AllocateIommuMemory",&memory,0,0);if(result)goto done;allocated=1;
 struct d2_recorder params={0};params.name="TCL_BONDRIVER_TUNER1";params.tsio=tsio;params.physical=memory.physical;params.size=memory.size;params.packet=188;
 result=DO("mi_tsr_Open",&params,&record,0);if(result)goto done;
 result=DO("MI_TSR_AddPid",record,0,0);if(result)goto done;
 result=DO("MI_TSR_Start",record,&all,0);if(result)goto done;
 if(d2_cw_observe_start()){result=40;goto done;}
 __atomic_store_n(&d2_cw_active,1,__ATOMIC_SEQ_CST);
 DO("mif_demux_get_path_params",1,saved,0);result=DO("mif_demux_set_path_params",1,tsio,source);if(result)goto done;mapped=1;
 result=DO("fpi_dtv_demux_change_source_by_tspath_and_stream_port",1,1,2);if(result)goto done;
 d2_dual_state.phase=1;d2_dual_state.parsed=0;d2_dual_state.pmt=0;d2_ecm_have_last=0;
 tsd_mux_reset(&d2_mux,d2_dual_state.tsid);tsd_psi_reset(&d2_dual_state.pat_parser,0);deadline=d2_now_ms()+10000;
 pthread_mutex_lock(&d2_dual_lock);d2_dual_state.running=1;d2_dual_state.error=0;d2_dual_state.video_clear=d2_dual_state.audio_clear=0;pthread_mutex_unlock(&d2_dual_lock);
 struct {void *data;uint32_t capacity;} buffer={data,sizeof data};
 while(!d2_dual_stopping()) {
  uint32_t size=0;result=DO("MI_TSR_Read",record,&buffer,&size);
  if(result==3 || (!result&&!size)) {if(d2_now_ms()>last_data+10000){result=21;goto done;}usleep(3000);continue;}
  if(result)goto done;if(size>sizeof data || size%188){result=31;goto done;}last_data=d2_now_ms();
  unsigned sync=0,cc_errors=0,tei=0;
  for(uint32_t i=0;i<size;i+=188) {
   uint8_t *p=data+i;if(p[0]!=0x47){sync++;continue;}unsigned pid=((p[1]&31)<<8)|p[2],af=(p[3]>>4)&3,cc=p[3]&15;
   if(p[1]&128)tei++;if((af&2)&&p[4]&&p[4]<=183&&(p[5]&128))last_cc[pid]=255;
   if((af&1)&&pid!=8191){if(last_cc[pid]!=255&&cc!=((last_cc[pid]+1)&15)&&cc!=last_cc[pid])cc_errors++;last_cc[pid]=cc;}
   if(d2_dual_state.phase<=2)tsd_mux_feed(&d2_mux,p);
   if(d2_dual_state.phase==1)tsd_psi_feed(&d2_dual_state.pat_parser,p,d2_dual_section,NULL);
   else if(d2_dual_state.phase==2)tsd_psi_feed(&d2_dual_state.pmt_parser,p,d2_dual_section,NULL);
   else {
    if(cas)tsd_psi_feed(&d2_dual_state.ecm_parser,p,d2_on_ecm,NULL);
    if(!(p[1]&128)&&!(p[3]&192)&&(p[1]&64)&&(af&1)) {
     unsigned off=4+((af&2)?1+p[4]:0);
     if(off+4<=188&&!p[off]&&!p[off+1]&&p[off+2]==1){if(pid==d2_dual_state.service.video)d2_dual_state.video_clear=1;if(pid==d2_dual_state.service.audio)d2_dual_state.audio_clear=1;}
    }
   }
  }
  if(d2_dual_state.phase==1 && d2_dual_state.parsed) {
   DO("MI_TSR_Stop",record,0,0);result=DO("MI_TSR_AddPid",record,d2_dual_state.pmt,0);if(!result)result=DO("MI_TSR_Start",record,&all,0);if(result)goto done;
   d2_dual_state.phase=2;d2_dual_state.parsed=0;tsd_psi_reset(&d2_dual_state.pmt_parser,d2_dual_state.pmt);memset(last_cc,255,sizeof last_cc);deadline=d2_now_ms()+10000;
  } else if(d2_dual_state.phase==2 && d2_dual_state.parsed && tsd_mux_ready(&d2_mux)) {
   DO("MI_TSR_Stop",record,0,0);
   struct tsd_service_pids mux_pids;if(tsd_mux_union(&d2_mux,&mux_pids)){result=41;goto done;}
   for(unsigned i=0;i<mux_pids.count;i++){if(mux_pids.pids[i]==0||mux_pids.pids[i]==d2_dual_state.pmt)continue;result=DO("MI_TSR_AddPid",record,mux_pids.pids[i],0);if(result)goto done;}
   all=0;
   result=DO("MI_TSR_Start",record,&all,0);if(result)goto done;
   if(d2_dual_state.service.ecm!=0xffff) {
    result=DO("descramble_2K_PVR_reopen",0,0,0);if(result)goto done;cas=1;DO("tvhdtv_clear_pvr_desc_pids",1,0,0);
    if(d2_dual_state.service.video!=0xffff){result=DO("tvhdtv_set_pvr_desc_video_pids",1,&d2_dual_state.service.video,1);if(!result)result=DO("mif_ci_desc_connect_pvr_filter",d2_dual_state.service.video,0,0);if(result)goto done;}
    if(d2_dual_state.service.audio!=0xffff){result=DO("tvhdtv_set_pvr_desc_audio_pids",1,&d2_dual_state.service.audio,1);if(!result)result=DO("mif_ci_desc_connect_pvr_filter",d2_dual_state.service.audio,0,0);if(result)goto done;}
    uint16_t other[12];unsigned other_count=0;
    for(unsigned i=0;i<d2_mux.count;i++){struct tsd_service_pids *sp=&d2_mux.programs[i].service;if(sp->ecm!=d2_dual_state.service.ecm)continue;
     for(unsigned j=0;j<sp->count;j++){uint16_t pid=sp->pids[j];if(tsd_mux_pid_clear(&d2_mux.programs[i],pid)||pid<0x100||pid==sp->pcr||pid==sp->ecm||pid==d2_dual_state.service.video||pid==d2_dual_state.service.audio)continue;
      unsigned k=0;for(;k<other_count;k++)if(other[k]==pid)break;if(k<other_count)continue;if(other_count==12){result=42;goto done;}other[other_count++]=pid;
     }
    }
    if(other_count){result=DO("tvhdtv_set_pvr_desc_other_pids",1,other,other_count);if(result)goto done;for(unsigned i=0;i<other_count;i++){result=DO("mif_ci_desc_connect_pvr_filter",other[i],0,0);if(result)goto done;}}
    d2_cw_per_ecm=other_count+(d2_dual_state.service.video!=0xffff)+(d2_dual_state.service.audio!=0xffff);if(!d2_cw_per_ecm){result=43;goto done;}
    tsd_psi_reset(&d2_dual_state.ecm_parser,d2_dual_state.service.ecm);
   }
   d2_dual_state.video_clear=d2_dual_state.service.video==0xffff;d2_dual_state.audio_clear=d2_dual_state.service.audio==0xffff;
   d2_dual_state.phase=3;d2_dual_state.parsed=0;memset(last_cc,255,sizeof last_cc);deadline=d2_now_ms()+10000;
  }
  if(d2_dual_state.phase==3)d2_dual_stream_send(data,size);
  pthread_mutex_lock(&d2_dual_lock);d2_dual_state.bytes+=size;d2_dual_state.sent=d2_dual_sent;d2_dual_state.sync_errors+=sync;d2_dual_state.cc_errors+=cc_errors;d2_dual_state.tei+=tei;
  int newly_ready=!d2_dual_state.ready && d2_dual_state.video_clear && d2_dual_state.audio_clear;
  if(newly_ready){d2_dual_state.ready=1;pthread_cond_broadcast(&d2_dual_cond);}int ready=d2_dual_state.ready;pthread_mutex_unlock(&d2_dual_lock);
  if(newly_ready)d2_dual_save();if(!ready && d2_now_ms()>deadline){result=22;goto done;}
 }
 done:
 // Keep immutable queued ECM frames alive across retries and channel changes.
 if(d2_dual_client>=0){close(d2_dual_client);d2_dual_client=-1;epg_viewer(1,0);}
 uint64_t drain_deadline=d2_now_ms()+3000;
 while(__atomic_load_n(&d2_cw_counts[1],__ATOMIC_SEQ_CST)<d2_ecm_issued && d2_now_ms()<drain_deadline)usleep(10000);
 if(cas)DO("tvhdtv_clear_pvr_desc_pids",1,0,0);
 __atomic_store_n(&d2_cw_active,0,__ATOMIC_SEQ_CST);
 if(mapped)DO("mif_demux_set_path_params",1,saved[0],saved[1]);
 if(record){DO("MI_TSR_Stop",record,0,0);DO("MI_TSR_Close",record,0,0);}
 if(allocated){struct {uint64_t physical;uint32_t size;} release={memory.physical,memory.size};DO("MI_OS_FreeIommuMemory",&release,0,0);}
 if(connected)DO("MI_TSIO_DisconnectInput",tsio,source,0);if(tsio)DO("MI_TSIO_Close",tsio,0,0);if(tuned)DO("fpi_demod_stop_connect",1,0,0);
 return result;
}
static void *d2_dual_main(void *unused) {
 (void)unused;
 while(!d2_dual_stopping()) {
  int result=d2_dual_capture();pthread_mutex_lock(&d2_dual_lock);int was_ready=d2_dual_state.ready;d2_dual_state.running=0;d2_dual_state.ready=0;pthread_mutex_unlock(&d2_dual_lock);
  if(d2_dual_stopping())break;d2_dual_signal_error(result?result:21);if(!was_ready)break;
  for(int i=0;i<20&&!d2_dual_stopping();i++)usleep(100000);
 }
 pthread_mutex_lock(&d2_dual_lock);d2_dual_state.running=0;pthread_cond_broadcast(&d2_dual_cond);pthread_mutex_unlock(&d2_dual_lock);return NULL;
}
static void d2_dual_stop(void) {
 __atomic_store_n(&d2_dual_stop_flag,1,__ATOMIC_SEQ_CST);if(d2_dual_has_worker){pthread_join(d2_dual_worker,NULL);d2_dual_has_worker=0;}
}
static int d2_dual_tune(uint16_t onid,uint16_t tsid,uint16_t sid,int *clear) {
 uint16_t tuple[]={sid,tsid,onid};uint8_t *program=calloc(1,0x10000),mux[104]={0};if(!program)return 3;
 int result=DO("db_dtv_get_prog_info_by_triplet",program,tuple,0);int16_t muxid;memcpy(&muxid,program+0x65d6,2);free(program);if(result!=1)return 9;
 if(DO("db_dtv_get_mux_info_by_muxid",mux,muxid,0)!=1)return 9;uint32_t frequency;memcpy(&frequency,mux+12,4);if(frequency<400000||frequency>800000)return 9;
 if(d2_dual_listener<0)return 41;d2_dual_stop();__atomic_store_n(&epg_scan_channel[1],epg_background(1),__ATOMIC_SEQ_CST);
 pthread_mutex_lock(&d2_dual_lock);memset(&d2_dual_state,0,sizeof d2_dual_state);d2_dual_state.frequency=frequency;d2_dual_state.onid=onid;d2_dual_state.tsid=tsid;d2_dual_state.sid=sid;d2_dual_sent=0;d2_dual_clients=d2_dual_send_errors=0;pthread_mutex_unlock(&d2_dual_lock);
 __atomic_store_n(&d2_dual_stop_flag,0,__ATOMIC_SEQ_CST);if(pthread_create(&d2_dual_worker,NULL,d2_dual_main,NULL))return 3;d2_dual_has_worker=1;
 struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=35;
 pthread_mutex_lock(&d2_dual_lock);int wait=0;while(!d2_dual_state.ready&&!d2_dual_state.error&&!wait&&!epg_cancelled(1))wait=pthread_cond_timedwait(&d2_dual_cond,&d2_dual_lock,&deadline);
 *clear=d2_dual_state.ready;result=epg_cancelled(1)?24:(*clear?0:(d2_dual_state.error?d2_dual_state.error:23));pthread_mutex_unlock(&d2_dual_lock);if(result)d2_dual_stop();return result;
}
#undef DO
