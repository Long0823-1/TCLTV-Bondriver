#include "tsd_psi.h"
#include <string.h>
uint32_t tsd_crc32(const uint8_t *data,size_t length) {
 uint32_t crc=0xffffffff;
 for(size_t i=0;i<length;++i) {
  crc^=(uint32_t)data[i]<<24;
  for(int bit=0;bit<8;++bit) crc=(crc&0x80000000) ? (crc<<1)^0x04c11db7 : crc<<1;
 }
 return crc;
}
static unsigned u16(const uint8_t *data) { return ((unsigned)data[0]<<8)|data[1]; }
void tsd_psi_reset(struct tsd_psi *state,uint16_t pid) {
 memset(state,0,sizeof(*state)); state->pid=pid; state->last_cc=-1;
}
static void append(struct tsd_psi *state,const uint8_t *data,size_t length,int allow_new,tsd_section_cb cb,void *context) {
 while(length) {
  if(!state->used && (!allow_new || *data==0xff)) return;
  state->data[state->used++]=*data++; --length;
  if(state->used==3) {
   state->expected=3+(u16(state->data+1)&0x0fff);
   if(state->expected<8 || state->expected>TSD_PSI_MAX || !(state->data[1]&0x80)) { state->used=state->expected=0; return; }
  }
  if(state->expected && state->used==state->expected) {
   if(tsd_crc32(state->data,state->used)==0) cb(context,state->data,state->used);
   state->used=state->expected=0;
   if(!allow_new) return;
  }
 }
}
void tsd_psi_feed(struct tsd_psi *state,const uint8_t packet[188],tsd_section_cb cb,void *context) {
 if(packet[0]!=0x47 || (u16(packet+1)&0x1fff)!=state->pid) return;
 if((packet[1]&0x80) || (packet[3]&0xc0)) { state->used=state->expected=0; state->last_cc=-1; return; }
 unsigned afc=(packet[3]>>4)&3, offset=4;
 if(!afc) { state->used=state->expected=0; state->last_cc=-1; return; }
 if(afc&2) {
  if(packet[4]>183 || 5u+packet[4]>188) { state->used=state->expected=0; state->last_cc=-1; return; }
  if(packet[4] && (packet[5]&0x80)) { state->used=state->expected=0; state->last_cc=-1; }
  offset+=1+packet[4];
 }
 if(!(afc&1) || offset>=188) return;
 int cc=packet[3]&15;
 if(state->last_cc>=0) {
  if(cc==state->last_cc) return;
  if(cc!=((state->last_cc+1)&15)) state->used=state->expected=0;
 }
 state->last_cc=cc;
 if(packet[1]&0x40) {
  unsigned pointer=packet[offset++];
  if(pointer>188-offset) { state->used=state->expected=0; return; }
  if(state->used && pointer) append(state,packet+offset,pointer,0,cb,context);
  state->used=state->expected=0;
  offset+=pointer;
  append(state,packet+offset,188-offset,1,cb,context);
 } else if(state->used) append(state,packet+offset,188-offset,0,cb,context);
}
static int valid(const uint8_t *section,size_t length,uint8_t table) {
 return length>=12 && section[0]==table && (section[1]&0x80) && (section[5]&1) &&
        3+(u16(section+1)&0x0fff)==length && tsd_crc32(section,length)==0;
}
int tsd_pat_pmt(const uint8_t *section,size_t length,uint16_t tsid,uint16_t service,uint16_t *pmt) {
 if(!valid(section,length,0) || u16(section+3)!=tsid || (length-12)%4) return -1;
 for(size_t i=8;i+4<=length-4;i+=4) if(u16(section+i)==service) {
  uint16_t pid=(uint16_t)(u16(section+i+2)&0x1fff);
  if(!pid || pid==0x1fff) return -1;
  *pmt=pid; return 0;
 }
 return -1;
}
int tsd_pid_add(struct tsd_service_pids *result,unsigned pid) {
 if(pid>=0x1fff) return 0;
 for(unsigned i=0;i<result->count;++i) if(result->pids[i]==pid) return 0;
 if(result->count>=TSD_PID_MAX) return -1;
 result->pids[result->count++]=(uint16_t)pid; return 0;
}
static int descriptors(struct tsd_service_pids *result,const uint8_t *data,size_t length) {
 size_t offset=0;
 while(offset<length) {
  if(offset+2>length) return -1;
  unsigned tag=data[offset], size=data[offset+1];
  if(offset+2+size>length) return -1;
  if(tag==9 && size>=4) {
   unsigned pid=u16(data+offset+4)&0x1fff;
   if(result->ecm==0xffff && pid!=0x1fff) result->ecm=(uint16_t)pid;
   if(tsd_pid_add(result,pid)) return -1;
  }
  offset+=2+size;
 }
 return 0;
}
int tsd_pmt_pids(const uint8_t *section,size_t length,uint16_t service,struct tsd_service_pids *result) {
 if(!valid(section,length,2) || length<16 || u16(section+3)!=service) return -1;
 memset(result,0,sizeof(*result)); result->video=result->audio=result->ecm=0xffff;
 static const uint16_t tables[]={0,1,0x10,0x11,0x12,0x14,0x24,0x26,0x27};
 for(unsigned i=0;i<sizeof(tables)/sizeof(tables[0]);++i) if(tsd_pid_add(result,tables[i])) return -1;
 result->pcr=(uint16_t)(u16(section+8)&0x1fff);
 if(tsd_pid_add(result,result->pcr)) return -1;
 size_t info_length=u16(section+10)&0x0fff, offset=12+info_length;
 if(offset>length-4 || descriptors(result,section+12,info_length)) return -1;
 while(offset<length-4) {
  if(offset+5>length-4) return -1;
  unsigned type=section[offset], pid=u16(section+offset+1)&0x1fff, es_length=u16(section+offset+3)&0x0fff;
  if(offset+5+es_length>length-4 || tsd_pid_add(result,pid) || descriptors(result,section+offset+5,es_length)) return -1;
  if(result->video==0xffff && (type==2 || type==0x1b || type==0x24)) result->video=(uint16_t)pid;
  if(result->audio==0xffff && (type==3 || type==4 || type==0xf || type==0x11 || type==0x81 || type==0x87)) result->audio=(uint16_t)pid;
  offset+=5+es_length;
 }
 return offset==length-4 ? 0 : -1;
}
