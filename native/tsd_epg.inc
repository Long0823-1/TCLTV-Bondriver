// Low-priority EPG leases. Normal viewer sockets remain independent of EIT taps.
#include <time.h>
static pthread_mutex_t epg_lock=PTHREAD_MUTEX_INITIALIZER;
static struct {uint32_t lease;uint64_t expires,hold;int viewer;} epg_slots[2];
static int epg_tuning[2],epg_cancel[2],epg_scan_channel[2];
static void epg_wake(int tuner);
static uint64_t epg_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void epg_external(int tuner){pthread_mutex_lock(&epg_lock);epg_slots[tuner].lease=0;epg_slots[tuner].hold=epg_ms()+30000;__atomic_store_n(&epg_cancel[tuner],1,__ATOMIC_SEQ_CST);pthread_mutex_unlock(&epg_lock);epg_wake(tuner);}
static void epg_viewer(int tuner,int connected){if(connected)epg_external(tuner);pthread_mutex_lock(&epg_lock);epg_slots[tuner].viewer=connected;if(!connected)epg_slots[tuner].hold=epg_ms()+30000;pthread_mutex_unlock(&epg_lock);}
static int epg_owned(int tuner,uint32_t lease,int renew){pthread_mutex_lock(&epg_lock);int ok=lease&&epg_slots[tuner].lease==lease&&epg_slots[tuner].expires>epg_ms()&&!epg_slots[tuner].viewer;if(ok&&renew)epg_slots[tuner].expires=epg_ms()+90000;pthread_mutex_unlock(&epg_lock);return ok;}
static int epg_claim(int tuner,uint32_t lease){pthread_mutex_lock(&epg_lock);uint64_t now=epg_ms();int ok=lease&&!epg_slots[tuner].viewer&&epg_slots[tuner].hold<=now&&(!epg_slots[tuner].lease||epg_slots[tuner].expires<=now);if(ok){epg_slots[tuner].lease=lease;epg_slots[tuner].expires=now+90000;__atomic_store_n(&epg_cancel[tuner],0,__ATOMIC_SEQ_CST);}pthread_mutex_unlock(&epg_lock);return ok;}
static void epg_release(int tuner,uint32_t lease){pthread_mutex_lock(&epg_lock);int changed=epg_slots[tuner].lease==lease;if(changed){epg_slots[tuner].lease=0;__atomic_store_n(&epg_cancel[tuner],1,__ATOMIC_SEQ_CST);}pthread_mutex_unlock(&epg_lock);if(changed)epg_wake(tuner);}
static int epg_background(int tuner){return __atomic_load_n(&epg_tuning[tuner],__ATOMIC_SEQ_CST);}
static int epg_cancelled(int tuner){return epg_background(tuner)&&__atomic_load_n(&epg_cancel[tuner],__ATOMIC_SEQ_CST);}
// One sender per capture thread. Only PID 0x0012 goes to the loopback receiver.
static void epg_emit(int tuner,const void *data,size_t length){static int sockets[2]={-1,-1};int fd=sockets[tuner];if(fd<0){fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return;fcntl(fd,F_SETFL,O_NONBLOCK);sockets[tuner]=fd;}struct sockaddr_in to={.sin_family=AF_INET,.sin_port=htons((uint16_t)(40561+tuner)),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};const uint8_t *p=data;for(size_t i=0;i+188<=length;i+=188)if(p[i]==0x47&&(p[i+1]&31)==0&&p[i+2]==18)sendto(fd,p+i,188,MSG_DONTWAIT|MSG_NOSIGNAL,(void *)&to,sizeof to);}
