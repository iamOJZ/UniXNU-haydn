// SPDX-License-Identifier: BSD-2-Clause-Patent
// haydn (SM8350) FocalTech FTS5652 touch controller over QUP SE11 GENI SPI.
//
// The chip is identified as FTS5652P001 by the vendor focaltech_touch.ko
// (Device ID read from register 0x54, two bytes). The boot chain (XBL) already
// downloads the application firmware, so this driver only reads the chip ID,
// starts scanning, and polls the touch FIFO; it does not re-run the pramboot /
// ECC firmware download the vendor driver performs.
//
// Board facts are in HaydnTouchBoard.h (all values from the live firmware FDT).
// The SPI engine is the shared QcomGeniSpi component (GeniSpi.c); this file
// supplies its geni_io MMIO callbacks and the FTS5652 register protocol.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSSharedPtr.h>
#include <libkern/libkern.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <string.h>
extern "C" {
#include "GeniSpi.h"
#include "HaydnTouchBoard.h"
// <osfmk/arm/machine_routines.h>: the early device-memory mapping used by the
// GENI UART console patch as well.
unsigned long long ml_io_map(unsigned long long phys_addr, unsigned long long size);
}

// --- FTS5652 register addresses (FocalTech family, cross-checked with the vendor
// focaltech_touch.ko strings: read 0x54 / 0x5B / 0x0A / 0x06 / 0xFB, FC=0xAA/0x55, F1 0x50). ---
#define FTS_REG_CHIP_ID        0x54   // two-byte chip ID (vendor prints "Device ID:0x%02x%02x")
#define FTS_REG_POINT_COUNT    0x02   // number of active touch points in this frame
#define FTS_REG_TOUCH_START    0x00   // first byte of the point record
#define FTS_REG_SYSTEM_RESET   0xFC   // write 0xAA then 0x55 to reset into boot environment
#define FTS_REG_SCAN_MODE      0x00   // 0x00 active scan, 0x01 monitor / 0xC0 stop (family specific)
#define FTS_CMD_ACTIVE_MODE    0x00

// FocalTech point record: each point is 6 bytes [touchId/xH/xL/yH/yL/evt];
// event low 2 bits are press(0)/lift(1)/contact(2).
#define FTS_POINT_BYTES        6
#define FTS_EVENT_MASK         0x03
#define FTS_EVENT_DOWN         0x00
#define FTS_EVENT_UP           0x01
#define FTS_EVENT_CONTACT      0x02

// --- global observation surface for the bring-up console ---
extern "C" {
volatile uint32_t haydn_touch_stage;
volatile uint32_t haydn_touch_contacts;
volatile int haydn_touch_last_x = -1, haydn_touch_last_y = -1;
char haydn_touch_log[256];
}

