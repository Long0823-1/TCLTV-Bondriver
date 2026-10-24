// Included after resolve(); the public port accepts only authenticated, bounded commands.
#include "tsd_psi.h"
#include <ctype.h>
#include <time.h>
static pthread_mutex_t g_control_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_tune_serial=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_tune1_serial=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_control_cond=PTHREAD_COND_INITIALIZER;
static uint32_t g_pvr_generation;
static int g_control_listening, g_control_clients;
static struct {
 int phase;
 uint32_t generation;
 uint16_t tsid, sid, onid, pmt;
 struct tsd_psi pat_parser, pmt_parser;
 struct tsd_service_pids service;
 int video_clear,audio_clear;
 uint16_t selected[3];
 struct tsd_service_pids previous;
} g_control;

enum { WAIT_PAT=1,HAVE_PAT,WAIT_PMT,HAVE_PMT,WAIT_AV,HAVE_AV };
static int pvr_configure(const uint16_t *pids,size_t count,uint32_t *generation) {
 if(!count || count>63) return 8;
 for(size_t i=0;i<count;++i) if(pids[i]>0x1fff) return 8;
 int (*stop_fn)(void)=resolve("mif_tsi_pvr_stop");
 int (*set_fn)(const uint16_t *,uint32_t)=resolve("mif_tsi_pvr_set_filters");
 int (*start_fn)(void)=resolve("mif_tsi_pvr_start");
 if(!stop_fn || !set_fn || !start_fn) return 8;
 pthread_mutex_lock(&g_pvr_lock);
 int result=stop_fn();
 if(!result) result=set_fn(pids,(uint32_t)count);
 if(!result) result=start_fn();
 ++g_pvr_generation;
 if(generation) *generation=g_pvr_generation;
 g_pvr_error=result;
 pthread_mutex_unlock(&g_pvr_lock);
 return result;
}
static void control_section(void *unused,const uint8_t *section,size_t length) {
 (void)unused;
 if(g_control.phase==WAIT_PAT && tsd_pat_pmt(section,length,g_control.tsid,g_control.sid,&g_control.pmt)==0) {
  g_control.phase=HAVE_PAT; pthread_cond_broadcast(&g_control_cond);
 } else if(g_control.phase==WAIT_PMT && tsd_pmt_pids(section,length,g_control.sid,&g_control.service)==0 &&
           tsd_pid_add(&g_control.service,g_control.pmt)==0) {
  g_control.phase=HAVE_PMT; pthread_cond_broadcast(&g_control_cond);
 }
}
static void control_feed(const uint8_t *data,size_t length,uint32_t generation) {
 pthread_mutex_lock(&g_control_lock);
 if(g_control.generation!=generation || !g_control.phase) { pthread_mutex_unlock(&g_control_lock); return; }
 for(size_t i=0;i+188<=length;i+=188) {
  const uint8_t *p=data+i;
  if(p[0]!=0x47) continue;
  if(g_control.phase==WAIT_PAT) tsd_psi_feed(&g_control.pat_parser,p,control_section,NULL);
  else if(g_control.phase==WAIT_PMT) tsd_psi_feed(&g_control.pmt_parser,p,control_section,NULL);
  else if(g_control.phase==WAIT_AV && !(p[1]&0x80) && !(p[3]&0xc0) && (p[1]&0x40)) {
   unsigned pid=(((unsigned)p[1]&31)<<8)|p[2], afc=(p[3]>>4)&3;
   size_t offset=4+((afc&2)?1+p[4]:0);
   if(!(afc&1) || offset+4>188 || p[offset] || p[offset+1] || p[offset+2]!=1) continue;
   if(pid==g_control.service.video) g_control.video_clear=1;
   if(pid==g_control.service.audio) g_control.audio_clear=1;
   if(g_control.video_clear && g_control.audio_clear) { g_control.phase=HAVE_AV; pthread_cond_broadcast(&g_control_cond); }
  }
 }
 pthread_mutex_unlock(&g_control_lock);
}
static int control_wait(int phase,int seconds) {
 struct timespec deadline; clock_gettime(CLOCK_REALTIME,&deadline); deadline.tv_sec+=seconds;
 pthread_mutex_lock(&g_control_lock);
 int result=0;
 while(g_control.phase<phase && !result) result=pthread_cond_timedwait(&g_control_cond,&g_control_lock,&deadline);
 int ready=g_control.phase>=phase;
 pthread_mutex_unlock(&g_control_lock);
 return ready ? 0 : -1;
}
static void control_save(const uint16_t tuple[3],const struct tsd_service_pids *service) {
 const char *temporary="/data/local/tmp/tsd_last_channel.new";
 FILE *file=fopen(temporary,"w");
 if(!file) return;
 for(int i=0;i<3;++i) fprintf(file,"%02x%02x",tuple[i]&255,tuple[i]>>8);
 fputc('\n',file);
 for(unsigned i=0;i<service->count;++i) fprintf(file,"%02x%02x",service->pids[i]&255,service->pids[i]>>8);
 fputc('\n',file);
 int failed=ferror(file); if(fclose(file))failed=1;
 if(!failed) rename(temporary,"/data/local/tmp/tsd_last_channel"); else unlink(temporary);
}
static int control_tune(uint16_t onid,uint16_t tsid,uint16_t sid,int *av_clear) {
 int (*lookup)(void *,const uint16_t *)=resolve("db_dtv_get_prog_info_by_triplet");
 int (*play)(const uint16_t *)=resolve("tvhdtv_play_program_by_triplet");
 if(!lookup || !play) return 8;
 uint16_t tuple[3]={sid,tsid,onid}, old_tuple[3];
 struct tsd_service_pids old_service;
 void *program=calloc(1,0x10000);
 if(!program) return 3;
 int exists=lookup(program,tuple); free(program);
 if(exists!=1) return 9;
 pthread_mutex_lock(&g_control_lock);
 memcpy(old_tuple,g_control.selected,sizeof(old_tuple)); old_service=g_control.previous;
 memset(&g_control.service,0,sizeof(g_control.service));
 g_control.tsid=tsid; g_control.sid=sid; g_control.onid=onid;
 g_control.phase=WAIT_PAT; g_control.generation=UINT32_MAX;
 tsd_psi_reset(&g_control.pat_parser,0);
 pthread_mutex_unlock(&g_control_lock);
 int result=play(tuple);
 if(!result) usleep(4000000); // Allow the vendor channel-change path to release/reset its recording flow.
 uint32_t generation=0;
 uint16_t pids[2]={0,0};
 if(!result) result=pvr_configure(pids,1,&generation);
 pthread_mutex_lock(&g_control_lock); g_control.generation=generation; pthread_mutex_unlock(&g_control_lock);
 if(!result && control_wait(HAVE_PAT,20)) result=20;
 if(!result) {
  pthread_mutex_lock(&g_control_lock);
  pids[1]=g_control.pmt;
  tsd_psi_reset(&g_control.pmt_parser,pids[1]);
  g_control.phase=WAIT_PMT; g_control.generation=UINT32_MAX;
  pthread_mutex_unlock(&g_control_lock);
  result=pvr_configure(pids,2,&generation);
  pthread_mutex_lock(&g_control_lock); g_control.generation=generation; pthread_mutex_unlock(&g_control_lock);
  if(!result && control_wait(HAVE_PMT,10)) result=21;
 }
 struct tsd_service_pids service;
 if(!result) {
  pthread_mutex_lock(&g_control_lock);
  service=g_control.service; g_control.phase=WAIT_AV; g_control.generation=UINT32_MAX;
  g_control.video_clear=service.video==0xffff; g_control.audio_clear=service.audio==0xffff;
  if(g_control.video_clear && g_control.audio_clear)g_control.phase=HAVE_AV;
  pthread_mutex_unlock(&g_control_lock);
  result=pvr_configure(service.pids,service.count,&generation);
  pthread_mutex_lock(&g_control_lock); g_control.generation=generation; pthread_mutex_unlock(&g_control_lock);
  if(!result) {
   *av_clear=control_wait(HAVE_AV,6)==0;
   pthread_mutex_lock(&g_control_lock);
   memcpy(g_control.selected,tuple,sizeof(tuple)); g_control.previous=service;
   pthread_mutex_unlock(&g_control_lock);
   control_save(tuple,&service);
  }
 }
 pthread_mutex_lock(&g_control_lock); g_control.phase=0; pthread_mutex_unlock(&g_control_lock);
 if(result && old_tuple[0] && old_service.count) { play(old_tuple); usleep(500000); pvr_configure(old_service.pids,old_service.count,NULL); }
 return result;
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
 char *words[8],*context=NULL; int count=0;
 for(char *word=strtok_r(line," \r\t",&context);word && count<8;word=strtok_r(NULL," \r\t",&context))words[count++]=word;
 if(count<3 || strcmp(words[0],"AUTH") || !control_authenticated(words[1])) { out(fd,"ERR authentication\n"); goto done; }
 if(count==3 && !strcmp(words[2],"STATUS")) {
  pthread_mutex_lock(&g_pvr_lock);
  int running=g_pvrstream_started, error=g_pvr_error;
  unsigned long long bytes=g_pvr_bytes,sent=g_pvr_sent;
  pthread_mutex_unlock(&g_pvr_lock);
  pthread_mutex_lock(&g_control_lock);
  unsigned sid=g_control.selected[0],tsid=g_control.selected[1];
  pthread_mutex_unlock(&g_control_lock);
  pthread_mutex_lock(&d2_dual_lock);
  int second_running=d2_dual_state.running,second_error=d2_dual_state.error;unsigned second_tsid=d2_dual_state.tsid,second_sid=d2_dual_state.sid;
  unsigned long long second_bytes=d2_dual_state.bytes,second_sent=d2_dual_state.sent;
  unsigned second_ecm=d2_dual_state.ecm_errors,second_sync=d2_dual_state.sync_errors,second_cc=d2_dual_state.cc_errors,second_tei=d2_dual_state.tei,second_frequency=d2_dual_state.frequency;int second_ready=d2_dual_state.ready;
  pthread_mutex_unlock(&d2_dual_lock);
  out(fd,"OK protocol=2 tuners=%d running=%d tsid=%u service=%u bytes=%llu sent=%llu error=%d tuner1_running=%d tuner1_tsid=%u tuner1_service=%u tuner1_bytes=%llu tuner1_sent=%llu tuner1_error=%d tuner1_ready=%d tuner1_frequency=%u tuner1_ecm_errors=%u tuner1_sync_errors=%u tuner1_cc_errors=%u tuner1_tei=%u\n",d2_dual_listener>=0?2:1,running,tsid,sid,bytes,sent,error,second_running,second_tsid,second_sid,second_bytes,second_sent,second_error,second_ready,second_frequency,second_ecm,second_sync,second_cc,second_tei);
 } else if(count==7 && !strcmp(words[2],"TUNE") && (!strcmp(words[3],"0") || !strcmp(words[3],"1"))) {
  uint16_t onid,tsid,sid;
  if(control_number(words[4],&onid) || control_number(words[5],&tsid) || control_number(words[6],&sid)) { out(fd,"ERR parameters\n");goto done; }
  pthread_mutex_t *tune_lock=!strcmp(words[3],"1")?&g_tune1_serial:&g_tune_serial;
  if(pthread_mutex_trylock(tune_lock)) { out(fd,"ERR busy\n");goto done; }
  pthread_mutex_lock(&g_pvr_lock); int running=g_pvrstream_started; pthread_mutex_unlock(&g_pvr_lock);
  int clear=0,result=running ? (!strcmp(words[3],"1")?d2_dual_tune(onid,tsid,sid,&clear):control_tune(onid,tsid,sid,&clear)) : 4;
  if(result) out(fd,"ERR tune=%d\n",result);
  else out(fd,"OK tsid=%u service=%u av_clear=%d\n",tsid,sid,clear);
  pthread_mutex_unlock(tune_lock);
 } else if(count==4 && !strcmp(words[2],"STOP") && !strcmp(words[3],"1")) {
  if(pthread_mutex_trylock(&g_tune1_serial)){out(fd,"ERR busy\n");goto done;}
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
 for(int i=0;i<1800;i++){pthread_mutex_lock(&g_pvr_lock);running=g_pvrstream_started;pthread_mutex_unlock(&g_pvr_lock);if(running)break;usleep(100000);}
 if(!running)return NULL;sleep(2);
 for(int i=0;i<3;i++){int clear=0;pthread_mutex_lock(&g_tune1_serial);int result=d2_dual_tune((uint16_t)onid,(uint16_t)tsid,(uint16_t)sid,&clear);pthread_mutex_unlock(&g_tune1_serial);if(!result)return NULL;LOG("tuner1 boot restore result=%d",result);sleep(3);}
 return NULL;
}
static void *control_server(void *unused) {
 (void)unused;
 if(d2_dual_stream_start())LOG("tuner1 stream listener unavailable");
 pthread_t restore; if(!pthread_create(&restore,NULL,control_restore_second,NULL))pthread_detach(restore);
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
