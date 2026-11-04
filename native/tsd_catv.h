#ifndef TSD_CATV_H
#define TSD_CATV_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#ifndef TSD_CATV_PATH
#define TSD_CATV_PATH "/data/local/tmp/tsd_catv_channels"
#endif
struct tsd_catv_channel {uint32_t frequency,mode,slot;int enabled;};
static int tsd_catv_numbers(const char *line,unsigned values[6]){
 const char *p=line;
 for(unsigned i=0;i<6;i++){
  while(*p==' '||*p=='\t')p++;
  if(*p<'0'||*p>'9')return -1;
  char *end;errno=0;unsigned long value=strtoul(p,&end,10);
  if(errno||value>UINT32_MAX||end==p||(*end&&!isspace((unsigned char)*end)))return -1;
  values[i]=(unsigned)value;p=end;
 }return 0;
}
// Catalog: ONID TSID SID frequency_kHz TSMF_mode relative_slot UTF-8_name.
// Writers replace the whole root-owned catalog atomically.
static int tsd_catv_lookup(uint16_t onid,uint16_t tsid,uint16_t sid,struct tsd_catv_channel *channel){
 memset(channel,0,sizeof *channel);FILE *f=fopen(TSD_CATV_PATH,"r");if(!f)return 0;
 char line[1024];unsigned count=0;int found=0;
 while(fgets(line,sizeof line,f)){
  if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;
  unsigned v[6];if(++count>512||(!strchr(line,'\n')&&!feof(f))||tsd_catv_numbers(line,v)){found=-1;break;}
  unsigned on=v[0],ts=v[1],sv=v[2],freq=v[3],mode=v[4],slot=v[5];
  if(on<1||on>65535||ts<1||ts>65535||sv<1||sv>65535||freq<90000||freq>770000||mode>2||slot<1||slot>15){found=-1;break;}
  if(on==onid&&ts==tsid&&sv==sid){
   if(found){found=-1;break;}
   channel->frequency=freq;channel->mode=mode;channel->slot=slot;channel->enabled=1;found=1;
  }
 }
 if(ferror(f))found=-1;fclose(f);if(found<0)memset(channel,0,sizeof *channel);return found;
}
#endif
