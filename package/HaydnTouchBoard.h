// SPDX-License-Identifier: BSD-2-Clause-Patent
// haydn (SM8350) touch controller board facts. Every constant states its source.
// Nothing here touches hardware; HaydnTouch.cpp owns the MMIO. Values come from
// the live firmware FDT (docs/haydn-fdt.dts, decompiled from /sys/firmware/fdt).
//
// haydn uses a FocalTech touch IC on QUP SE11, unlike venus (ST FingerTipS on the
// same SE) and sheng (Novatek over I2C). The SPI controller is the shared
// QcomGeniSpi component (GeniSpi.c); this header feeds it the board facts.
#ifndef HAYDN_TOUCH_BOARD_H
#define HAYDN_TOUCH_BOARD_H
#include <stdint.h>

// SPI serial engine (haydn-fdt.dts: spi@a8c000 { reg = <0xa8c000 0x4000> }).
#define HTB_SE_BASE      0x00a8c000ULL
#define HTB_SE_SIZE      0x4000ULL
// QUP wrapper (haydn-fdt.dts: qcom,qupv3_0_geni_se@9c0000 { reg = <0x9c0000 0x2000> }).
#define HTB_QUP_BASE     0x009c0000ULL
#define HTB_QUP_SIZE     0x2000ULL
// TLMM (haydn-fdt.dts: pinctrl@f000000). ctl_reg = 0x1000*gpio, io_reg = +4,
// mux bits 2..5, drive-strength bits 6..8, output-enable bit 9 (same layout as sm8550).
#define HTB_TLMM_BASE    0x0f000000ULL
#define HTB_GPIO_STRIDE  0x1000UL
// qupv3_se11_spi_active: pins gpio48/49/50 (MISO/MOSI/CLK), function "qup11".
// "qup11" is function slot 1 on lahaina gpio48..51 (cross-SoC-verified, see porting notes).
#define HTB_GPIO_MISO    48
#define HTB_GPIO_MOSI    49
#define HTB_GPIO_CLK     50
#define HTB_GPIO_CS      51   // CS0; SPI uses the engine's native CS, muxed like the data pins
#define HTB_FUNC_QUP11   1    // function slot index for "qup11"
#define HTB_DRIVE_6MA    2    // drive-strength <6> encodes (6mA/2)-1 = 2
// Touch interrupt / reset (haydn-fdt.dts focaltech@0): irq GPIO23 flags 0x2008 (LEVEL_LOW|ONESHOT),
// reset GPIO22 active high.
#define HTB_GPIO_IRQ     23
#define HTB_GPIO_RESET   22
#define HTB_IRQ_FLAGS    0x2008u
// GCC clock controller (haydn-fdt.dts: qcom,gcc@100000). The touch SPI is QUP1 SE11
// (spi@a8c000, pinctrl qupv3_se11). QUP1 clocks are NOT enabled by the boot chain the
// way QUP0's console UART is, so the driver votes them before touching the engine.
// Values from the SM8350 GCC clock driver (gcc-sm8350.c): enable votes at 0x52008,
// each branch checked at its halt register.
#define HTB_GCC_BASE     0x00100000ULL
#define HTB_GCC_VOTE_ENABLE  0x52008   // clock branch enable register for QUP1
// QUP1 branch votes (BIT positions in 0x52008).
#define HTB_VOTE_WRAP1_CORE_2X  (1u<<18)  // gcc_qupv3_wrap1_core_2x_clk, halt 0x23140
#define HTB_VOTE_WRAP1_CORE     (1u<<19)  // gcc_qupv3_wrap1_core_clk,    halt 0x23138
#define HTB_VOTE_WRAP1_M_AHB    (1u<<20)  // gcc_qupv3_wrap_1_m_ahb_clk,  halt 0x18004
#define HTB_VOTE_WRAP1_S_AHB    (1u<<21)  // gcc_qupv3_wrap_1_s_ahb_clk,  halt 0x18008
#define HTB_VOTE_WRAP1_S3       (1u<<25)  // gcc_qupv3_wrap1_s3_clk (SE11), halt 0x1839c
#define HTB_VOTE_WRAP1_ALL      (HTB_VOTE_WRAP1_CORE_2X | HTB_VOTE_WRAP1_CORE | \
                                 HTB_VOTE_WRAP1_M_AHB | HTB_VOTE_WRAP1_S_AHB | HTB_VOTE_WRAP1_S3)
#define HTB_HALT_WRAP1_S3       0x1839c   // halt register for the SE11 branch
#define HTB_HALT_CLK_OFF        (1u<<31)  // BRANCH_HALT_VOTED: bit 31 set = clock off
// Board DT: focaltech spi-max-frequency = 0x5b8d80 = 6 MHz.
#define HTB_SPI_HZ       6000000u
#define HTB_SPI_MODE     0       // SPI mode 0 (CPOL=0, CPHA=0)
// Display coordinates (focaltech,display-coords = <0 0 0x21c0 0x4b00>): the raw panel
// resolution is 1080x2400; the IC reports in its own 12-bit space.
#define HTB_TOUCH_X_MAX  1080
#define HTB_TOUCH_Y_MAX  2400
#define HTB_TOUCH_MAX_FINGERS 10  // focaltech,max-touch-number = <10>

static inline uint32_t htb_pin_cfg(unsigned func, unsigned drive_code) {
    // mux = func (bits 2..5), drive = drive_code (bits 6..8), pull disabled, output-enable off.
    return (func & 0xFu) << 2 | (drive_code & 0x7u) << 6;
}
static inline uint32_t htb_tlmm_ctl(unsigned gpio) { return (uint32_t)gpio * HTB_GPIO_STRIDE; }
static inline uint32_t htb_tlmm_io(unsigned gpio) { return (uint32_t)gpio * HTB_GPIO_STRIDE + 4; }

#endif
