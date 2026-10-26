// Firmware-specific normal-bank initialization, without any TV AV player.
// The installer checks the vendor library hash; verify entry points again here.
static int d0_prepare_normal(void) {
 void *anchor=resolve("descramble_2K_PVR_reopen");Dl_info info;
 if(!anchor||!dladdr(anchor,&info)||(uintptr_t)anchor-(uintptr_t)info.dli_fbase!=0x4aaa4)return 9;
 const uint32_t *code=(const uint32_t *)((uintptr_t)info.dli_fbase+0x54db8);
 if(code[0]!=0xe92d4ff0||code[1]!=0xe28db01c||code[2]!=0xe24dd014)return 9;
 ((void (*)(void))code)();return 0;
}
static int d0_dual_listener=-1,d0_dual_client=-1;
static unsigned long long d0_dual_sent;
static unsigned d0_dual_clients,d0_dual_send_errors;
static uint64_t d0_last_output;static uint64_t d0_now_ms(void);static int d0_dual_stopping(void);
static int d0_dual_stream_start(void) {
 d0_dual_sent=0;d0_dual_clients=d0_dual_send_errors=0;d0_dual_listener=socket(AF_INET,SOCK_STREAM,0);if(d0_dual_listener<0)return -1;
 int one=1;setsockopt(d0_dual_listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(40556),.sin_addr.s_addr=htonl(INADDR_ANY)};
 if(bind(d0_dual_listener,(void *)&address,sizeof address)||listen(d0_dual_listener,1)){close(d0_dual_listener);d0_dual_listener=-1;return -2;}
 fcntl(d0_dual_listener,F_SETFL,O_NONBLOCK);return 0;
}
static void d0_dual_stream_send(const void *data,size_t length) {
 epg_emit(0,data,length);
 if(d0_dual_client<0){d0_dual_client=accept(d0_dual_listener,NULL,NULL);if(d0_dual_client>=0){struct timeval timeout={0,100000};int buffer=1048576;setsockopt(d0_dual_client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);setsockopt(d0_dual_client,SOL_SOCKET,SO_SNDBUF,&buffer,sizeof buffer);d0_dual_clients++;epg_viewer(0,1);}}
 if(d0_dual_client<0)return;
 size_t offset=0;while(offset<length){ssize_t n=send(d0_dual_client,(const char *)data+offset,length-offset,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0){d0_dual_send_errors++;close(d0_dual_client);d0_dual_client=-1;epg_viewer(0,0);return;}offset+=(size_t)n;d0_dual_sent+=(unsigned long long)n;}d0_last_output=d0_now_ms();
}


// Keep the authenticated stream alive during TCL's transient primary-bank stop.
// Null TS packets carry no AV data and leave scramble flags unchanged.
static void d0_dual_keepalive(void) {
 if(d0_dual_client<0||d0_dual_stopping()||d0_now_ms()-d0_last_output<400)return;
 uint8_t packet[188];memset(packet,0xff,sizeof packet);packet[0]=0x47;packet[1]=0x1f;packet[2]=0xff;packet[3]=0x10;
 d0_dual_stream_send(packet,sizeof packet);
}

