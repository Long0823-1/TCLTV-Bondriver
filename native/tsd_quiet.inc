// While this service tunes for BonDriver/Mirakurun in the background, the vendor B-CAS code reports ECM errors
// (for example "not subscribed - no viewing key", A103) to the TV UI, and the TV then leaves the HDMI input.
// The UI reads the error code through fpi_dtv_ca_get_ecm_error_code(), which returns the value of
// tvhdtv_bcas_get_ecm_retcode() inside libisdb.so. We replace libisdb.so's import of that function (a PLT/GOT
// slot, no code patching) with a wrapper that returns 0 while one of our own receivers is running. Notifications
// that carry no error are untouched. Touch /data/local/tmp/tsd_quiet_off to disable the wrapper.
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define QUIET_OFF_PATH "/data/local/tmp/tsd_quiet_off"
#define QUIET_LOG_PATH "/data/local/tmp/tsd_quiet.log"
static void quiet_log(const char *format,...){FILE *f=fopen(QUIET_LOG_PATH,"a");if(!f)return;va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);fputc(10,f);fclose(f);}
static volatile int quiet_log_budget=500;
static volatile unsigned quiet_hidden_total;

static int quiet_background_active(void){return d0_dual_state.running||d2_dual_state.running;}
// The vendor ECM thread keeps reporting for a while after a receiver stops (channel changes, Mirakurun scans),
// so keep hiding for QUIET_HOLD_MS after the last time one of our receivers was running.
#define QUIET_HOLD_MS 90000u
static volatile uint64_t quiet_last_active_ms;
static void *quiet_watch(void *arg){(void)arg;for(;;){if(quiet_background_active())quiet_last_active_ms=d2_now_ms();usleep(200000);}return NULL;}
static int quiet_hold(void){uint64_t last=quiet_last_active_ms;return quiet_background_active()||(last&&d2_now_ms()-last<QUIET_HOLD_MS);}
static int quiet_disabled(void){struct stat st;return !stat(QUIET_OFF_PATH,&st);}

