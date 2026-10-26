// Included after resolve(); the public port accepts only authenticated, bounded commands.
#include "tsd_psi.h"
#include <ctype.h>
#include <time.h>
static pthread_mutex_t g_control_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_tune_serial=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_tune1_serial=PTHREAD_MUTEX_INITIALIZER;
static int g_control_listening,g_control_clients;

static struct {uint16_t selected[3];} g_control;
#include "tsd_primary_cas.inc"
#include "tsd_record_primary.inc"

static void epg_wake(int tuner){pthread_mutex_t *lock=tuner?&d2_dual_lock:&d0_dual_lock;pthread_cond_t *cond=tuner?&d2_dual_cond:&d0_dual_cond;pthread_mutex_lock(lock);pthread_cond_broadcast(cond);pthread_mutex_unlock(lock);}
static void *epg_watch(void *unused){(void)unused;pthread_mutex_lock(&epg_lock);epg_slots[0].hold=epg_slots[1].hold=epg_ms()+60000;pthread_mutex_unlock(&epg_lock);for(;;){int listeners[2]={d0_dual_listener,d2_dual_listener};for(int t=0;t<2;t++){if(listeners[t]<0)continue;struct pollfd p={.fd=listeners[t],.events=POLLIN};if(poll(&p,1,0)>0&&(p.revents&POLLIN))epg_external(t);}usleep(100000);}return NULL;}
static int epg_lease_number(const char *s,uint32_t *value){if(!*s||strspn(s,"0123456789")!=strlen(s))return -1;errno=0;char *end;unsigned long v=strtoul(s,&end,10);if(errno||*end||!v)return -1;*value=(uint32_t)v;return 0;}
static int control_tune(uint16_t onid,uint16_t tsid,uint16_t sid,int *clear){
 int r=d0_dual_tune(onid,tsid,sid,clear);
 if(!r){pthread_mutex_lock(&g_control_lock);g_control.selected[0]=sid;g_control.selected[1]=tsid;g_control.selected[2]=onid;pthread_mutex_unlock(&g_control_lock);}return r;
}
static int control_authenticated(const char *supplied) {
 char token[128];
 FILE *file=fopen("/data/local/tmp/tsd_token","r");
 if(!file) return 0;
 char *line=fgets(token,sizeof(token),file); fclose(file);
 if(!line) return 0;
 token[strcspn(token,"\r\n")]=0;
 if(strlen(token)!=64 || strlen(supplied)!=64 || strspn(token,"0123456789abcdefABCDEF")!=64) return 0;
 unsigned different=0;
 for(int i=0;i<64;++i) different|=(unsigned char)token[i]^(unsigned char)supplied[i];
 return different==0;
}
static int control_number(const char *text,uint16_t *number) {
 if(!*text || strspn(text,"0123456789")!=strlen(text)) return -1;
 errno=0; char *end; unsigned long value=strtoul(text,&end,10);
 if(errno || *end || !value || value>65535) return -1;
 *number=(uint16_t)value; return 0;
}
static void *control_client(void *arg) {
 int fd=(int)(intptr_t)arg;
 struct timeval timeout={.tv_sec=5}; setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 char line[512]; size_t length=0; int complete=0;
 struct timespec start; clock_gettime(CLOCK_MONOTONIC,&start);
 while(length+1<sizeof(line)) {
  struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
  int remaining=5000-(int)((now.tv_sec-start.tv_sec)*1000+(now.tv_nsec-start.tv_nsec)/1000000);
  if(remaining<=0) break;
  struct pollfd pfd={.fd=fd,.events=POLLIN};
  int ready=poll(&pfd,1,remaining);
  if(ready<0 && errno==EINTR)continue;
  if(ready<=0)break;
  char c; ssize_t count=recv(fd,&c,1,0);
  if(count<0 && errno==EINTR)continue;
  if(count<=0)break;
  if(c=='\n') { complete=1;break; }
  if(!c || ((unsigned char)c<32 && c!='\r' && c!='\t'))break;
  line[length++]=c;
 }
 line[length]=0;
 if(!complete) { out(fd,"ERR protocol\n"); goto done; }
 char *words[9],*context=NULL; int count=0;
 for(char *word=strtok_r(line," \r\t",&context);word && count<9;word=strtok_r(NULL," \r\t",&context))words[count++]=word;
 if(count<3 || strcmp(words[0],"AUTH") || !control_authenticated(words[1])) { out(fd,"ERR authentication\n"); goto done; }
 if(count==4 && !strcmp(words[2],"SIGNAL") && (!strcmp(words[3],"0")||!strcmp(words[3],"1"))) {
  control_signal(fd,words[3][0]-'0');
 } else if(count==3 && !strcmp(words[2],"STATUS")) {
  pthread_mutex_lock(&d0_dual_lock);
  int running=d0_dual_state.running,error=d0_dual_state.error;
  unsigned long long bytes=d0_dual_state.bytes,sent=d0_dual_state.sent;
  unsigned sid=d0_dual_state.sid,tsid=d0_dual_state.tsid;
  pthread_mutex_unlock(&d0_dual_lock);
  pthread_mutex_lock(&d2_dual_lock);
  int second_running=d2_dual_state.running,second_error=d2_dual_state.error;unsigned second_tsid=d2_dual_state.tsid,second_sid=d2_dual_state.sid;
  unsigned long long second_bytes=d2_dual_state.bytes,second_sent=d2_dual_state.sent;
  unsigned second_ecm=d2_dual_state.ecm_errors,second_sync=d2_dual_state.sync_errors,second_cc=d2_dual_state.cc_errors,second_tei=d2_dual_state.tei,second_frequency=d2_dual_state.frequency;int second_ready=d2_dual_state.ready;
  pthread_mutex_unlock(&d2_dual_lock);
  out(fd,"OK protocol=2 tuners=%d running=%d tsid=%u service=%u bytes=%llu sent=%llu error=%d tuner1_running=%d tuner1_tsid=%u tuner1_service=%u tuner1_bytes=%llu tuner1_sent=%llu tuner1_error=%d tuner1_ready=%d tuner1_frequency=%u tuner1_ecm_errors=%u tuner1_sync_errors=%u tuner1_cc_errors=%u tuner1_tei=%u epg_priority=1\n",d2_dual_listener>=0?2:1,running,tsid,sid,bytes,sent,error,second_running,second_tsid,second_sid,second_bytes,second_sent,second_error,second_ready,second_frequency,second_ecm,second_sync,second_cc,second_tei);
 } else if((count==5||count==8)&&(!strcmp(words[2],"EPG_CLAIM")||!strcmp(words[2],"EPG_POLL")||!strcmp(words[2],"EPG_RELEASE")||!strcmp(words[2],"EPG_TUNE"))&&(!strcmp(words[3],"0")||!strcmp(words[3],"1"))){
  int tuner=words[3][0]-'0';uint32_t lease;
  if(epg_lease_number(words[4],&lease)){out(fd,"ERR parameters\n");goto done;}
  if(count==5&&!strcmp(words[2],"EPG_CLAIM")){struct pollfd pending={.fd=tuner?d2_dual_listener:d0_dual_listener,.events=POLLIN};int queued=poll(&pending,1,0)>0&&(pending.revents&POLLIN);out(fd,!queued&&epg_claim(tuner,lease)?"OK claimed\n":"ERR busy\n");}
  else if(count==5&&!strcmp(words[2],"EPG_POLL"))out(fd,epg_owned(tuner,lease,1)?"OK owned\n":"ERR preempted\n");
  else if(count==5&&!strcmp(words[2],"EPG_RELEASE")){epg_release(tuner,lease);out(fd,"OK released\n");}
  else if(count==8&&!strcmp(words[2],"EPG_TUNE")){
   uint16_t onid,tsid,sid;if(control_number(words[5],&onid)||control_number(words[6],&tsid)||control_number(words[7],&sid)){out(fd,"ERR parameters\n");goto done;}
   pthread_mutex_t *serial=tuner?&g_tune1_serial:&g_tune_serial;if(pthread_mutex_trylock(serial)){out(fd,"ERR busy\n");goto done;}
   if(!epg_owned(tuner,lease,1)){pthread_mutex_unlock(serial);out(fd,"ERR preempted\n");goto done;}
   __atomic_store_n(&epg_tuning[tuner],1,__ATOMIC_SEQ_CST);int clear=0,result=tuner?d2_dual_tune(onid,tsid,sid,&clear):control_tune(onid,tsid,sid,&clear);__atomic_store_n(&epg_tuning[tuner],0,__ATOMIC_SEQ_CST);
   if(result)out(fd,"ERR tune=%d\n",result);else out(fd,"OK epg_tuned\n");pthread_mutex_unlock(serial);
  }else out(fd,"ERR command\n"); } else if(count==7 && !strcmp(words[2],"TUNE") && (!strcmp(words[3],"0") || !strcmp(words[3],"1"))) {
  uint16_t onid,tsid,sid;
  if(control_number(words[4],&onid) || control_number(words[5],&tsid) || control_number(words[6],&sid)) { out(fd,"ERR parameters\n");goto done; }
  int tuner=words[3][0]-'0';epg_external(tuner);
  pthread_mutex_t *tune_lock=!strcmp(words[3],"1")?&g_tune1_serial:&g_tune_serial;
  int locked=pthread_mutex_trylock(tune_lock);for(int retry=0;locked&&retry<60;retry++){usleep(50000);locked=pthread_mutex_trylock(tune_lock);}if(locked){out(fd,"ERR busy\n");goto done;}
  pthread_mutex_lock(&g_pvr_lock); int running=g_pvrstream_started; pthread_mutex_unlock(&g_pvr_lock);
  int clear=0,result=running ? (!strcmp(words[3],"1")?d2_dual_tune(onid,tsid,sid,&clear):control_tune(onid,tsid,sid,&clear)) : 4;
  if(result) out(fd,"ERR tune=%d\n",result);
  else out(fd,"OK tsid=%u service=%u av_clear=%d\n",tsid,sid,clear);
  pthread_mutex_unlock(tune_lock);
 } else if(count==4 && !strcmp(words[2],"STOP") && !strcmp(words[3],"1")) {
  epg_external(1);if(pthread_mutex_trylock(&g_tune1_serial)){out(fd,"ERR busy\n");goto done;}
  d2_dual_stop();unlink("/data/local/tmp/tsd_last_channel_1");out(fd,"OK stopped=1\n");pthread_mutex_unlock(&g_tune1_serial);
 } else out(fd,"ERR command\n");
 done:
 close(fd);
 pthread_mutex_lock(&g_control_lock); --g_control_clients; pthread_mutex_unlock(&g_control_lock);
 return NULL;
}
static void *control_restore_second(void *unused) {
 (void)unused;FILE *f=fopen("/data/local/tmp/tsd_last_channel_1","r");if(!f)return NULL;
 unsigned onid,tsid,sid;int count=fscanf(f,"%u %u %u",&onid,&tsid,&sid);fclose(f);
 if(count!=3||!onid||!tsid||!sid||onid>65535||tsid>65535||sid>65535)return NULL;
 int running=0;
 for(int i=0;i<1800;i++){pthread_mutex_lock(&d0_dual_lock);running=d0_dual_state.ready;pthread_mutex_unlock(&d0_dual_lock);if(running)break;usleep(100000);}
 if(!running)return NULL;sleep(2);
 for(int i=0;i<3;i++){int clear=0;pthread_mutex_lock(&g_tune1_serial);int result=d2_dual_tune((uint16_t)onid,(uint16_t)tsid,(uint16_t)sid,&clear);pthread_mutex_unlock(&g_tune1_serial);if(!result)return NULL;LOG("tuner1 boot restore result=%d",result);sleep(3);}
 return NULL;
}
static void *control_server(void *unused) {
 (void)unused;
 if(d0_dual_stream_start())LOG("tuner0 stream listener unavailable");else g_pvrstream_started=1;
 if(d2_dual_stream_start())LOG("tuner1 stream listener unavailable");
 pthread_t restore; if(!pthread_create(&restore,NULL,control_restore_second,NULL))pthread_detach(restore);
 pthread_t epg_thread;if(!pthread_create(&epg_thread,NULL,epg_watch,NULL))pthread_detach(epg_thread);
 int listener=socket(AF_INET,SOCK_STREAM,0),one=1;
 if(listener<0)return NULL;
 setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(40557),.sin_addr.s_addr=htonl(INADDR_ANY)};
 if(bind(listener,(void *)&address,sizeof address) || listen(listener,4)) { close(listener);return NULL; }
 pthread_mutex_lock(&g_control_lock); g_control_listening=1; pthread_mutex_unlock(&g_control_lock);
 LOG("authenticated control listening on 40557");
 for(;;) {
  int fd=accept(listener,NULL,NULL);
  if(fd<0) { if(errno!=EINTR)usleep(10000);continue; }
  pthread_mutex_lock(&g_control_lock);
  if(g_control_clients>=4) { pthread_mutex_unlock(&g_control_lock); close(fd);continue; }
  ++g_control_clients; pthread_mutex_unlock(&g_control_lock);
  pthread_t worker;
  int result=pthread_create(&worker,NULL,control_client,(void *)(intptr_t)fd);
  if(result) { close(fd); pthread_mutex_lock(&g_control_lock); --g_control_clients;pthread_mutex_unlock(&g_control_lock); }
  else pthread_detach(worker);
 }
 return NULL;
}
