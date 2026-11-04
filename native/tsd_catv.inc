// G06/CXD2856 Sony ISDB-C path verified on 2026-11-05. Installer also checks
// sitatvservice's complete SHA-256 before loading this firmware-specific code.
static uint32_t catv_word(const void *p,unsigned offset){uint32_t value;memcpy(&value,(const uint8_t *)p+offset,4);return value;}
static void *catv_demod(unsigned tuner){
 void *anchor=resolve("fpi_cxd2856_start_connect");Dl_info info;
 if(tuner>1||!anchor||!dladdr(anchor,&info)||(uintptr_t)anchor-(uintptr_t)info.dli_fbase!=0x9ba10c||catv_word(anchor,0)!=0xe92d4df0)return NULL;
 uint8_t *demod=(uint8_t *)info.dli_fbase+0x1a2feb4+tuner*0x520;
 if(catv_word(demod,0x2e4)!=0x90||catv_word(demod,0x308)!=(uintptr_t)demod)return NULL;
 return demod;
}
static int catv_start_raw(unsigned tuner,uint32_t frequency,uint32_t mode,uint32_t select,uint16_t id,uint16_t network){
 void *demod=catv_demod(tuner);if(!demod)return 9;
 int (*tune)(void *,void *)=resolve("_Z21sony_demod_isdbc_TuneP12sony_demod_tP23sony_isdbc_tune_param_t");
 int (*repeater)(void *,unsigned)=resolve("_Z28sony_demod_I2cRepeaterEnableP12sony_demod_th");
 int (*end)(void *)=resolve("_Z18sony_demod_TuneEndP12sony_demod_t");
 if(!tune||!repeater||!end)return 9;
 void *tuner_object=(void *)(uintptr_t)catv_word(demod,0x30c);if(!tuner_object)return 9;
 int (*rf)(void *,uint32_t,unsigned,unsigned)=(void *)(uintptr_t)catv_word(tuner_object,0x38);if(!rf)return 9;
 struct{uint32_t frequency,mode,select;uint16_t id,network;} params={frequency,mode,select,id,network};
 int r=tune(demod,&params);if(r)return r;
 r=repeater(demod,1);if(r)return r;
 r=rf(tuner_object,frequency,8,6);int off=repeater(demod,0);if(!r)r=off;
 return r?r:end(demod);
}
static int catv_start(unsigned tuner,const struct tsd_catv_channel *channel){return catv_start_raw(tuner,channel->frequency,channel->mode,channel->slot?1u:0u,(uint16_t)channel->slot,0);}
static int catv_lock(unsigned tuner,uint8_t *locked){
 void *demod=catv_demod(tuner);int (*sync)(void *,uint8_t *,uint8_t *,uint8_t *)=resolve("_Z33sony_demod_isdbc_monitor_SyncStatP12sony_demod_tPhS1_S1_");
 uint8_t demod_lock=0,ts_lock=0,unlock=0;*locked=0;if(!demod||!sync)return 9;
 int r=sync(demod,&demod_lock,&ts_lock,&unlock);if(!r)*locked=demod_lock&&ts_lock;return r;
}
static void catv_stop(unsigned tuner){void *demod=catv_demod(tuner);int (*sleep_fn)(void *)=resolve("_Z16sony_demod_SleepP12sony_demod_t");if(demod&&sleep_fn)sleep_fn(demod);}

static uint64_t catv_now_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;}
// Tune and wait for a lock. TSMF stream selection does not always lock on the first try, so retry with a
// demodulator sleep / reset in between. Returns 0 when locked, 20 when no lock, 21 when asked to stop.
static int catv_tune_locked(unsigned tuner,const struct tsd_catv_channel *channel,unsigned per_attempt_ms,unsigned attempts,int (*stopping)(void),void (*tick)(void)){
 int (*sync)(void *,uint8_t *,uint8_t *,uint8_t *)=resolve("_Z33sony_demod_isdbc_monitor_SyncStatP12sony_demod_tPhS1_S1_");
 for(unsigned attempt=0;attempt<attempts;attempt++){
  if(attempt%3==1){catv_stop(tuner);usleep(100000);}
  if(attempt%3==2){catv_start_raw(tuner,channel->frequency,0,0,0,0);usleep(500000);}
  int r=catv_start(tuner,channel);if(r)return r;
  for(uint64_t until=catv_now_ms()+per_attempt_ms;;){
   if(stopping&&stopping())return 21;
   void *demod=catv_demod(tuner);uint8_t dl=0,tl=0,ul=0;
   if(demod&&sync&&!sync(demod,&dl,&tl,&ul)&&(dl||tl))return 0;
   if(catv_now_ms()>=until)break;
   if(tick)tick();usleep(30000);
  }
 }
 return 20;
}
