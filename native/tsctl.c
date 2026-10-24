#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
int main(int argc,char **argv) {
 if(argc<2) { fprintf(stderr,"Usage: tsctl <command> [command ...]\n"); return 2; }
 int status_mode=argc==2&&!strcmp(argv[1],"--status");
 int fd=socket(AF_INET,SOCK_STREAM,0);
 struct timeval timeout={.tv_sec=5};
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
 setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
 struct sockaddr_in sa={.sin_family=AF_INET,.sin_port=htons(status_mode?40557:40555),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 if(connect(fd,(void *)&sa,sizeof sa)) { perror("tsctl connect"); close(fd); return 1; }
 char token[128];FILE *key=fopen("/data/local/tmp/tsd_token","r");
 if(!key) {fprintf(stderr,"Device key is unavailable; run as root after setup.\n");close(fd);return 1;}
 char *keyline=fgets(token,sizeof token,key);fclose(key);
 if(!keyline) {close(fd);return 1;}
 token[strcspn(token,"\r\n")]=0;
 if(strlen(token)!=64 || strspn(token,"0123456789abcdefABCDEF")!=64) {close(fd);return 1;}
 char auth[80];int authlen=snprintf(auth,sizeof auth,status_mode?"AUTH %s STATUS\n":"AUTH %s\n",token);memset(token,0,sizeof token);
 size_t authsent=0;
 while(authsent<(size_t)authlen) {ssize_t n=send(fd,auth+authsent,(size_t)authlen-authsent,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);return 1;}authsent+=(size_t)n;}
 memset(auth,0,sizeof auth);
 if(status_mode) {
  char response[4096];size_t used=0;
  while(used<sizeof response-1){char ch;ssize_t n=recv(fd,&ch,1,0);if(n<0&&errno==EINTR)continue;if(n<=0)break;response[used++]=ch;if(ch=='\n')break;}
  response[used]=0;fputs(response,stdout);close(fd);return used&&response[used-1]=='\n'&&!strncmp(response,"OK ",3)?0:1;
 }
 char ack[64];size_t acklen=0;
 while(acklen<sizeof ack-1) {char ch;ssize_t n=recv(fd,&ch,1,0);if(n<0&&errno==EINTR)continue;if(n<=0)break;ack[acklen++]=ch;if(ch=='\n')break;}
 ack[acklen]=0;if(strcmp(ack,"OK\n")) {fprintf(stderr,"Console authentication failed.\n");close(fd);return 1;}
 for(int arg=1;arg<argc;++arg) {
  size_t len=strlen(argv[arg]);
  if(len>1000 || strchr(argv[arg],'\n')) { close(fd); return 2; }
  char line[1024]; memcpy(line,argv[arg],len); line[len++]='\n';
  size_t sent=0;
  while(sent<len) { ssize_t n=send(fd,line+sent,len-sent,MSG_NOSIGNAL); if(n<0 && errno==EINTR)continue; if(n<=0) { perror("tsctl send");close(fd);return 1; } sent+=(size_t)n; }
  int gotline=0;
  for(size_t count=0;count<8192;++count) {
   char c; ssize_t n=recv(fd,&c,1,0);
   if(n<0 && errno==EINTR) { --count;continue; }
   if(n<=0) { perror("tsctl recv");close(fd);return 1; }
   putchar(c);
   if(c=='\n') { gotline=1;break; }
  }
  fflush(stdout);
  if(!gotline) { close(fd);return 1; }
 }
 close(fd);return 0;
}
