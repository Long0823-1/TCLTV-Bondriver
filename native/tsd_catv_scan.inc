// Scan the cable attached to the terrestrial input; never switch RF inputs or LNB power.
struct catv_scan_entry{uint16_t onid,tsid,sid;uint32_t frequency,mode,slot;char name[321];};
struct catv_scan_context{struct catv_scan_entry entries[512];unsigned count;uint16_t pat_tsid;uint32_t frequency,mode,slot;int full;};
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
static int catv_scan_transport(uint32_t recorder,struct catv_scan_context *context){
 struct tsd_psi pat,sdt;tsd_psi_reset(&pat,0);tsd_psi_reset(&sdt,0x11);context->pat_tsid=0;
 uint8_t data[48128];struct{void *data;uint32_t capacity;} buffer={data,sizeof data};uint64_t start=d2_now_ms();
 unsigned before=context->count;
 while(d2_now_ms()-start<2600){
  uint32_t size=0;int r=(int)d2_invoke("MI_TSR_Read",recorder,(uintptr_t)&buffer,(uintptr_t)&size);
  if(r==3||(!r&&!size)){usleep(3000);continue;}if(r)return r;if(size>sizeof data||size%188)return 31;
  if(d2_now_ms()-start<1000)continue;
  for(uint32_t p=0;p<size;p+=188){tsd_psi_feed(&pat,data+p,catv_scan_pat,context);tsd_psi_feed(&sdt,data+p,catv_scan_sdt,context);}
  if(context->count>before||context->full)break;
 }return 0;
}
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
 for(unsigned i=0;i<count&&!context->full;i++){
  struct tsd_catv_channel channel={frequencies[i],0,1,1};r=catv_start(1,&channel);if(r)goto done;
  uint64_t deadline=d2_now_ms()+900;uint8_t locked=0;
  do{r=catv_lock(1,&locked);if(r)goto done;if(locked)break;usleep(10000);}while(d2_now_ms()<deadline);
  if(!locked)continue;
  usleep(150000);uint8_t header[256]={0};int (*get_header)(void *,void *)=resolve("_Z35sony_demod_isdbc_monitor_TSMFHeaderP12sony_demod_tP24sony_isdbc_tsmf_header_t");
  int tsmf=get_header&&get_header(catv_demod(1),header)==0;
  for(unsigned slot=1;slot<=(tsmf?15u:1u);slot++){
   uint16_t tsid=0,network=0;if(tsmf){memcpy(&tsid,header+84+2*(slot-1),2);memcpy(&network,header+114+2*(slot-1),2);if(!tsid||tsid==65535||!network||network==65535)continue;}
   channel.mode=tsmf?1:0;channel.slot=slot;if(tsmf){r=catv_start(1,&channel);if(r)goto done;usleep(200000);}
   context->frequency=channel.frequency;context->mode=channel.mode;context->slot=slot;
   r=catv_scan_transport(recorder,context);if(r)goto done;
  }
  DIAG("CATV scan frequency=%u progress=%u/%u channels=%u",channel.frequency,i+1,count,context->count);
 }
 if(context->full){r=44;goto done;}
 if(context->count){
  FILE *file=fopen(TSD_CATV_PATH ".new","w");if(!file){r=45;goto done;}
  for(unsigned i=0;i<context->count;i++){struct catv_scan_entry *e=&context->entries[i];fprintf(file,"%u %u %u %u %u %u arib:%s\n",e->onid,e->tsid,e->sid,e->frequency,e->mode,e->slot,e->name);}
  int bad=ferror(file);if(fclose(file))bad=1;
  if(bad||chmod(TSD_CATV_PATH ".new",0600)||rename(TSD_CATV_PATH ".new",TSD_CATV_PATH))r=45;
  else *found=context->count;
 }else r=46;
 done:
 catv_stop(1);
 if(recorder){d2_invoke("MI_TSR_Stop",recorder,0,0);d2_invoke("MI_TSR_Close",recorder,0,0);}
 if(allocated){struct{uint64_t physical;uint32_t size;} release={memory.physical,memory.size};d2_invoke("MI_OS_FreeIommuMemory",(uintptr_t)&release,0,0);}
 if(connected)d2_invoke("MI_TSIO_DisconnectInput",tsio,source,0);if(tsio)d2_invoke("MI_TSIO_Close",tsio,0,0);
 free(context);return r;
}
