// The vendor database route is global: serialize both tuner lookups and restore
// the route before touching capture state. Never keep it selected for playback.
static int tsd_lookup_channel(int tuner,uint16_t onid,uint16_t tsid,uint16_t sid,uint32_t *frequency,uint32_t *satellite,struct tsd_catv_channel *catv) {
 int cable=tsd_catv_lookup(onid,tsid,sid,catv);if(cable<0)return 9;
 if(cable){*frequency=catv->frequency;*satellite=0;return 0;}
 typedef int (*vendor_fn)(uintptr_t,uintptr_t,uintptr_t);
 vendor_fn get_route=(vendor_fn)resolve("db_dtv_get_route"),get_target=(vendor_fn)resolve("db_dtv_get_route_by_onid_tsid"),set_route=(vendor_fn)resolve("db_dtv_set_route"),get_program=(vendor_fn)resolve("db_dtv_get_prog_info_by_triplet"),get_mux=(vendor_fn)resolve("db_dtv_get_mux_info_by_muxid");
 if(!get_route||!get_target||!set_route||!get_program||!get_mux)return 9;
 uint8_t *program=calloc(1,0x10000),mux[104]={0};if(!program)return 3;
 uint16_t tuple[]={sid,tsid,onid};int result=9,changed=0;
 pthread_mutex_lock(&route_switch_lock);
 int current=get_route(0,0,0),target=get_target(onid,tsid,0);
 if((current!=4&&current!=8)||(target!=4&&target!=8))goto done;
 // As in the fork, satellite background EPG tuning is excluded because the
 // vendor middleware can contend with a viewer's satellite path.
 if(target==8&&epg_background(tuner))goto done;
 changed=target!=current;
 if(changed)set_route((uintptr_t)target,0,0);
 if(get_route(0,0,0)!=target)goto done;
 if(get_program((uintptr_t)program,(uintptr_t)tuple,0)!=1)goto done;
 int16_t muxid;memcpy(&muxid,program+0x65d6,2);
 if(get_mux((uintptr_t)mux,(uintptr_t)(uint16_t)muxid,0)!=1)goto done;
 memcpy(frequency,mux+12,4);memcpy(satellite,mux+24,4);
 if(!tsd_tuning_valid(target,*frequency,*satellite))goto done;
 result=0;
 done:
 if(changed){set_route((uintptr_t)current,0,0);if(get_route(0,0,0)!=current)result=9;}
 pthread_mutex_unlock(&route_switch_lock);free(program);return result;
}