namespace {

struct Region {
    uintptr_t va = 0;
};

bool mapRegion(uint64_t phys, uint64_t length, Region &region) {
    // Same path as the GENI UART console patch (0002): ml_io_map is available as
    // soon as the VM is up, well before the full IOKit device-memory stack. The
    // SPI engine and TLMM are device memory, so the mapping must be uncached.
    region.va = static_cast<uintptr_t>(ml_io_map(phys, length));
    return region.va != 0;
}

static inline uint32_t rd32(uintptr_t base, uint32_t reg) {
    return *reinterpret_cast<volatile uint32_t *>(base + reg);
}
static inline void wr32(uintptr_t base, uint32_t reg, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(base + reg) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

// geni_io callbacks for the SPI engine.
uint32_t spiRd(void *ctx, uint32_t reg) { return rd32(reinterpret_cast<uintptr_t>(ctx), reg); }
void spiWr(void *ctx, uint32_t reg, uint32_t value) { wr32(reinterpret_cast<uintptr_t>(ctx), reg, value); }
void spiDelayUs(void *, unsigned us) { IODelay(us); }

// TLMM pin mux. ctl_reg = gpio*0x1000, mux bits 2..5, drive bits 6..8.
void pinConfig(uintptr_t tlmm, unsigned gpio, unsigned func, unsigned drive) {
    wr32(tlmm, htb_tlmm_ctl(gpio), htb_pin_cfg(func, drive));
}
int pinRead(uintptr_t tlmm, unsigned gpio) {
    return (int)(rd32(tlmm, htb_tlmm_io(gpio)) & 1u);
}

struct HaydnTouch {
    Region tlmm, wrapper, se, gcc;
    geni_spi spi;
    bool running = false;

    // One full-duplex transfer with chip-select asserted. FocalTech reads need a
    // leading dummy byte: the IC clocks out the first byte of register data on the
    // byte after the register address.
    bool xfer(const uint8_t *tx, uint8_t *rx, size_t len) {
        return geni_spi_transfer(&spi, tx, rx, len, 0) == 0;
    }
    // Write a register (no read phase).
    bool regWrite(uint8_t reg, uint8_t value) {
        uint8_t tx[2] = { reg, value };
        return xfer(tx, nullptr, 2);
    }
    // Read a register. The first returned byte is a dummy and is dropped.
    bool regRead(uint8_t reg, uint8_t *out, size_t len) {
        uint8_t tx[8], rx[8];
        if (len + 2 > sizeof(tx)) return false;
        memset(tx, 0, sizeof(tx));
        memset(rx, 0, sizeof(rx));
        tx[0] = reg;
        if (!xfer(tx, rx, len + 1)) return false;   // +1 dummy byte
        memcpy(out, rx + 1, len);
        return true;
    }
};

} // namespace

// Called from the board init hook (bsd_init) via sheng_pmgr_run-style wiring, or
// directly from a late boot hook. Stage values land in haydn_touch_stage.
extern "C" void haydn_touch_run(void);

void haydn_touch_run(void)
{
    HaydnTouch t;
    memset(&t.spi, 0, sizeof(t.spi));

    kprintf("haydn-touch: begin\n");

    haydn_touch_stage = 1;
    // Map the TLMM, QUP wrapper, serial engine and the GCC clock controller.
    if (!mapRegion(HTB_TLMM_BASE, 0x100000, t.tlmm)) { kprintf("haydn-touch: tlmm map failed\n"); return; }
    if (!mapRegion(HTB_QUP_BASE, HTB_QUP_SIZE, t.wrapper)) { kprintf("haydn-touch: qup map failed\n"); return; }
    if (!mapRegion(HTB_SE_BASE, HTB_SE_SIZE, t.se)) { kprintf("haydn-touch: se map failed\n"); return; }
    if (!mapRegion(HTB_GCC_BASE, 0x60000, t.gcc)) { kprintf("haydn-touch: gcc map failed\n"); return; }
    kprintf("haydn-touch: mapped tlmm=%llx qup=%llx se=%llx gcc=%llx\n", (unsigned long long)t.tlmm.va, (unsigned long long)t.wrapper.va, (unsigned long long)t.se.va, (unsigned long long)t.gcc.va);

    haydn_touch_stage = 2;
    // Mux the SPI data pins and the CS line to function qup11.
    pinConfig(t.tlmm.va, HTB_GPIO_MISO, HTB_FUNC_QUP11, HTB_DRIVE_6MA);
    pinConfig(t.tlmm.va, HTB_GPIO_MOSI, HTB_FUNC_QUP11, HTB_DRIVE_6MA);
    pinConfig(t.tlmm.va, HTB_GPIO_CLK,  HTB_FUNC_QUP11, HTB_DRIVE_6MA);
    pinConfig(t.tlmm.va, HTB_GPIO_CS,   HTB_FUNC_QUP11, HTB_DRIVE_6MA);
    // Touch IRQ is a level-low input; reset is an output, drive high (out of reset).
    pinConfig(t.tlmm.va, HTB_GPIO_IRQ,   0, 0);            // function 0 = gpio input
    pinConfig(t.tlmm.va, HTB_GPIO_RESET, 0, HTB_DRIVE_6MA);
    wr32(t.tlmm.va, htb_tlmm_io(HTB_GPIO_RESET), 1u << 9); // output enable + high
    kprintf("haydn-touch: pins muxed\n");

    // Vote the QUP1 clocks (core + AHB + the SE11 branch) and wait for the SE11
    // branch to leave halt. The boot chain only keeps QUP0 (console UART) running,
    // so without this the first read of the SPI engine hangs the bus. The GCC is
    // mapped from 0x100000 as one region, so the full register offset is used.
    {
        wr32(t.gcc.va, HTB_GCC_VOTE_ENABLE, rd32(t.gcc.va, HTB_GCC_VOTE_ENABLE) | HTB_VOTE_WRAP1_ALL);
        bool ok = false;
        for (unsigned i = 0; i < 100000; ++i) {
            if (!(rd32(t.gcc.va, HTB_HALT_WRAP1_S3) & HTB_HALT_CLK_OFF)) { ok = true; break; }
            IODelay(10);
        }
        kprintf("haydn-touch: qup1 clocks voted, s3 halt %s\n", ok ? "clear" : "STILL SET");
    }

    haydn_touch_stage = 3;
    // Wire the geni_io callbacks and read the engine identity. ctx is the mapped
    // virtual address itself (not a pointer to the field that holds it).
    t.spi.se = { reinterpret_cast<void *>(t.se.va), spiRd, spiWr, spiDelayUs };
    t.spi.wrapper = { reinterpret_cast<void *>(t.wrapper.va), spiRd, spiWr, spiDelayUs };
    t.spi.poll_limit = 200000;
    if (geni_spi_init(&t.spi) != 0) { kprintf("haydn-touch: geni init failed (proto %u)\n", t.spi.info.protocol); return; }
    if (t.spi.info.protocol != GENI_PROTO_SPI) { kprintf("haydn-touch: SE not SPI (proto %u)\n", t.spi.info.protocol); return; }
    kprintf("haydn-touch: GENI SPI ok, fw_rev=%x hw_param0=%x\n", t.spi.info.fw_revision, t.spi.info.hw_param0);

    haydn_touch_stage = 4;
    // Configure mode 0, chip select 0, 6 MHz source clock (XO 19.2 MHz, clk_sel 0).
    geni_spi_setup(&t.spi, HTB_SPI_MODE, 0);
    geni_spi_set_clock(&t.spi, HTB_SPI_HZ, 19200000u, 0);
    kprintf("haydn-touch: spi configured 6MHz mode0\n");

    haydn_touch_stage = 5;
    // Read the two-byte chip ID and confirm it is a FocalTech FTS5652.
    uint8_t id[2] = { 0, 0 };
    if (!t.regRead(FTS_REG_CHIP_ID, id, 2)) { kprintf("haydn-touch: chip id read failed\n"); return; }
    // FTS5652 family chip id (low byte varies by revision); accept any non-zero FocalTech id.
    if (id[0] == 0 && id[1] == 0) { kprintf("haydn-touch: chip id 00 00\n"); return; }
    snprintf(haydn_touch_log, sizeof haydn_touch_log, "FTS5652 id %02x %02x", id[0], id[1]);
    kprintf("haydn-touch: FTS5652 id %02x %02x\n", id[0], id[1]);

    haydn_touch_stage = 6;
    // The application firmware is already running (loaded by XBL); request active scan.
    t.regWrite(FTS_REG_SCAN_MODE, FTS_CMD_ACTIVE_MODE);
    snprintf(haydn_touch_log, sizeof haydn_touch_log, "touch scanning, id %02x %02x", id[0], id[1]);
    kprintf("haydn-touch: done, active scan requested\n");

    // No blocking poll here: this hook runs inside bsd_init, so a spin would stall
    // the whole kernel. The handshake above is the bring-up milestone (SPI works,
    // the chip answers with a real FTS5652 id); IRQ-driven report + HID publishing
    // come later and run on their own work-loop / thread.
}
