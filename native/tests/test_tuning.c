#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../tsd_tuning.h"
#define TSD_CATV_PATH "tsd_catv_test_fixture"
#include "../tsd_catv.h"
typedef int pthread_mutex_t;
static pthread_mutex_t route_switch_lock;
static void pthread_mutex_lock(pthread_mutex_t *lock){++*lock;}
static void pthread_mutex_unlock(pthread_mutex_t *lock){--*lock;}
static int route=4,fixture_target=8,background,fail_program,fail_mux,fail_set;
static uint32_t fixture_frequency=1049480,fixture_satellite=59;
static int epg_background(int tuner){(void)tuner;return background;}
static int get_route(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;return route;}
static int get_target(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;return fixture_target;}
static int set_route(uintptr_t a,uintptr_t b,uintptr_t c){(void)b;(void)c;if(!fail_set)route=(int)a;return 0;}
static int get_program(uintptr_t a,uintptr_t b,uintptr_t c){(void)b;(void)c;int16_t muxid=17;memcpy((uint8_t *)a+0x65d6,&muxid,2);return !fail_program;}
static int get_mux(uintptr_t a,uintptr_t b,uintptr_t c){(void)b;(void)c;memcpy((uint8_t *)a+12,&fixture_frequency,4);memcpy((uint8_t *)a+24,&fixture_satellite,4);return !fail_mux;}
typedef int (*fn)(uintptr_t,uintptr_t,uintptr_t);
static fn resolve(const char *name){
 if(!strcmp(name,"db_dtv_get_route"))return get_route;
 if(!strcmp(name,"db_dtv_get_route_by_onid_tsid"))return get_target;
 if(!strcmp(name,"db_dtv_set_route"))return set_route;
 if(!strcmp(name,"db_dtv_get_prog_info_by_triplet"))return get_program;
 return get_mux;
}
#include "../tsd_tuning.inc"
static int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL line %d\n",__LINE__);return 1;}}while(0)
int main(void){
 uint32_t cfg[7],f,s;struct tsd_catv_channel catv;remove(TSD_CATV_PATH);
 tsd_tuning_config(cfg,0,473143,0);CHECK(cfg[0]==0&&cfg[1]==4&&cfg[2]==473143&&cfg[3]==2&&cfg[4]==0);
 tsd_tuning_config(cfg,1,1049480,59);CHECK(cfg[0]==1&&cfg[1]==8&&cfg[2]==1049480&&cfg[3]==0&&cfg[4]==59&&cfg[5]==0&&cfg[6]==0);
 CHECK(tsd_tuning_valid(8,1613000,60));CHECK(!tsd_tuning_valid(8,1049480,0));CHECK(!tsd_tuning_valid(8,1049480,61));CHECK(!tsd_tuning_valid(4,1049480,0));CHECK(!tsd_tuning_valid(8,473143,59));CHECK(!tsd_tuning_valid(10,1049480,59));
 CHECK(tsd_lookup_channel(0,4,16400,101,&f,&s,&catv)==0&&f==fixture_frequency&&s==59&&route==4&&route_switch_lock==0);
 fixture_target=4;route=8;fixture_frequency=473143;fixture_satellite=0;CHECK(tsd_lookup_channel(1,32738,32738,1040,&f,&s,&catv)==0&&route==8);
 fixture_target=8;route=4;fixture_frequency=1049480;fixture_satellite=59;
 fail_program=1;CHECK(tsd_lookup_channel(0,4,16400,101,&f,&s,&catv)==9&&route==4&&route_switch_lock==0);fail_program=0;
 fail_mux=1;CHECK(tsd_lookup_channel(0,4,16400,101,&f,&s,&catv)==9&&route==4&&route_switch_lock==0);fail_mux=0;
 background=1;CHECK(tsd_lookup_channel(1,4,16400,101,&f,&s,&catv)==9&&route==4&&route_switch_lock==0);background=0;
 fail_set=1;CHECK(tsd_lookup_channel(0,4,16400,101,&f,&s,&catv)==9&&route==4&&route_switch_lock==0);fail_set=0;
 fixture_satellite=61;CHECK(tsd_lookup_channel(0,4,16400,101,&f,&s,&catv)==9&&route==4&&route_switch_lock==0);
 FILE *catalog=fopen(TSD_CATV_PATH,"w");CHECK(catalog!=NULL);fputs("4 16528 211 345000 1 1 BS11\n",catalog);fclose(catalog);
 CHECK(tsd_lookup_channel(1,4,16528,211,&f,&s,&catv)==0&&catv.enabled&&f==345000&&s==0&&catv.mode==1&&catv.slot==1&&route==4);
 catalog=fopen(TSD_CATV_PATH,"a");CHECK(catalog!=NULL);fputs("4 16528 211 345000 1 1 duplicate\n",catalog);fclose(catalog);
 CHECK(tsd_lookup_channel(1,4,16528,211,&f,&s,&catv)==9&&!catv.enabled&&route==4);
 catalog=fopen(TSD_CATV_PATH,"w");CHECK(catalog!=NULL);fputs("4 16528 211 345000 1 16 invalid\n",catalog);fclose(catalog);
 CHECK(tsd_lookup_channel(1,4,16528,211,&f,&s,&catv)==9&&!catv.enabled);
 CHECK(remove(TSD_CATV_PATH)==0);
 catalog=fopen(TSD_CATV_PATH,"w");CHECK(catalog!=NULL);fputs("4294967300 16528 211 345000 1 1 overflow\n",catalog);fclose(catalog);
 CHECK(tsd_lookup_channel(1,4,16528,211,&f,&s,&catv)==9&&!catv.enabled);CHECK(remove(TSD_CATV_PATH)==0);
 printf("Tuning/route/CATV checks: %d\n",checks);return 0;
}
