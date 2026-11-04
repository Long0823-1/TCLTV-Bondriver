// ISDB-S parameters adapted from 2m3k/TCLTV-Bondriver-3wave 2c7bb42 (MIT).
#ifndef TSD_TUNING_H
#define TSD_TUNING_H
#include <stdint.h>
static int tsd_tuning_valid(int route,uint32_t frequency,uint32_t satellite) {
 if(route==4)return satellite==0&&frequency>=400000&&frequency<=800000;
 return route==8&&(satellite==59||satellite==60)&&frequency>=950000&&frequency<=2150000;
}
static void tsd_tuning_config(uint32_t cfg[7],unsigned tuner,uint32_t frequency,uint32_t satellite) {
 cfg[0]=tuner;cfg[1]=satellite?8:4;cfg[2]=frequency;cfg[3]=satellite?0:2;
 cfg[4]=satellite;cfg[5]=0;cfg[6]=0;
}
#endif
