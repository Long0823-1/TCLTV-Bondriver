// libtsd.so: LD_PRELOAD probe injected into sitatvservice.
// Opens a tiny text console on 127.0.0.1:40555 to call exported symbols by name.
//   call <sym> a0..a5   (args: 123 | 0x1f | @k = scratch buffer k | s:text)
//   peek <addr> <len>   |  peekk <k> <len>  |  pokek <k> <off> <hexbytes>
//   quit
#define _GNU_SOURCE
#include <android/log.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TAG "tsd"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define NSCR 8
#define SCRSZ 0x10000

static unsigned char *scr[NSCR];
uintptr_t g_stream_addr;
int g_stream_port;
int g_tsr_patch_tsio = 1;
int g_pvrstream_port;
static pthread_mutex_t g_pvr_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_pvrstream_started;




static void out(int fd, const char *fmt, ...) {
  char b[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  if (n > 0) send(fd, b, n < (int)sizeof b ? n : (int)sizeof b - 1, MSG_NOSIGNAL);
}

// Background TS stream server: polls a fixed live-TS-buffer address for
// well-formed 188B packets and forwards them to one TCP client as they're
// found. Driven by two static globals set just before this thread starts
// (see the "tsstream" console command).
static void *stream_thread(void *arg) {
  (void)arg;
  volatile unsigned char *addr = (volatile unsigned char *)g_stream_addr;
  int port = g_stream_port;

  int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (bind(s, (void *)&sa, sizeof sa) || listen(s, 1)) {
    LOG("tsstream: bind/listen failed on port %d", port);
    return NULL;
  }
  LOG("tsstream: listening on %d, source addr=%p", port, (void *)addr);
  for (;;) {
    int c = accept(s, NULL, NULL);
    if (c < 0) continue;
    LOG("tsstream: client connected");
    unsigned char last[188];
    int have_last = 0;
    unsigned char buf[512];
    long sent = 0;
    for (;;) {
      memcpy(buf, (void *)addr, sizeof buf);
      int wrote_any = 0;
      for (int i = 0; i + 376 <= (int)sizeof buf; i++) {
        if (buf[i] == 0x47 && buf[i + 188] == 0x47) {
          if (!have_last || memcmp(last, buf + i, 188) != 0) {
            if (send(c, buf + i, 188, MSG_NOSIGNAL) != 188) goto client_gone;
            memcpy(last, buf + i, 188);
            have_last = 1;
            sent += 188;
            wrote_any = 1;
          }
          break;
        }
      }
      (void)wrote_any;
      usleep(1500);
    }
  client_gone:
    LOG("tsstream: client disconnected, sent %ld bytes", sent);
    close(c);
  }
  return NULL;
}

static void *resolve(const char *name) {
  void *f = dlsym(RTLD_DEFAULT, name);
  if (f) return f;
  static const char *libs[] = {"libfpp.so", "libmi3.so", "libisdb.so", "libdtv_mw.so", "libdtv_mw_porting.so", "libmtal.so", 0};
  for (int i = 0; libs[i]; i++) {
    void *h = dlopen(libs[i], RTLD_NOW | RTLD_NOLOAD);
    if (h && (f = dlsym(h, name))) return f;
  }
  return NULL;
}

static uintptr_t parse_arg(const char *t) {
  if (t[0] == '@') return (uintptr_t)scr[atoi(t + 1) % NSCR];
  if (t[0] == 's' && t[1] == ':') return (uintptr_t)strdup(t + 2);
  return (uintptr_t)strtoul(t, NULL, 0);
}

#include "tsd_epg.inc"
#include "tsd_dual.inc"

#include "tsd_signal.inc"
#include "tsd_control.inc"

static void handle(int fd, char *line) {
  char *tok[16];
  int n = 0;
  for (char *p = strtok(line, " \r\n"); p && n < 16; p = strtok(NULL, " \r\n")) tok[n++] = p;
  if (!n) return;
  if (!strcmp(tok[0], "call") && n >= 2) {
    void *f = resolve(tok[1]);
    if (!f) { out(fd, "nosym %s\n", tok[1]); return; }
    uintptr_t a[6] = {0};
    for (int i = 0; i < 6 && i + 2 < n; i++) a[i] = parse_arg(tok[i + 2]);
    LOG("call %s(%x,%x,%x,%x,%x,%x)", tok[1], a[0], a[1], a[2], a[3], a[4], a[5]);
    uintptr_t r = ((uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))f)(
        a[0], a[1], a[2], a[3], a[4], a[5]);
    out(fd, "ret=0x%x (%ld)\n", r, (long)r);
  } else if (!strcmp(tok[0], "load") && n >= 2) {
    void *h = dlopen(tok[1], RTLD_NOW | RTLD_GLOBAL);
    if (h) out(fd, "loaded %p\n", h); else out(fd, "dlerr %s\n", dlerror());
  } else if (!strcmp(tok[0], "sym") && n >= 2) {
    out(fd, "%p\n", resolve(tok[1]));
  } else if (!strcmp(tok[0], "peek") && n >= 3) {
    unsigned char *p = (unsigned char *)strtoul(tok[1], NULL, 0);
    int len = atoi(tok[2]);
    for (int i = 0; i < len; i++) out(fd, "%02x", p[i]);
    out(fd, "\n");
  } else if (!strcmp(tok[0], "peekk") && n >= 3) {
    int k = atoi(tok[1]) % NSCR, len = atoi(tok[2]);
    for (int i = 0; i < len && i < SCRSZ; i++) out(fd, "%02x", scr[k][i]);
    out(fd, "\n");
  } else if (!strcmp(tok[0], "pokek") && n >= 4) {
    int k = atoi(tok[1]) % NSCR, off = atoi(tok[2]);
    for (const char *h = tok[3]; h[0] && h[1]; h += 2) {
      char b[3] = {h[0], h[1], 0};
      scr[k][off++] = (unsigned char)strtoul(b, NULL, 16);
    }
    out(fd, "ok\n");
  } else if (!strcmp(tok[0], "tsgrab") && n >= 4) {
    // tsgrab <addr_hex> <n_packets> <out_k>
    // Polls a fixed live-TS-buffer address, hunting for 0x47 sync bytes 188
    // apart, and appends well-formed 188-byte packets into scr[out_k] until
    // n_packets collected (capped by scratch buffer size) or 2000 polls pass.
    volatile unsigned char *addr = (volatile unsigned char *)strtoul(tok[1], NULL, 0);
    int want = atoi(tok[2]);
    int outk = atoi(tok[3]) % NSCR;
    int cap = SCRSZ / 188;
    if (want > cap) want = cap;
    int got = 0;
    unsigned char last[188];
    int have_last = 0;
    unsigned char buf[512];
    for (int poll = 0; poll < 4000 && got < want; poll++) {
      memcpy(buf, (void *)addr, sizeof buf);
      for (int i = 0; i + 376 <= (int)sizeof buf && got < want; i++) {
        if (buf[i] == 0x47 && buf[i + 188] == 0x47) {
          if (!have_last || memcmp(last, buf + i, 188) != 0) {
            memcpy(scr[outk] + got * 188, buf + i, 188);
            memcpy(last, buf + i, 188);
            have_last = 1;
            got++;
          }
          break;
        }
      }
      usleep(2000);
    }
    out(fd, "ok got=%d\n", got);
  } else if (!strcmp(tok[0], "tsgrab2") && n >= 4) {
    // tsgrab2 <hFilterHandle_hex> <n_packets> <out_k>
    // Like tsgrab, but re-fetches MI_DMX_GetAttr(handle,4,...) fresh on every
    // poll instead of trusting one address to stay valid over time.
    void *fn = resolve("MI_DMX_GetAttr");
    uintptr_t handle = strtoul(tok[1], NULL, 0);
    int want = atoi(tok[2]);
    int outk = atoi(tok[3]) % NSCR;
    int cap = SCRSZ / 188;
    if (want > cap) want = cap;
    int got = 0;
    unsigned char last[188];
    int have_last = 0;
    uintptr_t outaddr;
    for (int poll = 0; poll < 6000 && got < want && fn; poll++) {
      outaddr = 0;
      uintptr_t r = ((uintptr_t(*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t *))fn)(handle, 4, 0, &outaddr);
      if (r == 0 && outaddr != 0) {
        unsigned char buf[512];
        memcpy(buf, (void *)outaddr, sizeof buf);
        for (int i = 0; i + 376 <= (int)sizeof buf && got < want; i++) {
          if (buf[i] == 0x47 && buf[i + 188] == 0x47) {
            if (!have_last || memcmp(last, buf + i, 188) != 0) {
              memcpy(scr[outk] + got * 188, buf + i, 188);
              memcpy(last, buf + i, 188);
              have_last = 1;
              got++;
            }
            break;
          }
        }
      }
      usleep(500);
    }
    out(fd, "ok got=%d lastaddr=0x%x\n", got, (unsigned)outaddr);
  } else if (!strcmp(tok[0], "tsstream") && n >= 3) {
    // tsstream <addr_hex> <port>  — background thread: accepts one TCP client
    // on <port> and continuously writes well-formed 188B TS packets polled
    // from <addr_hex>, until the client disconnects.
    g_stream_addr = strtoul(tok[1], NULL, 0);
    g_stream_port = atoi(tok[2]);
    pthread_t t;
    pthread_create(&t, NULL, stream_thread, NULL);
    pthread_detach(t);
    out(fd, "ok streaming addr=0x%x port=%d\n", (unsigned)g_stream_addr, g_stream_port);
  } else if (!strcmp(tok[0], "pvrstatus")) {
    pthread_mutex_lock(&g_pvr_lock);
    out(fd, "running=%d port=%d bytes=%llu sent=%llu clients=%llu error=%d\n",
        d0_dual_state.running, 40556, (unsigned long long)d0_dual_state.bytes, (unsigned long long)d0_dual_state.sent, (unsigned long long)d0_dual_clients, d0_dual_state.error);
    pthread_mutex_unlock(&g_pvr_lock);
  } else if (!strcmp(tok[0], "pvrset") || !strcmp(tok[0], "pvrstream")) {
    out(fd, "error use authenticated TUNE for record-only capture\n");
  } else if (!strcmp(tok[0], "mktsioparams") && n >= 4) {
    // mktsioparams <k_struct> <k_name> <pathval(hex/dec)>
    // writes {ptr_to_scr[k_name], pathval, 0, 0, ...} into scr[k_struct] natively,
    // so the pointer never has to round-trip through the text protocol.
    int ks = atoi(tok[1]) % NSCR, kn = atoi(tok[2]) % NSCR;
    uint32_t pathval = (uint32_t)strtoul(tok[3], NULL, 0);
    memset(scr[ks], 0, 64);
    *(void **)(scr[ks] + 0) = scr[kn];
    *(uint32_t *)(scr[ks] + 4) = pathval;
    out(fd, "ok ptr=%p\n", scr[kn]);
  } else if (!strcmp(tok[0], "setstr") && n >= 3) {
    int k = atoi(tok[1]) % NSCR;
    strncpy((char *)scr[k], tok[2], SCRSZ - 1);
    out(fd, "ok\n");
  } else if (!strcmp(tok[0], "mkpattern") && n >= 5) {
    // mkpattern <k_struct> <k_patternbuf> <k_maskbuf> <depth> [k_nmaskbuf]
    // Writes MI_DMX_SetPattern's real input struct into scr[k_struct], per
    // precise assembly tracing of libfpp.so's MI_DMX_SetPattern (2026-10-23):
    // [0:4]=pointer to scr[k_patternbuf] (pattern_ptr), [4:8]=pointer to
    // scr[k_maskbuf] (mask_ptr), [8:12]=pointer to scr[k_nmaskbuf] or 0
    // (nmask_ptr, optional), [12]=depth byte (must be 1..14).
    int ks = atoi(tok[1]) % NSCR, kp = atoi(tok[2]) % NSCR, km = atoi(tok[3]) % NSCR;
    uint8_t depth = (uint8_t)strtoul(tok[4], NULL, 0);
    void *nmaskptr = NULL;
    if (n >= 6) nmaskptr = scr[atoi(tok[5]) % NSCR];
    memset(scr[ks], 0, 16);
    *(void **)(scr[ks] + 0) = scr[kp];
    *(void **)(scr[ks] + 4) = scr[km];
    *(void **)(scr[ks] + 8) = nmaskptr;
    scr[ks][12] = depth;
    out(fd, "ok patternptr=%p maskptr=%p nmaskptr=%p\n", scr[kp], scr[km], nmaskptr);
  } else if (!strcmp(tok[0], "mkdmxopen") && n >= 5) {
    // mkdmxopen <k_struct> <typeflag> <bufsize> <enable_event(0/1)> <k_dummybuf>
    // Builds the full >=60-byte MI_DMX_Open input struct into scr[k_struct],
    // per reverse-engineered mik.ko _MI_DEV_DMX_DoInnerIoctl layout (2026-10-23):
    //   [0:4]=tsio handle tag (0xab000000, from mif_demux_get_path_params(0,..)[0])
    //   [4:8]=source handle tag (0xae100000, same call's [1])
    //   [8:12]=type flag (must be 0-3 for SecFlt-capable path; see 進捗20)
    //   [12:16]=0
    //   [16:20]=section buffer size (e.g. 0x1000)
    //   [20:28]=0
    //   [28:32]=REAL function pointer to libfpp.so's internal
    //           _MIF_DEMUX_FilterCallbackPara dispatcher (NOT literal 1 --
    //           that was the actual cause of 3 watchdog crashes in 進捗22-24;
    //           see 進捗25). Computed at runtime as
    //           dlsym(libfpp,"mif_demux_get_path_params") + 0x1300, a delta
    //           measured empirically in the same boot via the "sym" command
    //           and confirmed to reproduce the exact value the official app
    //           itself puts here (0xa8ebe651 in that boot).
    //   [32:36]=enable_event flag: if nonzero AND typeflag<5, kernel force-sets
    //           the callback to its own _MI_DEV_DMX_EventNotifyTask and calls
    //           MI_DEV_EVENT_Listen twice to actually subscribe for data-ready
    //           events (進捗22) -- THIS is what was missing all along.
    //   [36:52]=0
    //   [52:56]=0 (passed as mi_dmx_Open's own 2nd arg; keep 0, unverified)
    //   [56:60]=pointer to scr[k_dummybuf]. Decompiled MI_DEV_EVENT_Listen/
    //           ListenWithImplHandle in 進捗25: this is used ONLY as an
    //           equality-comparison key (never dereferenced/called), so it is
    //           NOT the crash cause and can safely be any value incl. NULL.
    //   [60:64]=0 (padding)
    int ks = atoi(tok[1]) % NSCR;
    uint32_t typeflag = (uint32_t)strtoul(tok[2], NULL, 0);
    uint32_t bufsize = (uint32_t)strtoul(tok[3], NULL, 0);
    uint32_t enable_event = (uint32_t)strtoul(tok[4], NULL, 0);
    void *dummyptr = NULL;
    if (n >= 6) dummyptr = scr[atoi(tok[5]) % NSCR];
    void *cbbase = resolve("mif_demux_get_path_params");
    void *cbptr = cbbase ? (void *)((uintptr_t)cbbase + 0x1300) : NULL;
    memset(scr[ks], 0, 64);
    *(uint32_t *)(scr[ks] + 0) = 0xab000000;
    *(uint32_t *)(scr[ks] + 4) = 0xae100000;
    *(uint32_t *)(scr[ks] + 8) = typeflag;
    *(uint32_t *)(scr[ks] + 16) = bufsize;
    *(void **)(scr[ks] + 28) = cbptr;
    *(uint32_t *)(scr[ks] + 32) = enable_event;
    *(void **)(scr[ks] + 56) = dummyptr;
    out(fd, "ok dummyptr=%p cbbase=%p cbptr=%p\n", dummyptr, cbbase, cbptr);
  } else {
    out(fd, "?\n");
  }
}

