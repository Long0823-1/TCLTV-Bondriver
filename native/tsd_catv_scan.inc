#define CATV_ONLY_PATH "/data/local/tmp/tsd_catv_scan_freqs"
#define CATV_LOG_PATH "/data/local/tmp/tsd_catv_scan.log"
static int catv_only;
#define CLOG(...) do{if(catv_only){FILE *log_file=fopen(CATV_LOG_PATH,"a");if(log_file){fprintf(log_file,__VA_ARGS__);fclose(log_file);}}}while(0)
// Scan the cable attached to the terrestrial input; never switch RF inputs or LNB power.
struct catv_scan_entry{uint16_t onid,tsid,sid;uint32_t frequency,mode,slot;char name[321];};
struct catv_scan_context{struct catv_scan_entry entries[512];unsigned count;uint16_t pat_tsid;uint32_t frequency,mode,slot,bytes;int full;};
static void catv_scan_pat(void *opaque,const uint8_t *s,size_t n){struct catv_scan_context *c=opaque;if(n>=12&&s[0]==0&&(s[5]&1))c->pat_tsid=(uint16_t)s[3]<<8|s[4];}
static void catv_scan_sdt(void *opaque,const uint8_t *s,size_t n){
 struct catv_scan_context *c=opaque;if(n<15||s[0]!=0x42||!(s[5]&1)||((uint16_t)s[3]<<8|s[4])!=c->pat_tsid)return;
 uint16_t onid=(uint16_t)s[8]<<8|s[9];if(!onid)return;
 for(size_t p=11;p+5<=n-4;){
  uint16_t sid=(uint16_t)s[p]<<8|s[p+1];size_t len=((s[p+3]&15)<<8)|s[p+4],end=p+5+len;if(end>n-4)return;
  for(size_t d=p+5;d+2<=end;){size_t dl=s[d+1];if(d+2+dl>end)break;
   if(s[d]==0x48&&dl>=3){const uint8_t *v=s+d+2;unsigned type=v[0];size_t provider=v[1];
    if((type==1||type==0xa1||type==0xa5)&&provider+3<=dl&&sid){
     size_t length=v[2+provider];if(length>160||provider+3+length>dl)break;
     unsigned i=0;for(;i<c->count;i++)if(c->entries[i].onid==onid&&c->entries[i].tsid==c->pat_tsid&&c->entries[i].sid==sid)break;
     if(i==c->count){if(c->count==512){c->full=1;return;}struct catv_scan_entry *e=&c->entries[c->count++];memset(e,0,sizeof *e);e->onid=onid;e->tsid=c->pat_tsid;e->sid=sid;e->frequency=c->frequency;e->mode=c->mode;e->slot=c->slot;
      static const char hex[]="0123456789abcdef";for(size_t k=0;k<length;k++){e->name[2*k]=hex[v[3+provider+k]>>4];e->name[2*k+1]=hex[v[3+provider+k]&15];}
     }
    }
   }d+=2+dl;
  }p=end;
 }
}
static uint32_t catv_pid_count[8192];
static void catv_log_pids(void){if(!catv_only)return;unsigned distinct=0,top[5]={0,0,0,0,0};for(unsigned p=0;p<8192;p++)if(catv_pid_count[p]){distinct++;for(int k=0;k<5;k++)if(!catv_pid_count[top[k]]||catv_pid_count[p]>catv_pid_count[top[k]]){for(int m=4;m>k;m--)top[m]=top[m-1];top[k]=p;break;}}
 CLOG("    pids distinct=%u pat=%u sdt=%u null=%u top=%x:%u,%x:%u,%x:%u\n",distinct,catv_pid_count[0],catv_pid_count[0x11],catv_pid_count[0x1fff],top[0],catv_pid_count[top[0]],top[1],catv_pid_count[top[1]],top[2],catv_pid_count[top[2]]);}
