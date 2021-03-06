/*
 * QEMU RISC-V VirtIO Board
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * RISC-V machine with 16550a UART and VirtIO MMIO
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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/armleopc.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "sysemu/device_tree.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"

#include <libfdt.h>

#if defined(TARGET_RISCV32)
# define BIOS_FILENAME "opensbi-riscv32-virt-fw_jump.bin"
#else
# define BIOS_FILENAME "opensbi-riscv64-virt-fw_jump.bin"
#endif

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} armleopc_memmap[] = {
    [ARMLEOPC_MROM] =        {     0x1000,       0x11000 },
    [ARMLEOPC_CLINT] =       {  0x2000000,       0x10000 },
    [ARMLEOPC_PLIC] =        {  0xc000000,     0x4000000 },
    [ARMLEOPC_UART0] =       { 0x10000000,         0x100 },
    [ARMLEOPC_DRAM] =        { 0x80000000,           0x0 },
};

static void create_fdt(RISCVArmleoPCState *s, const struct MemmapEntry *memmap,
    uint64_t mem_size, const char *cmdline)
{
    void *fdt;
    int cpu;
    uint32_t *cells;
    char *nodename;
    uint32_t plic_phandle, phandle = 1;

    fdt = s->fdt = create_device_tree(&s->fdt_size);
    if (!fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(fdt, "/", "model", "riscv-armleopc,qemu");
    qemu_fdt_setprop_string(fdt, "/", "compatible", "riscv-armleopc");
    qemu_fdt_setprop_cell(fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(fdt, "/soc");
    qemu_fdt_setprop(fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(fdt, "/soc", "#address-cells", 0x2);

    nodename = g_strdup_printf("/memory@%lx",
        (long)memmap[ARMLEOPC_DRAM].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        memmap[ARMLEOPC_DRAM].base >> 32, memmap[ARMLEOPC_DRAM].base,
        mem_size >> 32, mem_size);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    g_free(nodename);

    qemu_fdt_add_subnode(fdt, "/cpus");
    qemu_fdt_setprop_cell(fdt, "/cpus", "timebase-frequency",
                          SIFIVE_CLINT_TIMEBASE_FREQ);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(fdt, "/cpus", "#address-cells", 0x1);

    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        int cpu_phandle = phandle++;
        int intc_phandle;
        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        char *intc = g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        char *isa = riscv_isa_string(&s->soc.harts[cpu]);
        qemu_fdt_add_subnode(fdt, nodename);
        qemu_fdt_setprop_string(fdt, nodename, "mmu-type", "riscv,sv48");
        qemu_fdt_setprop_string(fdt, nodename, "riscv,isa", isa);
        qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv");
        qemu_fdt_setprop_string(fdt, nodename, "status", "okay");
        qemu_fdt_setprop_cell(fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_cell(fdt, nodename, "phandle", cpu_phandle);
        intc_phandle = phandle++;
        qemu_fdt_add_subnode(fdt, intc);
        qemu_fdt_setprop_cell(fdt, intc, "phandle", intc_phandle);
        qemu_fdt_setprop_string(fdt, intc, "compatible", "riscv,cpu-intc");
        qemu_fdt_setprop(fdt, intc, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(fdt, intc, "#interrupt-cells", 1);
        g_free(isa);
        g_free(intc);
        g_free(nodename);
    }

    /* Add cpu-topology node */
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map");
    qemu_fdt_add_subnode(fdt, "/cpus/cpu-map/cluster0");
    for (cpu = s->soc.num_harts - 1; cpu >= 0; cpu--) {
        char *core_nodename = g_strdup_printf("/cpus/cpu-map/cluster0/core%d",
                                              cpu);
        char *cpu_nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, cpu_nodename);
        qemu_fdt_add_subnode(fdt, core_nodename);
        qemu_fdt_setprop_cell(fdt, core_nodename, "cpu", intc_phandle);
        g_free(core_nodename);
        g_free(cpu_nodename);
    }

    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_SOFT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_M_TIMER);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/clint@%lx",
        (long)memmap[ARMLEOPC_CLINT].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,clint0");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[ARMLEOPC_CLINT].base,
        0x0, memmap[ARMLEOPC_CLINT].size);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    g_free(cells);
    g_free(nodename);

    plic_phandle = phandle++;
    cells =  g_new0(uint32_t, s->soc.num_harts * 4);
    for (cpu = 0; cpu < s->soc.num_harts; cpu++) {
        nodename =
            g_strdup_printf("/cpus/cpu@%d/interrupt-controller", cpu);
        uint32_t intc_phandle = qemu_fdt_get_phandle(fdt, nodename);
        cells[cpu * 4 + 0] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 1] = cpu_to_be32(IRQ_M_EXT);
        cells[cpu * 4 + 2] = cpu_to_be32(intc_phandle);
        cells[cpu * 4 + 3] = cpu_to_be32(IRQ_S_EXT);
        g_free(nodename);
    }
    nodename = g_strdup_printf("/soc/interrupt-controller@%lx",
        (long)memmap[ARMLEOPC_PLIC].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_cell(fdt, nodename, "#address-cells",
                          FDT_PLIC_ADDR_CELLS);
    qemu_fdt_setprop_cell(fdt, nodename, "#interrupt-cells",
                          FDT_PLIC_INT_CELLS);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "riscv,plic0");
    qemu_fdt_setprop(fdt, nodename, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop(fdt, nodename, "interrupts-extended",
        cells, s->soc.num_harts * sizeof(uint32_t) * 4);
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[ARMLEOPC_PLIC].base,
        0x0, memmap[ARMLEOPC_PLIC].size);
    qemu_fdt_setprop_cell(fdt, nodename, "riscv,ndev", 0);
    qemu_fdt_setprop_cell(fdt, nodename, "phandle", plic_phandle);
    plic_phandle = qemu_fdt_get_phandle(fdt, nodename);
    g_free(cells);
    g_free(nodename);

    nodename = g_strdup_printf("/uart@%lx",
        (long)memmap[ARMLEOPC_UART0].base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "compatible", "ns16550a");
    qemu_fdt_setprop_cells(fdt, nodename, "reg",
        0x0, memmap[ARMLEOPC_UART0].base,
        0x0, memmap[ARMLEOPC_UART0].size);
    qemu_fdt_setprop_cell(fdt, nodename, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupt-parent", plic_phandle);
    qemu_fdt_setprop_cell(fdt, nodename, "interrupts", UART0_IRQ);

    qemu_fdt_add_subnode(fdt, "/chosen");
    qemu_fdt_setprop_string(fdt, "/chosen", "stdout-path", nodename);
    if (cmdline) {
        qemu_fdt_setprop_string(fdt, "/chosen", "bootargs", cmdline);
    }
    g_free(nodename);

}


