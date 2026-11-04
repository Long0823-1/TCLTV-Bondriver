// Verified firmware FPI ABI: lock/strength/quality are uint8_t, CN is uint32_t.
// The Sony ISDB-T monitor reports C/N in millidecibels (dB * 1000).
static void control_signal(int fd,int index) {
 int (*lock_fn)(unsigned,uint8_t *)=resolve("fpi_demod_get_lock_status");
 int (*cn_fn)(unsigned,uint32_t *)=resolve("fpi_demod_get_signal_cn");
 int (*strength_fn)(unsigned,uint8_t *)=resolve("fpi_demod_get_signal_strength");
 int (*quality_fn)(unsigned,uint8_t *)=resolve("fpi_demod_get_signal_quality");
 uint8_t locked=0,strength=0,quality=0;uint32_t cn=0;int valid=0,active=1;
 int cable=0;
 if(index==0){pthread_mutex_lock(&d0_dual_lock);cable=d0_dual_state.catv.enabled;active=d0_dual_state.running;pthread_mutex_unlock(&d0_dual_lock);}
 else {pthread_mutex_lock(&d2_dual_lock);cable=d2_dual_state.catv.enabled;active=d2_dual_state.running;pthread_mutex_unlock(&d2_dual_lock);}
 if(cable){
  if(active&&!catv_lock((unsigned)index,&locked)&&locked){int (*cable_cn)(void *,uint32_t *)=resolve("_Z28sony_demod_isdbc_monitor_SNRP12sony_demod_tPi");valid=cable_cn&&!cable_cn(catv_demod((unsigned)index),&cn)&&cn<=100000;if(!valid)cn=0;}
  out(fd,"OK tuner=%d locked=%u cn_valid=%d cn_mdb=%u strength=0 quality=0 system=ISDB-C\n",index,locked,valid,cn);return;
 }
 if(index==1){pthread_mutex_lock(&d2_dual_lock);active=d2_dual_state.running;pthread_mutex_unlock(&d2_dual_lock);}
 if(active&&lock_fn&&lock_fn((unsigned)index,&locked)==0&&locked) {
  valid=cn_fn&&cn_fn((unsigned)index,&cn)==0&&cn<=100000;
  if(!valid)cn=0;
  if(!strength_fn||strength_fn((unsigned)index,&strength)||strength>100)strength=0;
  if(!quality_fn||quality_fn((unsigned)index,&quality)||quality>100)quality=0;
 }else locked=0;
 out(fd,"OK tuner=%d locked=%u cn_valid=%d cn_mdb=%u strength=%u quality=%u\n",index,locked,valid,cn,strength,quality);
}