static void *serve(void *arg) {
  (void)arg;
  sleep(5);
  pthread_t control_thread;
  if (!pthread_create(&control_thread, NULL, control_server, NULL)) pthread_detach(control_thread);
  for (int i = 0; i < NSCR; i++) scr[i] = calloc(1, SCRSZ);
  int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in sa = {.sin_family = AF_INET, .sin_port = htons(40555), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  if (bind(s, (void *)&sa, sizeof sa) || listen(s, 2)) { LOG("bind/listen failed"); return NULL; }
  LOG("console up on 40555");
  for (;;) {
    int c = accept(s, NULL, NULL);
    if (c < 0) continue;
    struct timeval console_timeout = {.tv_sec=3};
    setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&console_timeout,sizeof console_timeout);
    setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,&console_timeout,sizeof console_timeout);
    char buf[1024];
    int authenticated=0, rejected=0;
    int len = 0, r;
    while ((r = read(c, buf + len, sizeof buf - len - 1)) > 0) {
      len += r;
      char *nl;
      buf[len] = 0;
      while ((nl = memchr(buf, '\n', len))) {
        *nl = 0;
        if (!authenticated) {
          if (!strncmp(buf,"AUTH ",5) && control_authenticated(buf+5)) {
            authenticated=1;
            console_timeout.tv_sec=30;
            setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&console_timeout,sizeof console_timeout);
            out(c,"OK\n");
          } else { out(c,"ERR authentication\n"); rejected=1;break; }
        } else { handle(c,buf); }
        int rest = len - (nl + 1 - buf);
        memmove(buf, nl + 1, rest);
        len = rest;
      }
      if(rejected || len==(int)sizeof(buf)-1)break;
    }
    close(c);
  }
}

__attribute__((constructor)) static void init(void) {
  char exe[256] = {0};
  readlink("/proc/self/exe", exe, sizeof exe - 1);
  if (!strstr(exe, "sitatvservice")) return;
  unsetenv("LD_PRELOAD");
  LOG("loaded into %s", exe);
  pthread_t t;
  pthread_create(&t, NULL, serve, NULL);
  pthread_detach(t);
}
