/*
 * QEMU RISC-V VirtIO machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_RISCV_ARMLEOPC_H
#define HW_RISCV_ARMLEOPC_H

#include "hw/riscv/riscv_hart.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"

#define TYPE_RISCV_ARMLEOPC_MACHINE MACHINE_TYPE_NAME("armleopc")
#define RISCV_ARMLEOPC_MACHINE(obj) \
    OBJECT_CHECK(RISCVArmleoPCState, (obj), TYPE_RISCV_ARMLEOPC_MACHINE)

typedef struct {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    RISCVHartArrayState soc;
    DeviceState *plic;
    PFlashCFI01 *flash[2];

    void *fdt;
    int fdt_size;
} RISCVArmleoPCState;

enum {
    ARMLEOPC_DEBUG,
    ARMLEOPC_MROM,
    ARMLEOPC_TEST,
    ARMLEOPC_CLINT,
    ARMLEOPC_PLIC,
    ARMLEOPC_UART0,
    ARMLEOPC_VIRTIO,
    ARMLEOPC_FLASH,
    ARMLEOPC_DRAM,
    ARMLEOPC_PCIE_MMIO,
    ARMLEOPC_PCIE_PIO,
    ARMLEOPC_PCIE_ECAM
};

enum {
    UART0_IRQ = 10,
    VIRTIO_IRQ = 1, /* 1 to 8 */
    VIRTIO_COUNT = 8,
    PCIE_IRQ = 0x20, /* 32 to 35 */
    VIRTIO_NDEV = 0x35 /* Arbitrary maximum number of interrupts */
};

#define ARMLEOPC_PLIC_HART_CONFIG "MS"
#define ARMLEOPC_PLIC_NUM_SOURCES 127
#define ARMLEOPC_PLIC_NUM_PRIORITIES 7
#define ARMLEOPC_PLIC_PRIORITY_BASE 0x04
#define ARMLEOPC_PLIC_PENDING_BASE 0x1000
#define ARMLEOPC_PLIC_ENABLE_BASE 0x2000
#define ARMLEOPC_PLIC_ENABLE_STRIDE 0x80
#define ARMLEOPC_PLIC_CONTEXT_BASE 0x200000
#define ARMLEOPC_PLIC_CONTEXT_STRIDE 0x1000

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_PLIC_ADDR_CELLS   0
#define FDT_PLIC_INT_CELLS    1
#define FDT_INT_MAP_WIDTH     (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + 1 + \
                               FDT_PLIC_ADDR_CELLS + FDT_PLIC_INT_CELLS)

#if defined(TARGET_RISCV32)
#define ARMLEOPC_CPU TYPE_RISCV_CPU_BASE32
#elif defined(TARGET_RISCV64)
#define ARMLEOPC_CPU TYPE_RISCV_CPU_BASE64
#endif

#endif
