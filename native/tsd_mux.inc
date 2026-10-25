// Discover all PAT programs while PVR-all is enabled, then record their union
// through PID filters so the PVR descrambler stays in the data path.
#define TSD_MUX_PROGRAMS 32
struct tsd_mux_program {uint16_t sid,pid;int ready;struct tsd_psi parser;struct tsd_service_pids service;uint16_t clear_pids[TSD_PID_MAX];unsigned clear_count;};
struct tsd_mux {uint16_t tsid;unsigned count;int pat,error;struct tsd_psi parser;struct tsd_mux_program programs[TSD_MUX_PROGRAMS];};
static struct tsd_mux d2_mux;
// An ES-level CA descriptor with the null ECM PID overrides program-level CA.
// Keep such ES in the TS union, but do not spend a descrambler slot on it.
static int tsd_mux_pid_clear(const struct tsd_mux_program *p,uint16_t pid){for(unsigned i=0;i<p->clear_count;i++)if(p->clear_pids[i]==pid)return 1;return 0;}
static void tsd_mux_pmt(void *context,const uint8_t *s,size_t n) {
 struct tsd_mux_program *p=context;if(tsd_pmt_pids(s,n,p->sid,&p->service))return;
 p->clear_count=0;
 for(size_t off=12+(((unsigned)s[10]&15)<<8)+s[11];off+5<=n-4;){
  unsigned pid=(((unsigned)s[off+1]&31)<<8)|s[off+2],length=(((unsigned)s[off+3]&15)<<8)|s[off+4];int has_ca=0,has_ecm=0;
  for(size_t d=off+5;d+2<=off+5+length;d+=2+s[d+1])if(s[d]==9&&s[d+1]>=4){has_ca=1;if(((((unsigned)s[d+4]&31)<<8)|s[d+5])!=0x1fff)has_ecm=1;}
  if(has_ca&&!has_ecm&&p->clear_count<TSD_PID_MAX)p->clear_pids[p->clear_count++]=(uint16_t)pid;
  off+=5+length;
 }
 p->ready=1;
}
static void tsd_mux_pat(void *context,const uint8_t *s,size_t n) {
 struct tsd_mux *m=context;
 if(n<12||(n-12)%4||!(s[1]&0x80)||s[0]||!(s[5]&1)||tsd_crc32(s,n)||((s[3]<<8)|s[4])!=m->tsid)return;
 // This firmware's terrestrial PAT fits one section; fail instead of truncating.
 if(s[6]||s[7]){m->error=1;return;}if(m->pat)return;
 for(size_t i=8;i+4<=n-4;i+=4){unsigned sid=(s[i]<<8)|s[i+1],pid=((s[i+2]&31)<<8)|s[i+3];if(!sid)continue;
  if(m->count==TSD_MUX_PROGRAMS||!pid||pid==8191){m->error=1;return;}
  struct tsd_mux_program *p=&m->programs[m->count++];p->sid=(uint16_t)sid;p->pid=(uint16_t)pid;tsd_psi_reset(&p->parser,(uint16_t)pid);
 }m->pat=1;
}
static void tsd_mux_reset(struct tsd_mux *m,uint16_t tsid){memset(m,0,sizeof(*m));m->tsid=tsid;tsd_psi_reset(&m->parser,0);}
static void tsd_mux_feed(struct tsd_mux *m,const uint8_t *p){tsd_psi_feed(&m->parser,p,tsd_mux_pat,m);for(unsigned i=0;i<m->count;i++)if(!m->programs[i].ready)tsd_psi_feed(&m->programs[i].parser,p,tsd_mux_pmt,&m->programs[i]);}
static int tsd_mux_ready(const struct tsd_mux *m){if(!m->pat||!m->count||m->error)return 0;for(unsigned i=0;i<m->count;i++)if(!m->programs[i].ready)return 0;return 1;}
static int tsd_mux_union(const struct tsd_mux *m,struct tsd_service_pids *out){memset(out,0,sizeof(*out));for(unsigned i=0;i<m->count;i++){const struct tsd_mux_program *p=&m->programs[i];if(tsd_pid_add(out,p->pid))return -1;if(p->service.video==0xffff&&p->service.audio==0xffff)continue;for(unsigned j=0;j<p->service.count;j++)if(tsd_pid_add(out,p->service.pids[j]))return -1;}return 0;}
