#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../tsd_psi.h"
#include "../tsd_mux.inc"
static void finish(uint8_t *b,size_t n){unsigned len=(unsigned)n-3;b[1]=(uint8_t)(0xb0|(len>>8));b[2]=(uint8_t)len;uint32_t crc=tsd_crc32(b,n-4);b[n-4]=(uint8_t)(crc>>24);b[n-3]=(uint8_t)(crc>>16);b[n-2]=(uint8_t)(crc>>8);b[n-1]=(uint8_t)crc;}
static void feed(unsigned pid,const uint8_t *b,size_t n){uint8_t packet[188];memset(packet,255,sizeof packet);packet[0]=0x47;packet[1]=(uint8_t)(0x40|(pid>>8));packet[2]=(uint8_t)pid;packet[3]=0x10;packet[4]=0;memcpy(packet+5,b,n);tsd_mux_feed(&d2_mux,packet);}
int main(void){
 uint8_t pat[24]={0,0,0,0x7f,0xe0,0xc1,0,0,4,0,0xe1,0xf0,4,1,0xe3,0xf0,4,2,0xe4,0xf0};finish(pat,sizeof pat);
 tsd_mux_reset(&d2_mux,32736);feed(0,pat,sizeof pat);assert(d2_mux.count==3&&!tsd_mux_ready(&d2_mux));
 for(unsigned i=0;i<3;i++){uint8_t pmt[21]={2,0,0,4,(uint8_t)i,0xc1,0,0,0xe1,0,0xf0,0,2,0xe1,0,0xf0,0};finish(pmt,sizeof pmt);feed(i==0?496:(i==1?1008:1264),pmt,sizeof pmt);assert(tsd_mux_ready(&d2_mux)==(i==2));}
 struct tsd_service_pids out;assert(!tsd_mux_union(&d2_mux,&out));assert(out.count==13);
 unsigned pmts=0;for(unsigned i=0;i<out.count;i++)if(out.pids[i]==496||out.pids[i]==1008||out.pids[i]==1264)pmts++;assert(pmts==3);
 tsd_mux_reset(&d2_mux,32736);pat[7]=1;finish(pat,sizeof pat);feed(0,pat,sizeof pat);assert(d2_mux.error&&!tsd_mux_ready(&d2_mux));
 tsd_mux_reset(&d2_mux,32736);d2_mux.count=2;for(unsigned i=0;i<2;i++){d2_mux.programs[i].pid=(uint16_t)(500+i);d2_mux.programs[i].service.count=63;for(unsigned j=0;j<63;j++)d2_mux.programs[i].service.pids[j]=(uint16_t)(100+i*100+j);}assert(tsd_mux_union(&d2_mux,&out)==-1);
 struct tsd_mux_program program;memset(&program,0,sizeof program);program.sid=1024;
 uint8_t ca[38]={2,0,0,4,0,0xc1,0,0,0xe1,0,0xf0,6,9,4,0,5,0xe0,49,6,0xe1,0x15,0xf0,6,9,4,0,5,0xff,0xff,2,0xe1,0x11,0xf0,0};
 finish(ca,sizeof ca);tsd_mux_pmt(&program,ca,sizeof ca);assert(program.ready&&tsd_mux_pid_clear(&program,277)&&!tsd_mux_pid_clear(&program,273));
 assert(program.service.count>0);int kept=0;for(unsigned i=0;i<program.service.count;i++)if(program.service.pids[i]==277)kept=1;assert(kept);
 ca[27]=0xe0;ca[28]=49;finish(ca,sizeof ca);tsd_mux_pmt(&program,ca,sizeof ca);assert(!tsd_mux_pid_clear(&program,277));
 tsd_mux_reset(&d2_mux,32736);d2_mux.count=1;d2_mux.programs[0].pid=500;d2_mux.programs[0].service.video=d2_mux.programs[0].service.audio=0xffff;d2_mux.programs[0].service.count=1;d2_mux.programs[0].service.pids[0]=1000;
 assert(!tsd_mux_union(&d2_mux,&out)&&out.count==1&&out.pids[0]==500);
 puts("mux: all PMTs, missing PMT, deduplication, section limit, PID limit passed");return 0;
}
