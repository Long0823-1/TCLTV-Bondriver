// Live descramblers require a normal demux PID filter in addition to TSR.
// Own these filters explicitly and release them before changing the channel.
static struct {uint32_t handle;uint16_t pid;int opened,started,registered;} primary_other[12],primary_main[2];
static void primary_other_close(void) {
 int (*unset)(unsigned,const uint32_t *)=resolve("tvhdtv_unset_desc_other_pids");
 int (*stop)(unsigned,uint32_t)=resolve("mif_demux_pid_stop");
 int (*close_filter)(unsigned,uint32_t)=resolve("mif_demux_close_filter");
 for(unsigned i=0;i<2;i++){if(primary_main[i].started&&stop)stop(0,primary_main[i].handle);if(primary_main[i].opened&&close_filter)close_filter(0,primary_main[i].handle);memset(&primary_main[i],0,sizeof(primary_main[i]));}
 for(unsigned i=0;i<12;i++){if(primary_other[i].registered&&unset)unset(0,&primary_other[i].handle);if(primary_other[i].started&&stop)stop(0,primary_other[i].handle);if(primary_other[i].opened&&close_filter)close_filter(0,primary_other[i].handle);memset(&primary_other[i],0,sizeof(primary_other[i]));}
}
static int primary_other_open(const struct tsd_mux *mux,const struct tsd_service_pids *selected) {
 int (*open_filter)(unsigned,unsigned,uint32_t *)=resolve("mif_demux_open_filter");
 int (*set_filter)(unsigned,uint32_t,uint16_t,void *,void *)=resolve("mif_demux_set_filter");
 int (*start)(unsigned,uint32_t)=resolve("mif_demux_pid_start");
 int (*set_desc)(unsigned,const uint32_t *,const uint16_t *,unsigned)=resolve("tvhdtv_set_desc_other_pids");
 if(!open_filter||!set_filter||!start||!set_desc)return 8;
 // Reuse the two normal AV descramblers already initialized by the firmware.
 // Only their handle IDs are read; key material is never read or copied.
 void *anchor=resolve("descramble_2K_PVR_reopen");Dl_info info;
 if(!anchor||!dladdr(anchor,&info)||(uintptr_t)anchor-(uintptr_t)info.dli_fbase!=0x4aaa4)return 9;
 uintptr_t base=(uintptr_t)info.dli_fbase;
 if(*(const uint32_t *)(base+0x54e0c)!=0xe59f0338||*(const uint32_t *)(base+0x54f2c)!=0xe59f0248)return 9;
 uint32_t main_desc[2]={*(const uint32_t *)(base+0x2081a8),*(const uint32_t *)(base+0x2081ac)};
 if(main_desc[0]>=512||main_desc[1]>=512||main_desc[0]==main_desc[1])return 9;
 int (*set_pid)(uint32_t,uint16_t)=resolve("descramble_set_pid");if(!set_pid)return 8;
 uint16_t main_pids[2]={selected->video,selected->audio};
 for(unsigned i=0;i<2;i++)if(main_pids[i]!=0xffff){
  primary_main[i].pid=main_pids[i];int result=open_filter(0,10,&primary_main[i].handle);if(result){primary_other_close();return result;}primary_main[i].opened=1;
  result=set_filter(0,primary_main[i].handle,main_pids[i],NULL,NULL);
  if(!result)result=start(0,primary_main[i].handle);if(!result)primary_main[i].started=1;
  if(!result)result=set_pid(main_desc[i],main_pids[i]);
  if(result){primary_other_close();return result;}
 }
 unsigned count=0;uint16_t pids[12];
 for(unsigned i=0;i<mux->count;i++){const struct tsd_service_pids *s=&mux->programs[i].service;if(s->ecm==0xffff||s->ecm!=selected->ecm)continue;
  for(unsigned j=0;j<s->count;j++){uint16_t pid=s->pids[j];if(tsd_mux_pid_clear(&mux->programs[i],pid)||pid<0x100||pid==s->pcr||pid==s->ecm||pid==selected->video||pid==selected->audio)continue;
   unsigned k=0;for(;k<count;k++)if(pids[k]==pid)break;if(k<count)continue;if(count==12){primary_other_close();return 42;}pids[count++]=pid;
  }
 }
 for(unsigned i=0;i<count;i++){
  primary_other[i].pid=pids[i];int result=open_filter(0,10,&primary_other[i].handle);if(result)goto failed;primary_other[i].opened=1;
  result=set_filter(0,primary_other[i].handle,pids[i],NULL,NULL);if(result)goto failed;
  result=start(0,primary_other[i].handle);if(result)goto failed;primary_other[i].started=1;
  result=set_desc(0,&primary_other[i].handle,&primary_other[i].pid,1);if(result)goto failed;primary_other[i].registered=1;
  continue;
 failed:primary_other_close();return result;
 }
 return 0;
}
