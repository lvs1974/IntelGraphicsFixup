/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef kern_regs_hpp
#define kern_regs_hpp

#define GUC_STATUS         (0xc000)

#define GS_UKERNEL_SHIFT       8
#define GS_UKERNEL_MASK        (0xFF << GS_UKERNEL_SHIFT)
#define GS_UKERNEL_LAPIC_DONE  (0x30 << GS_UKERNEL_SHIFT)
#define GS_UKERNEL_DPC_ERROR   (0x60 << GS_UKERNEL_SHIFT)
#define GS_UKERNEL_READY       (0xF0 << GS_UKERNEL_SHIFT)

#define DMA_ADDR_0_LOW     (0xc300)
#define DMA_ADDR_0_HIGH    (0xc304)
#define DMA_ADDR_1_LOW     (0xc308)
#define DMA_ADDR_1_HIGH    (0xc30c)
#define DMA_COPY_SIZE      (0xc310)
#define DMA_CTRL           (0xc314)

#define HUC_UKERNEL        (1<<9)
#define UOS_MOVE           (1<<4)
#define START_DMA          (1<<0)

#define GEN9_GT_PM_CONFIG  (0x13816c)
#define GT_DOORBELL_ENABLE (1<<0)

#define GEN7_MISCCPCTL                    (0x9424)
#define GEN7_DOP_CLOCK_GATE_ENABLE        (1<<0)
#define GEN8_DOP_CLOCK_GATE_CFCLK_ENABLE  (1<<2)
#define GEN8_DOP_CLOCK_GATE_GUC_ENABLE    (1<<4)
#define GEN8_DOP_CLOCK_GATE_MEDIA_ENABLE  (1<<6)

#define GUC_ARAT_C6DIS                        (0xA178)

#define GUC_SHIM_CONTROL                      (0xc064)
#define GUC_DISABLE_SRAM_INIT_TO_ZEROES       (1<<0)
#define GUC_ENABLE_READ_CACHE_LOGIC           (1<<1)
#define GUC_ENABLE_MIA_CACHING                (1<<2)
#define GUC_GEN10_MSGCH_ENABLE                (1<<4)
#define GUC_ENABLE_READ_CACHE_FOR_SRAM_DATA   (1<<9)
#define GUC_ENABLE_READ_CACHE_FOR_WOPCM_DATA  (1<<10)
#define GUC_ENABLE_MIA_CLOCK_GATING           (1<<15)
#define GUC_GEN10_SHIM_WC_ENABLE              (1<<21)

#define DMA_GUC_WOPCM_OFFSET       (0xc340)
#define HUC_LOADING_AGENT_VCR      (0<<1)
#define HUC_LOADING_AGENT_GUC      (1<<1)
#define GUC_WOPCM_OFFSET_VALUE     0x80000  /* 512KB */

#define GUC_WOPCM_SIZE             (0xc050)
#define GUC_WOPCM_TOP              (0x80 << 12)  /* 512KB */
#define BXT_GUC_WOPCM_RC6_RESERVED (0x10 << 12)  /* 64KB  */

#define DMA_ADDRESS_SPACE_WOPCM    (7 << 16)

#define SOFT_SCRATCH(n)            (0xc180 + (n) * 4)
#define SOFT_SCRATCH_COUNT         16

#define UOS_RSA_SCRATCH(i)         (0xc200 + (i) * 4)
#define UOS_RSA_SCRATCH_COUNT      64

#define GEN8_GTCR                  (0x4274)
#define GEN8_GTCR_INVALIDATE       (1<<0)

#define GEN6_GDRST              (0x941c)
#define GEN6_GRDOM_FULL         (1 << 0)
#define GEN6_GRDOM_RENDER       (1 << 1)
#define GEN6_GRDOM_MEDIA        (1 << 2)
#define GEN6_GRDOM_BLT          (1 << 3)
#define GEN6_GRDOM_VECS         (1 << 4)
#define GEN9_GRDOM_GUC          (1 << 5)
#define GEN8_GRDOM_MEDIA2       (1 << 7)

#define GUC_CTL_CTXINFO             0
#define GUC_CTL_ARAT_HIGH           1
#define GUC_CTL_ARAT_LOW            2
#define GUC_CTL_DEVICE_INFO         3
#define GUC_CTL_LOG_PARAMS          4
#define GUC_CTL_PAGE_FAULT_CONTROL	5
#define GUC_CTL_WA                  6
#define GUC_CTL_FEATURE             7
#define GUC_CTL_DEBUG               8
#define GUC_CTL_RSRVD               9

#define GUC_CTL_GT_TYPE_SHIFT       0
#define GUC_CTL_CORE_FAMILY_SHIFT   7
#define GUC_CORE_FAMILY_GEN9        12

#define GUC_CTL_WA_UK_BY_DRIVER     (1 << 3)

#define GUC_CTL_VCS2_ENABLED            (1 << 0)
#define GUC_CTL_KERNEL_SUBMISSIONS      (1 << 1)
#define GUC_CTL_FEATURE2                (1 << 2)
#define GUC_CTL_POWER_GATING            (1 << 3)
#define GUC_CTL_DISABLE_SCHEDULER       (1 << 4)
#define GUC_CTL_PREEMPTION_LOG          (1 << 5)
#define GUC_CTL_ENABLE_SLPC             (1 << 7)
#define GUC_CTL_RESET_ON_PREMPT_FAILURE (1 << 8)

#endif /* kern_regs_hpp */