static int catv_scan_transport(uint32_t recorder,struct catv_scan_context *context){
 struct tsd_psi pat,sdt;tsd_psi_reset(&pat,0);tsd_psi_reset(&sdt,0x11);context->pat_tsid=0;
 uint8_t data[48128];struct{void *data;uint32_t capacity;} buffer={data,sizeof data};uint64_t start=d2_now_ms();
 unsigned before=context->count;context->bytes=0;memset(catv_pid_count,0,sizeof catv_pid_count);
 while(d2_now_ms()-start<9000){
  uint32_t size=0;int r=(int)d2_invoke("MI_TSR_Read",recorder,(uintptr_t)&buffer,(uintptr_t)&size);
  if(r==3||(!r&&!size)){usleep(3000);continue;}if(r)return r;if(size>sizeof data||size%188)return 31;
  context->bytes+=size;if(d2_now_ms()-start<1000)continue;if(catv_only)for(uint32_t p=0;p<size;p+=188)if(data[p]==0x47)catv_pid_count[((data[p+1]&0x1f)<<8)|data[p+2]]++;
  for(uint32_t p=0;p<size;p+=188){tsd_psi_feed(&pat,data+p,catv_scan_pat,context);tsd_psi_feed(&sdt,data+p,catv_scan_sdt,context);}
  if(context->count>before||context->full)break;
 }return 0;
}
#define CATV_PROGRESS_PATH "/data/local/tmp/tsd_catv_progress"
// "<done> <total> <frequency kHz> <services found>" for the Android app; removed when the scan ends.
static void catv_progress(unsigned done,unsigned total,uint32_t frequency,unsigned services){FILE *f=fopen(CATV_PROGRESS_PATH,"w");if(f){fprintf(f,"%u %u %u %u\n",done,total,frequency,services);fclose(f);chmod(CATV_PROGRESS_PATH,0644);}}
static int catv_scan(unsigned *found){
 *found=0;int r=d2_record_init();if(r)return r;
 struct catv_scan_context *context=calloc(1,sizeof *context);if(!context)return 3;
 uint32_t tsio=0,recorder=0,source=0xae100001;int connected=0,allocated=0;uint8_t mode=0,all=1;
 struct d2_allocation memory={0};strcpy(memory.tag,"tsr_down");memory.size=0x200000;memory.mode=1;
 struct{const char *name;uint32_t path;} input={"TCL_CATV_SCAN",0};
 r=(int)d2_invoke("MI_TSIO_Open",(uintptr_t)&input,(uintptr_t)&tsio,0);if(r)goto done;
 r=(int)d2_invoke("MI_TSIO_ConnectInput",tsio,source,(uintptr_t)&mode);if(r)goto done;connected=1;
 r=(int)d2_invoke("MI_OS_AllocateIommuMemory",(uintptr_t)&memory,0,0);if(r)goto done;allocated=1;
 struct d2_recorder params={0};params.name="TCL_CATV_SCAN";params.tsio=tsio;params.physical=memory.physical;params.size=memory.size;params.packet=188;
 r=(int)d2_invoke("mi_tsr_Open",(uintptr_t)&params,(uintptr_t)&recorder,0);if(r)goto done;
 r=(int)d2_invoke("MI_TSR_AddPid",recorder,0,0);if(r)goto done;
 r=(int)d2_invoke("MI_TSR_Start",recorder,(uintptr_t)&all,0);if(r)goto done;
 unsigned frequencies[128],count=0;
 for(unsigned f=93000;f<=159000;f+=6000)frequencies[count++]=f;
 const unsigned vhf[]={167000,173000,179000,185000,191000,195000,201000,207000,213000,219000};
 for(unsigned i=0;i<sizeof vhf/sizeof vhf[0];i++)frequencies[count++]=vhf[i];
 for(unsigned f=225000;f<=465000;f+=6000)frequencies[count++]=f;
 for(unsigned f=473000;f<=767000;f+=6000)frequencies[count++]=f;
 // Diagnostic mode: if this file lists frequencies (kHz), scan only those, log details, and keep the real catalog untouched.
 int only=0;catv_only=0;{FILE *list=fopen(CATV_ONLY_PATH,"r");if(list){unsigned value;count=0;while(count<128&&fscanf(list,"%u",&value)==1)frequencies[count++]=value;fclose(list);only=1;catv_only=1;unlink(CATV_LOG_PATH);}}
 for(unsigned i=0;i<count&&!context->full;i++){
  catv_progress(i,count,frequencies[i],context->count);
  struct tsd_catv_channel channel={frequencies[i],0,0,1}; /* slot 0 = no stream selection, so the TSMF header can be read */ r=catv_start(1,&channel);if(r)goto done;
  uint64_t deadline=d2_now_ms()+900;uint8_t locked=0;
  do{r=catv_lock(1,&locked);if(r)goto done;if(locked)break;usleep(10000);}while(d2_now_ms()<deadline);
  CLOG("f=%u locked=%d\n",frequencies[i],locked);
  if(!locked)continue;
  usleep(150000);uint8_t header[2048]={0}; /* the vendor TSMF header struct is larger than the 256 bytes the probe dumped */int (*get_header)(void *,void *)=resolve("_Z35sony_demod_isdbc_monitor_TSMFHeaderP12sony_demod_tP24sony_isdbc_tsmf_header_t");
  int tsmf=0;for(uint64_t header_deadline=d2_now_ms()+1500;;){tsmf=get_header&&get_header(catv_demod(1),header)==0;if(tsmf||d2_now_ms()>=header_deadline)break;usleep(100000);}
  CLOG("  tsmf=%d header=%02x%02x%02x%02x\n",tsmf,header[0],header[1],header[2],header[3]);
  for(unsigned slot=1;slot<=(tsmf?15u:1u);slot++){
   uint16_t tsid=0,network=0;if(tsmf){memcpy(&tsid,header+84+2*(slot-1),2);memcpy(&network,header+114+2*(slot-1),2);if(!tsid||tsid==65535||!network||network==65535)continue;}
   channel.mode=tsmf?1:0;channel.slot=slot;if(tsmf){
    // TSMF stream selection does not always lock on the first retune: retry with sleep / reset between attempts.
    int (*sync)(void *,uint8_t *,uint8_t *,uint8_t *)=resolve("_Z33sony_demod_isdbc_monitor_SyncStatP12sony_demod_tPhS1_S1_");uint8_t dl=0,tl=0,ul=0;int relocked=0,attempt=0;
    for(;attempt<6&&!relocked;attempt++){
     if(attempt%3==1){catv_stop(1);usleep(100000);}
     if(attempt%3==2){r=catv_start_raw(1,channel.frequency,0,0,0,0);if(r){CLOG("  reset=%d\n",r);goto done;}usleep(500000);}
     r=catv_start(1,&channel);if(r){CLOG("  slot=%u start=%d\n",slot,r);goto done;}
     for(uint64_t lock_deadline=d2_now_ms()+2500;;){if(sync&&!sync(catv_demod(1),&dl,&tl,&ul)&&(dl||tl)){relocked=1;break;}if(d2_now_ms()>=lock_deadline)break;usleep(20000);}
    }
    CLOG("  slot=%u relocked=%d attempts=%d demod=%u ts=%u\n",slot,relocked,attempt,dl,tl);
    usleep(500000);d2_invoke("MI_TSR_Stop",recorder,0,0);d2_invoke("MI_TSR_Start",recorder,(uintptr_t)&all,0);}
   context->frequency=channel.frequency;context->mode=channel.mode;context->slot=slot;
   unsigned before=context->count;context->bytes=0;
   r=catv_scan_transport(recorder,context);if(r){CLOG("  slot=%u transport=%d\n",slot,r);goto done;}
   catv_log_pids();CLOG("  slot=%u tsid=%u net=%u pat_tsid=%u services+=%u bytes=%u\n",slot,tsid,network,context->pat_tsid,context->count-before,context->bytes);
  }
  DIAG("CATV scan frequency=%u progress=%u/%u channels=%u",channel.frequency,i+1,count,context->count);
 }
 if(context->full){r=44;goto done;}
 if(only){FILE *file=fopen(TSD_CATV_PATH ".debug","w");if(file){for(unsigned i=0;i<context->count;i++){struct catv_scan_entry *e=&context->entries[i];fprintf(file,"%u %u %u %u %u %u\n",e->onid,e->tsid,e->sid,e->frequency,e->mode,e->slot);}fclose(file);}*found=context->count;r=0;goto done;}
 if(context->count){
  FILE *file=fopen(TSD_CATV_PATH ".new","w");if(!file){r=45;goto done;}
  for(unsigned i=0;i<context->count;i++){struct catv_scan_entry *e=&context->entries[i];fprintf(file,"%u %u %u %u %u %u arib:%s\n",e->onid,e->tsid,e->sid,e->frequency,e->mode,e->slot,e->name);}
  int bad=ferror(file);if(fclose(file))bad=1;
  if(bad||chmod(TSD_CATV_PATH ".new",0600)||rename(TSD_CATV_PATH ".new",TSD_CATV_PATH))r=45;
  else *found=context->count;
 }else r=46;
 done:
 unlink(CATV_PROGRESS_PATH);
 catv_stop(1);
 if(recorder){d2_invoke("MI_TSR_Stop",recorder,0,0);d2_invoke("MI_TSR_Close",recorder,0,0);}
 if(allocated){struct{uint64_t physical;uint32_t size;} release={memory.physical,memory.size};d2_invoke("MI_OS_FreeIommuMemory",(uintptr_t)&release,0,0);}
 if(connected)d2_invoke("MI_TSIO_DisconnectInput",tsio,source,0);if(tsio)d2_invoke("MI_TSIO_Close",tsio,0,0);
 free(context);return r;
}
