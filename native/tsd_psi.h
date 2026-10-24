#ifndef TCL_TSD_PSI_H
#define TCL_TSD_PSI_H
#include <stddef.h>
#include <stdint.h>
#define TSD_PSI_MAX 4096
#define TSD_PID_MAX 63
struct tsd_psi {
 uint16_t pid;
 uint8_t data[TSD_PSI_MAX];
 size_t used, expected;
 int last_cc;
};
struct tsd_service_pids {
 uint16_t pids[TSD_PID_MAX], count, pcr, video, audio, ecm;
};
typedef void (*tsd_section_cb)(void *,const uint8_t *,size_t);
uint32_t tsd_crc32(const uint8_t *,size_t);
void tsd_psi_reset(struct tsd_psi *,uint16_t);
void tsd_psi_feed(struct tsd_psi *,const uint8_t packet[188],tsd_section_cb,void *);
int tsd_pat_pmt(const uint8_t *,size_t,uint16_t tsid,uint16_t service,uint16_t *pmt);
int tsd_pid_add(struct tsd_service_pids *,unsigned);
int tsd_pmt_pids(const uint8_t *,size_t,uint16_t service,struct tsd_service_pids *);
#endif
