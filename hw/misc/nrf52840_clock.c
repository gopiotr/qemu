/*
 * nRF52840 SoC CLOCK emulation
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/arm/nrf52840.h"
#include "hw/irq.h"
#include "hw/misc/nrf52840_clock.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

static void nrf52840_clock_update_irq(NRF52840CLOCKState *s)
{
    bool irq = false;
    
    irq |= (s->lfclk_started_event_enabled &&
            s->lfclk_event_generated );
    irq |= (s->hfclk_started_event_enabled &&
            s->hfclk_event_generated );
    
    qemu_set_irq(s->irq, irq);
}

static uint64_t clock_read(void *opaque, hwaddr addr, unsigned int size)
{
    NRF52840CLOCKState *s = NRF52840_CLOCK(opaque);
    uint64_t r;

    switch (addr) {
    case A_CLOCK_EVENTS_HFCLKSTARTED:
        r = s->hfclk_event_generated;
        break;
    case A_CLOCK_EVENTS_LFCLKSTARTED:
        r = s->lfclk_event_generated;
        break;
    case A_CLOCK_HFCLKSTAT:
        r = s->reg[R_CLOCK_HFCLKSTAT] | (s->hfclk_started << R_CLOCK_HFCLKSTAT_STATE_SHIFT);
        break;
    case A_CLOCK_LFCLKSTAT:
        r = 0x00010001;  // TODO: repair this quick fix
        // r = s->reg[R_CLOCK_LFCLKSTAT] | 
        //     (s->lfclk_started << R_CLOCK_LFCLKSTAT_STATE_SHIFT) |
        //     (s->lfclk_source << R_CLOCK_LFCLKSTAT_SRC_SHIFT);
        break;
    default:
        r = s->reg[addr / 4];
        break;
    }

    return r;
}

static void clock_write(void *opaque, hwaddr addr,
                       uint64_t value, unsigned int size)
{
    NRF52840CLOCKState *s = NRF52840_CLOCK(opaque);

    switch (addr) {

    case A_CLOCK_TASKS_HFCLKSTART:
        if (value == 1) {
            s->hfclk_started = true;
            s->hfclk_event_generated = true;
            nrf52840_clock_update_irq(s);
        }
        s->reg[R_CLOCK_TASKS_HFCLKSTART] = value;
        break;
    case A_CLOCK_TASKS_LFCLKSTART:
        if (value == 1) {
            s->lfclk_started = true;
            s->lfclk_event_generated = true;
            nrf52840_clock_update_irq(s);
        }
        s->reg[R_CLOCK_TASKS_LFCLKSTART] = value;
        break;
    case A_CLOCK_EVENTS_HFCLKSTARTED:
        if (value & R_CLOCK_EVENTS_HFCLKSTARTED_GENERATED_MASK) {
            s->hfclk_event_generated = true;
            nrf52840_clock_update_irq(s);
        } 
        else {
            s->hfclk_event_generated = false;
        }
        s->reg[R_CLOCK_EVENTS_HFCLKSTARTED] = value;
        break;
    case A_CLOCK_EVENTS_LFCLKSTARTED:
        if (value & R_CLOCK_EVENTS_LFCLKSTARTED_GENERATED_MASK) {
            s->lfclk_event_generated = true;
            nrf52840_clock_update_irq(s);
        }
        else {
            s->lfclk_event_generated = false;
        }
        s->reg[R_CLOCK_EVENTS_LFCLKSTARTED] = value;
        break;
    case A_CLOCK_INTENSET:
        if (value & R_CLOCK_INTENSET_HFCLKSTARTED_MASK) {
            s->hfclk_started_event_enabled = true;
        }
        if (value & R_CLOCK_INTENSET_LFCLKSTARTED_MASK) {
            s->lfclk_started_event_enabled = true;
        }
        nrf52840_clock_update_irq(s);
        break;
    case A_CLOCK_INTENCLR:
        if (value & R_CLOCK_INTENCLR_HFCLKSTARTED_MASK) {
            s->hfclk_started_event_enabled = false;
        }
        if (value & R_CLOCK_INTENCLR_LFCLKSTARTED_MASK) {
            s->lfclk_started_event_enabled = false;
        }
        nrf52840_clock_update_irq(s);
        break;
    case A_CLOCK_LFCLKSRC:
        s->lfclk_source = value & R_CLOCK_LFCLKSRC_SRC_MASK;
        s->reg[R_CLOCK_LFCLKSRC] = value;
        break;
    default:
        s->reg[addr / 4] = value;
        break;
    }

    nrf52840_clock_update_irq(s);
}

static const MemoryRegionOps clock_ops = {
    .read =  clock_read,
    .write = clock_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void nrf52840_clock_reset(DeviceState *dev)
{
    NRF52840CLOCKState *s = NRF52840_CLOCK(dev);

    memset(s->reg, 0, sizeof(s->reg));

    s->hfclk_started = false;
    s->lfclk_started = false;
    s->hfclk_started_event_enabled = false;
    s->hfclk_event_generated = false;
    s->lfclk_started_event_enabled = false;
    s->lfclk_event_generated = false;
    s->lfclk_source = 0;

    nrf52840_clock_update_irq(s);
}

static void nrf52840_clock_init(Object *obj)
{
    NRF52840CLOCKState *s = NRF52840_CLOCK(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &clock_ops, s,
                          "nrf52840_soc.clock", CLOCK_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription nrf52840_clock_vmstate = {
    .name = "nrf52840_soc.clock",
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, NRF52840CLOCKState, CLOCK_REGISTERS_SIZE),
        VMSTATE_BOOL(hfclk_started, NRF52840CLOCKState),
        VMSTATE_BOOL(lfclk_started, NRF52840CLOCKState),
        VMSTATE_BOOL(hfclk_started_event_enabled, NRF52840CLOCKState),
        VMSTATE_BOOL(hfclk_event_generated, NRF52840CLOCKState),
        VMSTATE_BOOL(lfclk_started_event_enabled, NRF52840CLOCKState),
        VMSTATE_BOOL(lfclk_event_generated, NRF52840CLOCKState),
        VMSTATE_UINT32(lfclk_source, NRF52840CLOCKState),
        VMSTATE_END_OF_LIST()
    }
};

static Property nrf52840_clock_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf52840_clock_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nrf52840_clock_properties);
    dc->vmsd = &nrf52840_clock_vmstate;
    dc->reset = nrf52840_clock_reset;
}

static const TypeInfo nrf52840_clock_info = {
    .name = TYPE_NRF52840_CLOCK,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF52840CLOCKState),
    .instance_init = nrf52840_clock_init,
    .class_init = nrf52840_clock_class_init
};

static void nrf52840_clock_register_types(void)
{
    type_register_static(&nrf52840_clock_info);
}

type_init(nrf52840_clock_register_types)