static void riscv_armleopc_board_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = armleopc_memmap;
    RISCVArmleoPCState *s = RISCV_ARMLEOPC_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    MemoryRegion *mask_rom = g_new(MemoryRegion, 1);
    char *plic_hart_config;
    size_t plic_hart_config_len;
    target_ulong start_addr = memmap[ARMLEOPC_DRAM].base;
    int i;
    unsigned int smp_cpus = machine->smp.cpus;

    /* Initialize SOC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, sizeof(s->soc),
                            TYPE_RISCV_HART_ARRAY, &error_abort, NULL);
    object_property_set_str(OBJECT(&s->soc), machine->cpu_type, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->soc), smp_cpus, "num-harts",
                            &error_abort);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* register system main memory (actual RAM) */
    memory_region_init_ram(main_mem, NULL, "riscv_armleopc_board.ram",
                           machine->ram_size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[ARMLEOPC_DRAM].base,
        main_mem);

    /* create device tree */
    create_fdt(s, memmap, machine->ram_size, machine->kernel_cmdline);

    /* boot rom */
    memory_region_init_rom(mask_rom, NULL, "riscv_armleopc_board.mrom",
                           memmap[ARMLEOPC_MROM].size, &error_fatal);
    memory_region_add_subregion(system_memory, memmap[ARMLEOPC_MROM].base,
                                mask_rom);

    riscv_find_and_load_firmware(machine, BIOS_FILENAME,
                                 memmap[ARMLEOPC_DRAM].base);

    if (machine->kernel_filename) {
        uint64_t kernel_entry = riscv_load_kernel(machine->kernel_filename,
                                                  NULL);

        if (machine->initrd_filename) {
            hwaddr start;
            hwaddr end = riscv_load_initrd(machine->initrd_filename,
                                           machine->ram_size, kernel_entry,
                                           &start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen",
                                  "linux,initrd-start", start);
            qemu_fdt_setprop_cell(s->fdt, "/chosen", "linux,initrd-end",
                                  end);
        }
    }

    /* reset vector */
    uint32_t reset_vec[8] = {
        0x00000297,                  /* 1:  auipc  t0, %pcrel_hi(dtb) */
        0x02028593,                  /*     addi   a1, t0, %pcrel_lo(1b) */
        0xf1402573,                  /*     csrr   a0, mhartid  */
#if defined(TARGET_RISCV32)
        0x0182a283,                  /*     lw     t0, 24(t0) */
#elif defined(TARGET_RISCV64)
        0x0182b283,                  /*     ld     t0, 24(t0) */
#endif
        0x00028067,                  /*     jr     t0 */
        0x00000000,
        start_addr,                  /* start: .dword */
        0x00000000,
                                     /* dtb: */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[ARMLEOPC_MROM].base, &address_space_memory);

    /* copy in the device tree */
    if (fdt_pack(s->fdt) || fdt_totalsize(s->fdt) >
            memmap[ARMLEOPC_MROM].size - sizeof(reset_vec)) {
        error_report("not enough space to store device-tree");
        exit(1);
    }
    qemu_fdt_dumpdtb(s->fdt, fdt_totalsize(s->fdt));
    rom_add_blob_fixed_as("mrom.fdt", s->fdt, fdt_totalsize(s->fdt),
                          memmap[ARMLEOPC_MROM].base + sizeof(reset_vec),
                          &address_space_memory);

    /* create PLIC hart topology configuration string */
    plic_hart_config_len = (strlen(ARMLEOPC_PLIC_HART_CONFIG) + 1) * smp_cpus;
    plic_hart_config = g_malloc0(plic_hart_config_len);
    for (i = 0; i < smp_cpus; i++) {
        if (i != 0) {
            strncat(plic_hart_config, ",", plic_hart_config_len);
        }
        strncat(plic_hart_config, ARMLEOPC_PLIC_HART_CONFIG, plic_hart_config_len);
        plic_hart_config_len -= (strlen(ARMLEOPC_PLIC_HART_CONFIG) + 1);
    }

    /* MMIO */
    s->plic = sifive_plic_create(memmap[ARMLEOPC_PLIC].base,
        plic_hart_config,
        ARMLEOPC_PLIC_NUM_SOURCES,
        ARMLEOPC_PLIC_NUM_PRIORITIES,
        ARMLEOPC_PLIC_PRIORITY_BASE,
        ARMLEOPC_PLIC_PENDING_BASE,
        ARMLEOPC_PLIC_ENABLE_BASE,
        ARMLEOPC_PLIC_ENABLE_STRIDE,
        ARMLEOPC_PLIC_CONTEXT_BASE,
        ARMLEOPC_PLIC_CONTEXT_STRIDE,
        memmap[ARMLEOPC_PLIC].size);
    sifive_clint_create(memmap[ARMLEOPC_CLINT].base,
        memmap[ARMLEOPC_CLINT].size, smp_cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);

    serial_mm_init(system_memory, memmap[ARMLEOPC_UART0].base,
        0, qdev_get_gpio_in(DEVICE(s->plic), UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    g_free(plic_hart_config);
}

static void riscv_armleopc_machine_instance_init(Object *obj)
{
}

static void riscv_armleopc_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V ArmleoPC Board";
    mc->init = riscv_armleopc_board_init;
    mc->max_cpus = 8;
    mc->default_cpu_type = ARMLEOPC_CPU;
}

static const TypeInfo riscv_armleopc_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("armleopc"),
    .parent     = TYPE_MACHINE,
    .class_init = riscv_armleopc_machine_class_init,
    .instance_init = riscv_armleopc_machine_instance_init,
    .instance_size = sizeof(RISCVArmleoPCState),
};

static void riscv_armleopc_machine_init_register_types(void)
{
    type_register_static(&riscv_armleopc_machine_typeinfo);
}

type_init(riscv_armleopc_machine_init_register_types)