#define DO(name,a,b,c) d2_invoke(name,(uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c))
static uint64_t d0_now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
struct d0_allocation {char tag[32];uint32_t size,mode,secure,padding;uint64_t physical;uint8_t reserved[32];};
struct d0_recorder {const char *name;uint32_t tsio,reserved0,reserved1;uint64_t physical;uint32_t size,packet;uint32_t callback,context,reserved2,interval,reserved3,flag;};
_Static_assert(offsetof(struct d0_allocation,physical)==48,"IOMMU ABI");
_Static_assert(sizeof(struct d0_recorder)==56 && offsetof(struct d0_recorder,physical)==16,"TSR ABI");
static pthread_mutex_t d0_dual_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t d0_dual_cond=PTHREAD_COND_INITIALIZER;
static pthread_t d0_dual_worker;
static int d0_dual_has_worker,d0_dual_stop_flag;
static struct {
 uint32_t frequency;uint16_t onid,tsid,sid,pmt;
 int running,ready,error,phase,parsed,video_clear,audio_clear;
 uint64_t bytes,sent;unsigned sync_errors,cc_errors,tei,ecm_errors;
 struct tsd_service_pids service;
 struct tsd_psi pat_parser,pmt_parser,ecm_parser;
} d0_dual_state;
// Queue payloads are pointers. Reuse only after the serial vendor worker has
// reached a later PVR key-setting operation. The two PVR AV handles produce
// two calls per ECM; counting calls never reads the keys. If the worker stalls,
// the bounded pool fills and newer ECMs are rejected instead of overwriting it.
static unsigned d0_cw_per_ecm=2;
static uint8_t d0_ecm_frames[64][4096];
static uint32_t d0_ecm_frame_sequence[64],d0_ecm_issued,d0_ecm_last_crc;
static int d0_ecm_have_last;
static void d0_on_ecm(void *unused,const uint8_t *section,size_t length) {
 (void)unused;if(length<12 || length>4096 || section[0]!=0x82)return;
 uint32_t crc=(uint32_t)section[length-4]<<24|(uint32_t)section[length-3]<<16|(uint32_t)section[length-2]<<8|section[length-1];
 if(d0_ecm_have_last && crc==d0_ecm_last_crc)return;
 unsigned completed=__atomic_load_n(&d2_cw_counts[0],__ATOMIC_SEQ_CST),slot=64;
 for(unsigned i=0;i<64;i++)if(!d0_ecm_frame_sequence[i]||d0_ecm_frame_sequence[i]<completed){slot=i;break;}
 if(slot==64){pthread_mutex_lock(&d0_dual_lock);d0_dual_state.ecm_errors++;pthread_mutex_unlock(&d0_dual_lock);return;}
 uint8_t *parsed=d0_ecm_frames[slot];memset(parsed,0,4096);
 if(DO("tvhsipsi_parse_ecm",parsed,section,0) || DO("bcas_task_sendmsg_ex",0xa003,parsed,0)){pthread_mutex_lock(&d0_dual_lock);d0_dual_state.ecm_errors++;pthread_mutex_unlock(&d0_dual_lock);return;}
 d0_ecm_issued+=d0_cw_per_ecm;d0_ecm_frame_sequence[slot]=d0_ecm_issued;d0_ecm_last_crc=crc;d0_ecm_have_last=1;
}
static void d0_dual_section(void *unused,const uint8_t *section,size_t length) {
 (void)unused;
 if(d0_dual_state.phase==1 && !tsd_pat_pmt(section,length,d0_dual_state.tsid,d0_dual_state.sid,&d0_dual_state.pmt))d0_dual_state.parsed=1;
 else if(d0_dual_state.phase==2 && !tsd_pmt_pids(section,length,d0_dual_state.sid,&d0_dual_state.service) && !tsd_pid_add(&d0_dual_state.service,d0_dual_state.pmt))d0_dual_state.parsed=1;
}
static int d0_dual_stopping(void){return __atomic_load_n(&d0_dual_stop_flag,__ATOMIC_SEQ_CST);}
static void d0_dual_signal_error(int error) {
 pthread_mutex_lock(&d0_dual_lock);d0_dual_state.error=error;d0_dual_state.running=0;pthread_cond_broadcast(&d0_dual_cond);pthread_mutex_unlock(&d0_dual_lock);
}
static void d0_dual_save(void) {
 if(__atomic_load_n(&epg_scan_channel[0],__ATOMIC_SEQ_CST))return;
 FILE *f=fopen("/data/local/tmp/tsd_last_record_channel_0.new","w");if(!f)return;
 fprintf(f,"%u %u %u\n",d0_dual_state.onid,d0_dual_state.tsid,d0_dual_state.sid);
 int bad=ferror(f);if(fclose(f))bad=1;
 if(!bad){chmod("/data/local/tmp/tsd_last_record_channel_0.new",0600);rename("/data/local/tmp/tsd_last_record_channel_0.new","/data/local/tmp/tsd_last_record_channel_0");}
}
static struct tsd_mux d0_mux;
static int d0_dual_capture(void) {
 uint32_t cfg[]={0,4,d0_dual_state.frequency,2,0,0,0},tsio=0,record=0,source=0xae100000,saved[2]={0};
 int tuned=0,allocated=0,mapped=0,cas=0,result=0;uint8_t all=1;
 struct d0_allocation memory={0};strcpy(memory.tag,"tsr_down");memory.size=0x200000;memory.mode=1;
 uint8_t data[48128],last_cc[8192];memset(last_cc,255,sizeof last_cc);
 result=d2_record_init();if(result)goto done;
 result=DO("fpi_demod_start_connect",cfg,0,0);if(result)goto done;tuned=1;
 uint64_t deadline=d0_now_ms()+10000,last_data=d0_now_ms();uint8_t locked=0;
 do {if(d0_dual_stopping())goto done;DO("fpi_demod_get_lock_status",0,&locked,0);if(locked)break;d0_dual_keepalive();usleep(50000);}while(d0_now_ms()<deadline);
 if(!locked){result=20;goto done;}
 result=DO("mif_demux_get_path_params",0,saved,0);if(result)goto done;
 tsio=saved[0];source=saved[1];if((tsio>>24)!=0xab||(source>>24)!=0xae){result=43;goto done;}
 result=d0_prepare_normal();if(result)goto done;
 result=DO("MI_OS_AllocateIommuMemory",&memory,0,0);if(result)goto done;allocated=1;
 struct d0_recorder params={0};params.name="TCL_BONDRIVER_TUNER1";params.tsio=tsio;params.physical=memory.physical;params.size=memory.size;params.packet=188;
 result=DO("mi_tsr_Open",&params,&record,0);if(result)goto done;
 result=DO("MI_TSR_AddPid",record,0,0);if(result)goto done;
 result=DO("MI_TSR_Start",record,&all,0);if(result)goto done;
 if(d2_cw_observe_start()){result=40;goto done;}

 result=DO("mif_demux_set_path_params",0,tsio,source);if(result)goto done;mapped=1;
 result=DO("fpi_dtv_demux_change_source_by_tspath_and_stream_port",0,0,2);if(result)goto done;
 d0_dual_state.phase=1;d0_dual_state.parsed=0;d0_dual_state.pmt=0;d0_ecm_have_last=0;
 tsd_mux_reset(&d0_mux,d0_dual_state.tsid);tsd_psi_reset(&d0_dual_state.pat_parser,0);deadline=d0_now_ms()+10000;
 pthread_mutex_lock(&d0_dual_lock);d0_dual_state.running=1;d0_dual_state.error=0;d0_dual_state.video_clear=d0_dual_state.audio_clear=0;pthread_mutex_unlock(&d0_dual_lock);
 struct {void *data;uint32_t capacity;} buffer={data,sizeof data};
 while(!d0_dual_stopping()) {
  d0_dual_keepalive();uint32_t size=0;result=DO("MI_TSR_Read",record,&buffer,&size);
  if(result==3 || (!result&&!size)) {if(d0_now_ms()>last_data+(d0_dual_state.ready?1500:10000)){result=21;goto done;}usleep(3000);continue;}
  if(result)goto done;if(size>sizeof data || size%188){result=31;goto done;}last_data=d0_now_ms();
  unsigned sync=0,cc_errors=0,tei=0;
  for(uint32_t i=0;i<size;i+=188) {
   uint8_t *p=data+i;if(p[0]!=0x47){sync++;continue;}unsigned pid=((p[1]&31)<<8)|p[2],af=(p[3]>>4)&3,cc=p[3]&15;
   if(p[1]&128)tei++;if((af&2)&&p[4]&&p[4]<=183&&(p[5]&128))last_cc[pid]=255;
   if((af&1)&&pid!=8191){if(last_cc[pid]!=255&&cc!=((last_cc[pid]+1)&15)&&cc!=last_cc[pid])cc_errors++;last_cc[pid]=cc;}
   if(d0_dual_state.phase<=2)tsd_mux_feed(&d0_mux,p);
   if(d0_dual_state.phase==1)tsd_psi_feed(&d0_dual_state.pat_parser,p,d0_dual_section,NULL);
   else if(d0_dual_state.phase==2)tsd_psi_feed(&d0_dual_state.pmt_parser,p,d0_dual_section,NULL);
   else {
    if(cas)tsd_psi_feed(&d0_dual_state.ecm_parser,p,d0_on_ecm,NULL);
    if(!(p[1]&128)&&!(p[3]&192)&&(p[1]&64)&&(af&1)) {
     unsigned off=4+((af&2)?1+p[4]:0);
     if(off+4<=188&&!p[off]&&!p[off+1]&&p[off+2]==1){if(pid==d0_dual_state.service.video)d0_dual_state.video_clear=1;if(pid==d0_dual_state.service.audio)d0_dual_state.audio_clear=1;}
    }
   }
  }
  if(d0_dual_state.phase==1 && d0_dual_state.parsed) {
   DO("MI_TSR_Stop",record,0,0);result=DO("MI_TSR_AddPid",record,d0_dual_state.pmt,0);if(!result)result=DO("MI_TSR_Start",record,&all,0);if(result)goto done;
   d0_dual_state.phase=2;d0_dual_state.parsed=0;tsd_psi_reset(&d0_dual_state.pmt_parser,d0_dual_state.pmt);memset(last_cc,255,sizeof last_cc);deadline=d0_now_ms()+10000;
  } else if(d0_dual_state.phase==2 && d0_dual_state.parsed && tsd_mux_ready(&d0_mux)) {
   DO("MI_TSR_Stop",record,0,0);
   struct tsd_service_pids mux_pids;if(tsd_mux_union(&d0_mux,&mux_pids)){result=41;goto done;}
   for(unsigned i=0;i<mux_pids.count;i++){if(mux_pids.pids[i]==0||mux_pids.pids[i]==d0_dual_state.pmt)continue;result=DO("MI_TSR_AddPid",record,mux_pids.pids[i],0);if(result)goto done;}
   all=0;
   result=DO("MI_TSR_Start",record,&all,0);if(result)goto done;
   if(d0_dual_state.service.ecm!=0xffff) {
    struct tsd_service_pids all_service=d0_dual_state.service;
    result=primary_other_open(&d0_mux,&all_service);if(result)goto done;cas=1;
    d0_cw_per_ecm=2;for(unsigned i=0;i<12;i++)if(primary_other[i].registered)d0_cw_per_ecm++;
    d0_ecm_issued=__atomic_load_n(&d2_cw_counts[0],__ATOMIC_SEQ_CST);
    tsd_psi_reset(&d0_dual_state.ecm_parser,d0_dual_state.service.ecm);
   }
   d0_dual_state.video_clear=d0_dual_state.service.video==0xffff;d0_dual_state.audio_clear=d0_dual_state.service.audio==0xffff;
   d0_dual_state.phase=3;d0_dual_state.parsed=0;memset(last_cc,255,sizeof last_cc);deadline=d0_now_ms()+10000;
  }
  if(d0_dual_state.phase==3)d0_dual_stream_send(data,size);
  pthread_mutex_lock(&d0_dual_lock);d0_dual_state.bytes+=size;d0_dual_state.sent=d0_dual_sent;d0_dual_state.sync_errors+=sync;d0_dual_state.cc_errors+=cc_errors;d0_dual_state.tei+=tei;
  int newly_ready=!d0_dual_state.ready && d0_dual_state.video_clear && d0_dual_state.audio_clear;
  if(newly_ready){d0_dual_state.ready=1;pthread_cond_broadcast(&d0_dual_cond);}int ready=d0_dual_state.ready;pthread_mutex_unlock(&d0_dual_lock);
  if(newly_ready)d0_dual_save();if(!ready && d0_now_ms()>deadline){result=22;goto done;}
 }
 done:
 // Keep immutable queued ECM frames alive across retries and channel changes.
 if(d0_dual_stopping()&&d0_dual_client>=0){close(d0_dual_client);d0_dual_client=-1;epg_viewer(0,0);}
 uint64_t drain_deadline=d0_now_ms()+3000;
 while(__atomic_load_n(&d2_cw_counts[0],__ATOMIC_SEQ_CST)<d0_ecm_issued && d0_now_ms()<drain_deadline){d0_dual_keepalive();usleep(10000);}
 if(cas)primary_other_close();

 if(mapped)DO("mif_demux_set_path_params",0,saved[0],saved[1]);
 if(record){DO("MI_TSR_Stop",record,0,0);DO("MI_TSR_Close",record,0,0);}
 if(allocated){struct {uint64_t physical;uint32_t size;} release={memory.physical,memory.size};DO("MI_OS_FreeIommuMemory",&release,0,0);}
 if(tuned)DO("fpi_demod_stop_connect",0,0,0);
 return result;
}
static void *d0_dual_main(void *unused) {
 (void)unused;int was_ever_ready=0;
 while(!d0_dual_stopping()) {
  int result=d0_dual_capture();pthread_mutex_lock(&d0_dual_lock);int was_ready=d0_dual_state.ready;d0_dual_state.running=0;d0_dual_state.ready=0;pthread_mutex_unlock(&d0_dual_lock);
  if(d0_dual_stopping())break;d0_dual_signal_error(result?result:21);was_ever_ready|=was_ready;if(!was_ever_ready)break;
  for(int i=0;i<20&&!d0_dual_stopping();i++){d0_dual_keepalive();usleep(100000);}
 }
 pthread_mutex_lock(&d0_dual_lock);d0_dual_state.running=0;pthread_cond_broadcast(&d0_dual_cond);pthread_mutex_unlock(&d0_dual_lock);return NULL;
}
static void d0_dual_stop(void) {
 __atomic_store_n(&d0_dual_stop_flag,1,__ATOMIC_SEQ_CST);if(d0_dual_has_worker){pthread_join(d0_dual_worker,NULL);d0_dual_has_worker=0;}
}
static int d0_dual_tune(uint16_t onid,uint16_t tsid,uint16_t sid,int *clear) {
 uint16_t tuple[]={sid,tsid,onid};uint8_t *program=calloc(1,0x10000),mux[104]={0};if(!program)return 3;
 int result=DO("db_dtv_get_prog_info_by_triplet",program,tuple,0);int16_t muxid;memcpy(&muxid,program+0x65d6,2);free(program);if(result!=1)return 9;
 if(DO("db_dtv_get_mux_info_by_muxid",mux,muxid,0)!=1)return 9;uint32_t frequency;memcpy(&frequency,mux+12,4);if(frequency<400000||frequency>800000)return 9;
 if(d0_dual_listener<0)return 41;d0_dual_stop();__atomic_store_n(&epg_scan_channel[0],epg_background(0),__ATOMIC_SEQ_CST);
 pthread_mutex_lock(&d0_dual_lock);memset(&d0_dual_state,0,sizeof d0_dual_state);d0_dual_state.frequency=frequency;d0_dual_state.onid=onid;d0_dual_state.tsid=tsid;d0_dual_state.sid=sid;d0_dual_sent=0;d0_dual_clients=d0_dual_send_errors=0;pthread_mutex_unlock(&d0_dual_lock);
 __atomic_store_n(&d0_dual_stop_flag,0,__ATOMIC_SEQ_CST);if(pthread_create(&d0_dual_worker,NULL,d0_dual_main,NULL))return 3;d0_dual_has_worker=1;
 struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);deadline.tv_sec+=35;
 pthread_mutex_lock(&d0_dual_lock);int wait=0;while(!d0_dual_state.ready&&!d0_dual_state.error&&!wait&&!epg_cancelled(0))wait=pthread_cond_timedwait(&d0_dual_cond,&d0_dual_lock,&deadline);
 *clear=d0_dual_state.ready;result=epg_cancelled(0)?24:(*clear?0:(d0_dual_state.error?d0_dual_state.error:23));pthread_mutex_unlock(&d0_dual_lock);if(result)d0_dual_stop();return result;
}
#undef DO
