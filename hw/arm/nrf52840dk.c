/*
 * nRF52840DK
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include "hw/arm/nrf52840_soc.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

struct NRF52840DKMachineState {
    MachineState parent;

    NRF52840State nrf52840;
};

#define TYPE_NRF52840DK_MACHINE MACHINE_TYPE_NAME("nRF52840DK")

OBJECT_DECLARE_SIMPLE_TYPE(NRF52840DKMachineState, NRF52840DK_MACHINE)

static void nrf52840dk_init(MachineState *machine)
{
    NRF52840DKMachineState *s = NRF52840DK_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();

    object_initialize_child(OBJECT(machine), "nrf52840", &s->nrf52840,
                            TYPE_NRF52840_SOC);
    qdev_prop_set_chr(DEVICE(&s->nrf52840), "serial0", serial_hd(0));
    object_property_set_link(OBJECT(&s->nrf52840), "memory",
                             OBJECT(system_memory), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->nrf52840), &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       s->nrf52840.flash_size);
}

static void nrf52840dk_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "nRF52840DK test";
    mc->init = nrf52840dk_init;
    mc->max_cpus = 1;
}

static const TypeInfo nrf52840dk_info = {
    .name = TYPE_NRF52840DK_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(NRF52840DKMachineState),
    .class_init = nrf52840dk_machine_class_init,
};

static void nrf52840dk_machine_init(void)
{
    type_register_static(&nrf52840dk_info);
}

type_init(nrf52840dk_machine_init);
