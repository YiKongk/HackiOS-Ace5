/* OnePlus Ace 5 / giulia Synaptics S3910. Explicit ace5_touch=1 opt-in.
 * Conservative polled SPI brings up the controller without a GENI driver.
 * Coordinates reach the existing /dev/redwood-touch -> backboardd HID bridge.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/redwood_input.h>
#include <sys/systm.h>
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t ml_io_map_wcomb(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
static uint32_t s39_rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static void s39_wr(uintptr_t a, uint32_t v) {
    *(volatile uint32_t *)a=v; __asm__ volatile("dsb sy" ::: "memory");
}
static void s39_delay(unsigned us) { delay_for_interval(us, NSEC_PER_USEC); }
#include "giulia_s3910_hw.h"
#include "giulia_s3910_report.h"
static struct s39_hw hw;
static struct s39_contact last[10];
static unsigned active;
static void release_all(void) {
    for (unsigned i=0;i<10;i++) if (active&(1u<<i))
        redwood_touch_post(REDWOOD_TOUCH_UP,i,last[i].x,last[i].y);
    active=0;
}
static void s3910_thread(__unused void *arg, __unused wait_result_t wait) {
    struct s39_hw *h=&hw;
    int rc=-1; unsigned reports=0, bad=0, errors=0;
    h->gpio=ml_io_map(S39_TLMM_PA,0x100000);
    h->rsc=ml_io_map(S39_RSC_PA,0x10000);
    h->db=ml_io_map_wcomb(S39_DB_PA,0x20000);
    if (!h->gpio || !h->rsc || !h->db) goto failed;
    rc=s39_power(h);
    printf("s3910: power rc=%d RSC=%#x ldob4=%#x slot=%u\n",rc,h->rsc_id,h->regulator,h->slot);
    if (rc) goto failed;
    s39_bus_init(h); s39_reset(h);
    rc=s39_read(h);
    if (rc!=0x10 || h->length<24 || h->data[0]!=1 || h->data[1]!=1 ||
        memcmp(h->data+2,"S3910",5)) goto failed;
    printf("s3910: TouchComm v1 application firmware, build=%u\n",s39_le32(h->data+18));
    rc=s39_start_reports(h);
    if (rc) goto failed;
    printf("s3910: ready; max=%u,%u, 10 contacts, polled SPI\n",h->max_x,h->max_y);
    for (unsigned tick=0;;tick++) {
        if (!s39_get(h,162) || tick%10==0) {
            rc=s39_read(h);
            if (rc<0) {
                if (++errors>=10) { release_all(); goto failed; }
                bad++;
            } else {
                errors=0;
                if (rc==0x11) {
                    struct s39_contact point[10]={{0}}; unsigned mask=0;
                    if (s39_decode(h->data,h->length,h->max_x,h->max_y,point,&mask)) {
                        bad++; release_all();
                    } else {
                        for (unsigned i=0;i<10;i++) {
                            if (mask&(1u<<i)) {
                                redwood_touch_post(active&(1u<<i)?REDWOOD_TOUCH_MOVE:REDWOOD_TOUCH_DOWN,
                                    i,point[i].x,point[i].y);
                                last[i]=point[i];
                            } else if (active&(1u<<i)) redwood_touch_post(REDWOOD_TOUCH_UP,i,last[i].x,last[i].y);
                        }
                        active=mask; reports++;
                        if (reports<=20) printf("s3910: report=%u active=%#x\n",reports,active);
                    }
                } else if (rc==0x10) { /* reset invalidates the volatile report layout */
                    release_all(); rc=-40; goto failed;
                }
            }
        }
        if (tick%625==0) printf("s3910: reports=%u bad=%u active=%#x irq=%u\n",reports,bad,active,s39_get(h,162));
        s39_delay(8000);
    }
failed:
    printf("s3910: stopped rc=%d setup=%u header=%02x %02x %02x %02x\n",rc,h->setup_step,
        h->header[0],h->header[1],h->header[2],h->header[3]);
    thread_terminate(current_thread());
}
void giulia_s3910_init(void) {
    uint32_t enabled=0; thread_t thread;
    if (!PE_parse_boot_argn("ace5_touch",&enabled,sizeof(enabled)) || enabled!=1) return;
    if (kernel_thread_start(s3910_thread,NULL,&thread)==KERN_SUCCESS) thread_deallocate(thread);
}