static uint32_t (*quiet_real_get_ecm)(void);
static uint32_t quiet_get_ecm(void){
 uint32_t value=quiet_real_get_ecm?quiet_real_get_ecm():0;
 if(value){
  int active=quiet_hold(),off=quiet_disabled();
  if(quiet_log_budget>0){quiet_log_budget--;quiet_log("ecm_retcode=0x%x active=%d off=%d",value,active,off);}
  if(active&&!off){quiet_hidden_total++;return 0;}
 }
 return value;
}
static uintptr_t (*quiet_real_tvhdtv_notify)(uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static uintptr_t quiet_tvhdtv_notify(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){
 if(quiet_background_active()&&quiet_log_budget>0){quiet_log_budget--;quiet_log("tvhdtv_notify_event %u %u %u %u",(unsigned)a,(unsigned)b,(unsigned)c,(unsigned)d);}
 return quiet_real_tvhdtv_notify?quiet_real_tvhdtv_notify(a,b,c,d):0;
}

// app_notify_event(event, a1, a2): a1/a2 carry either the value or a pointer to it. Inspect both without risking a
// fault: probe pointers with write() to a pipe (EFAULT for an unmapped address).
static int quiet_probe_pipe[2]={-1,-1};
static int quiet_peek32(uint32_t pointer,uint32_t *value){
 if(pointer<0x10000||quiet_probe_pipe[1]<0)return 0;
 char scratch[8];ssize_t n;
 n=write(quiet_probe_pipe[1],(const void *)(uintptr_t)pointer,4);
 if(n!=4)return 0;
 if(read(quiet_probe_pipe[0],scratch,4)!=4)return 0;
 memcpy(value,scratch,4);return 1;
}
static uint32_t quiet_zero_value;
static int (*quiet_real_notify)(uint32_t,uint32_t,uint32_t);
static int quiet_notify(uint32_t event,uint32_t a1,uint32_t a2){
 int is_a1=((a1>>8)==0xA1)||((a2>>8)==0xA1),ptr_a1=0,ptr_a2=0;uint32_t v1=0,v2=0;
 if(quiet_hold()){
  if(quiet_peek32(a1,&v1)&&((v1&0xffff)>>8)==0xA1)ptr_a1=1;
  if(quiet_peek32(a2,&v2)&&((v2&0xffff)>>8)==0xA1)ptr_a2=1;
  if(quiet_log_budget>0&&(is_a1||ptr_a1||ptr_a2||event>0)){quiet_log_budget--;quiet_log("notify_event event=%u a1=0x%x a2=0x%x v1=0x%x v2=0x%x hit=%d%d%d",event,a1,a2,v1,v2,is_a1,ptr_a1,ptr_a2);}
  if(!quiet_disabled()){
   if(is_a1){quiet_hidden_total++;if((a1>>8)==0xA1)a1=0;if((a2>>8)==0xA1)a2=0;}
   if(ptr_a1){quiet_hidden_total++;a1=(uint32_t)(uintptr_t)&quiet_zero_value;}
   if(ptr_a2){quiet_hidden_total++;a2=(uint32_t)(uintptr_t)&quiet_zero_value;}
  }
 }
 return quiet_real_notify?quiet_real_notify(event,a1,a2):0;
}
// tvh_nortify_event(event, data1, data2) is what tvhdtv_bcas_main_proc really uses to tell the UI about ECM errors.
static int (*quiet_real_tvh)(uint32_t,uint32_t,uint32_t);
static int quiet_tvh(uint32_t event,uint32_t a1,uint32_t a2){
 int hit=((a1>>8)==0xA1)||((a2>>8)==0xA1);
 if(hit){
  int hold=quiet_hold(),off=quiet_disabled();
  if(quiet_log_budget>0){quiet_log_budget--;quiet_log("tvh_nortify_event event=0x%x a1=0x%x a2=0x%x hold=%d off=%d",event,a1,a2,hold,off);}
  if(hold&&!off){quiet_hidden_total++;if((a1>>8)==0xA1)a1=0;if((a2>>8)==0xA1)a2=0;}
 }
 return quiet_real_tvh?quiet_real_tvh(event,a1,a2):1;
}
struct quiet_target {const char *name;void *wrapper;void **real;};
static struct quiet_target quiet_targets[]={
 {"tvhdtv_bcas_get_ecm_retcode",(void *)quiet_get_ecm,(void **)&quiet_real_get_ecm},
 {"tvhdtv_notify_event",(void *)quiet_tvhdtv_notify,(void **)&quiet_real_tvhdtv_notify},
 {"app_notify_event",(void *)quiet_notify,(void **)&quiet_real_notify},
 {"tvh_nortify_event",(void *)quiet_tvh,(void **)&quiet_real_tvh},
};
struct quiet_scan {int patched;};
static int quiet_patch_lib(struct dl_phdr_info *info,size_t size,void *data){
 (void)size;struct quiet_scan *scan=data;
 if(!info->dlpi_name||!strstr(info->dlpi_name,"libisdb.so"))return 0;
 const ElfW(Dyn) *dyn=NULL;
 for(int i=0;i<info->dlpi_phnum;i++)if(info->dlpi_phdr[i].p_type==PT_DYNAMIC)dyn=(const ElfW(Dyn) *)(info->dlpi_addr+info->dlpi_phdr[i].p_vaddr);
 if(!dyn)return 0;
 uintptr_t base=info->dlpi_addr,jmprel=0,rel=0,symtab=0,strtab=0;size_t jmpsz=0,relsz=0;
 for(;dyn->d_tag!=DT_NULL;dyn++){
  uintptr_t v=dyn->d_un.d_ptr;if(v&&v<base)v+=base; // bionic keeps link-time addresses
  switch(dyn->d_tag){case DT_JMPREL:jmprel=v;break;case DT_PLTRELSZ:jmpsz=dyn->d_un.d_val;break;case DT_REL:rel=v;break;case DT_RELSZ:relsz=dyn->d_un.d_val;break;case DT_SYMTAB:symtab=v;break;case DT_STRTAB:strtab=v;break;default:break;}
 }
 if(!symtab||!strtab)return 0;
 struct {uintptr_t table;size_t size;} tables[2]={{jmprel,jmpsz},{rel,relsz}};
 for(int t=0;t<2;t++){
  if(!tables[t].table)continue;
  const ElfW(Rel) *r=(const ElfW(Rel) *)tables[t].table;
  for(size_t n=0;n<tables[t].size/sizeof *r;n++){
   unsigned type=ELF32_R_TYPE(r[n].r_info),sym=ELF32_R_SYM(r[n].r_info);
   if((type!=R_ARM_JUMP_SLOT&&type!=R_ARM_GLOB_DAT)||!sym)continue;
   const char *name=(const char *)strtab+((const ElfW(Sym) *)symtab)[sym].st_name;
   for(unsigned k=0;k<sizeof quiet_targets/sizeof quiet_targets[0];k++){
    if(strcmp(name,quiet_targets[k].name))continue;
    if(!*quiet_targets[k].real)*quiet_targets[k].real=resolve(quiet_targets[k].name);
    if(!*quiet_targets[k].real)continue;
    uintptr_t *slot=(uintptr_t *)(base+r[n].r_offset);
    uintptr_t page=(uintptr_t)slot&~(uintptr_t)4095;
    if(mprotect((void *)page,4096,PROT_READ|PROT_WRITE)){LOG("quiet: mprotect failed errno=%d",errno);continue;}
    *slot=(uintptr_t)quiet_targets[k].wrapper;scan->patched++;
    quiet_log("patched %s slot=0x%x",name,(unsigned)r[n].r_offset);
   }
  }
 }
 return 0;
}
// Called once from the service thread, after libisdb.so is loaded.
static void quiet_install(void){
 if(pipe(quiet_probe_pipe))quiet_probe_pipe[0]=quiet_probe_pipe[1]=-1;
 pthread_t watcher;if(!pthread_create(&watcher,NULL,quiet_watch,NULL))pthread_detach(watcher);
 struct quiet_scan scan={0};dl_iterate_phdr(quiet_patch_lib,&scan);
 LOG("quiet: patched %d import(s)",scan.patched);quiet_log("patched total=%d",scan.patched);
}
