#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "../tsd_epg.inc"
static int wakeups[2];
static void epg_wake(int tuner){wakeups[tuner]++;}
#define CHECK(test) do{if(!(test)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#test);return 1;}}while(0)
int main(void){
 CHECK(epg_claim(0,11));CHECK(epg_owned(0,11,1));CHECK(!epg_claim(0,12));CHECK(epg_claim(1,21));
 __atomic_store_n(&epg_tuning[0],1,__ATOMIC_SEQ_CST);CHECK(!epg_cancelled(0));epg_viewer(0,1);CHECK(!epg_owned(0,11,0));CHECK(epg_cancelled(0));CHECK(epg_owned(1,21,0));CHECK(!epg_claim(0,12));
 epg_viewer(0,0);CHECK(!epg_claim(0,12));epg_slots[0].hold=0;CHECK(epg_claim(0,12));CHECK(!epg_cancelled(0));epg_release(0,11);CHECK(epg_owned(0,12,0));epg_release(0,12);CHECK(!epg_owned(0,12,0));CHECK(epg_cancelled(0));CHECK(wakeups[0]>=2);
 CHECK(epg_claim(0,13));epg_slots[0].expires=0;CHECK(!epg_owned(0,13,0));CHECK(epg_claim(0,14));epg_external(0);CHECK(!epg_owned(0,14,0));CHECK(!epg_claim(0,15));
 int receiver=socket(AF_INET,SOCK_DGRAM,0);CHECK(receiver>=0);struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(40561),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};CHECK(bind(receiver,(void *)&addr,sizeof addr)==0);struct timeval timeout={.tv_sec=1};setsockopt(receiver,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);uint8_t ts[376];memset(ts,255,sizeof ts);ts[0]=ts[188]=0x47;ts[1]=ts[189]=0;ts[2]=18;ts[190]=0;epg_emit(0,ts,sizeof ts);uint8_t packet[188];CHECK(recv(receiver,packet,sizeof packet,0)==188);CHECK(packet[2]==18);CHECK(recv(receiver,packet,sizeof packet,0)<0);close(receiver);
 puts("PASS: viewer preemption, tuner independence, cooldown, stale lease rejection, expiry, cancellation, EIT-only loopback tap");return 0;
}
