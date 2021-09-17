/*
 * Nordic Semiconductor nRF52 SoC
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/sysbus.h"
#include "hw/misc/unimp.h"
#include "qemu/log.h"

#include "hw/arm/nrf52840.h"
#include "hw/arm/nrf52840_soc.h"

#define NRF52840_FLASH_PAGES    256
#define NRF52840_SRAM_PAGES     16
#define NRF52840_FLASH_SIZE     (NRF52840_FLASH_PAGES * NRF52840_PAGE_SIZE)
#define NRF52840_SRAM_SIZE      (NRF52840_SRAM_PAGES * NRF52840_PAGE_SIZE)

#define BASE_TO_IRQ(base) ((base >> 12) & 0x1F)

/* HCLK (the main CPU clock) on this SoC is always 64MHz */
#define HCLK_FRQ 64000000LL


static void nrf52840_soc_realize(DeviceState *dev_soc, Error **errp)
{
    NRF52840State *s = NRF52840_SOC(dev_soc);
    MemoryRegion *mr;
    Error *err = NULL;
    uint8_t i = 0;
    hwaddr base_addr = 0;

    if (!s->board_memory) {
        error_setg(errp, "memory property was not set");
        return;
    }

    //system_clock_scale = NANOSECONDS_PER_SECOND / HCLK_FRQ;

    object_property_set_link(OBJECT(&s->cpu), "memory", OBJECT(&s->container),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->cpu), errp)) {
        return;
    }

    memory_region_add_subregion_overlap(&s->container, 0, s->board_memory, -1);

    memory_region_init_ram(&s->sram, OBJECT(s), "nrf52840.sram", s->sram_size,
                           &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(&s->container, NRF52840_SRAM_BASE, &s->sram);

    /* UART */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uart), 0);
    memory_region_add_subregion_overlap(&s->container, NRF52840_UART_BASE, mr, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu),
                       BASE_TO_IRQ(NRF52840_UART_BASE)));

    /* RNG */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rng), errp)) {
        return;
    }

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rng), 0);
    memory_region_add_subregion_overlap(&s->container, NRF52840_RNG_BASE, mr, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rng), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu),
                       BASE_TO_IRQ(NRF52840_RNG_BASE)));

    /* UICR, FICR, NVMC, FLASH */
    if (!object_property_set_uint(OBJECT(&s->nvm), "flash-size",
                                  s->flash_size, errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nvm), errp)) {
        return;
    }

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 0);
    memory_region_add_subregion_overlap(&s->container, NRF52840_NVMC_BASE, mr, 0);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 1);
    memory_region_add_subregion_overlap(&s->container, NRF52840_FICR_BASE, mr, 0);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 2);
    memory_region_add_subregion_overlap(&s->container, NRF52840_UICR_BASE, mr, 0);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nvm), 3);
    memory_region_add_subregion_overlap(&s->container, NRF52840_FLASH_BASE, mr, 0);

    // /* GPIO */
    // if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpio), errp)) {
    //     return;
    // }

    // mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpio), 0);
    // memory_region_add_subregion_overlap(&s->container, NRF52840_GPIO_BASE, mr, 0);

    // /* Pass all GPIOs to the SOC layer so they are available to the board */
    // qdev_pass_gpios(DEVICE(&s->gpio), dev_soc, NULL);

    /* RTC */
    for (i = 0; i < NRF52840_NUM_RTCS; i++) {
        if (!object_property_set_uint(OBJECT(&s->rtc[i]), "id", i, errp)) {
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc[i]), errp)) {
            return;
        }

        switch (i) {
        case 0:
            base_addr = NRF52840_RTC0_BASE;
            break;
        case 1:
            base_addr = NRF52840_RTC1_BASE;
            break;
        case 2:
            base_addr = NRF52840_RTC2_BASE;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: bad RTC number %d\n", __func__, i);
        }

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->rtc[i]), 0, base_addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->cpu),
                                            BASE_TO_IRQ(base_addr)));
    }

    /* TIMER */
    for (i = 0; i < NRF52840_NUM_TIMERS; i++) {
        if (!object_property_set_uint(OBJECT(&s->timer[i]), "id", i, errp)) {
            return;
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer[i]), errp)) {
            return;
        }

        base_addr = NRF52840_TIMER_BASE + i * NRF52840_PERIPHERAL_SIZE;

        sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer[i]), 0, base_addr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->cpu),
                                            BASE_TO_IRQ(base_addr)));
    }

    /* CLOCK */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->clock), errp)) {
        return;
    }

    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->clock), 0);
    memory_region_add_subregion_overlap(&s->container, NRF52840_CLOCK_BASE, mr, 0);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->clock), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu),
                       BASE_TO_IRQ(NRF52840_CLOCK_BASE)));

    /* STUB Peripherals */
    // memory_region_init_io(&s->clock, OBJECT(dev_soc), &clock_ops, NULL,
    //                       "nrf52840_soc.clock", NRF52840_PERIPHERAL_SIZE);
    // memory_region_add_subregion_overlap(&s->container,
    //                                     NRF52840_IOMEM_BASE, &s->clock, -1);

    create_unimplemented_device("nrf52840_soc.io", NRF52840_IOMEM_BASE,
                                NRF52840_IOMEM_SIZE);
    create_unimplemented_device("nrf52840_soc.private",
                                NRF52840_PRIVATE_BASE, NRF52840_PRIVATE_SIZE);
}

static void nrf52840_soc_init(Object *obj)
{
    uint8_t i = 0;

    NRF52840State *s = NRF52840_SOC(obj);

    memory_region_init(&s->container, obj, "nrf52840-container", UINT64_MAX);

    object_initialize_child(OBJECT(s), "armv7m", &s->cpu, TYPE_ARMV7M);
    qdev_prop_set_string(DEVICE(&s->cpu), "cpu-type",
                         ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_uint32(DEVICE(&s->cpu), "num-irq", 32);

    object_initialize_child(obj, "uart", &s->uart, TYPE_NRF52840_UART);
    object_property_add_alias(obj, "serial0", OBJECT(&s->uart), "chardev");

    object_initialize_child(obj, "rng", &s->rng, TYPE_NRF52840_RNG);

    object_initialize_child(obj, "nvm", &s->nvm, TYPE_NRF52840_NVM);

    // object_initialize_child(obj, "gpio", &s->gpio, TYPE_NRF52840_GPIO);

    for (i = 0; i < NRF52840_NUM_RTCS; i++) {
        object_initialize_child(obj, "rtc[*]", &s->rtc[i],
                                TYPE_NRF52840_RTC);
    }

    for (i = 0; i < NRF52840_NUM_TIMERS; i++) {
        object_initialize_child(obj, "timer[*]", &s->timer[i],
                                TYPE_NRF52840_TIMER);
    }

    object_initialize_child(obj, "clock", &s->clock, TYPE_NRF52840_CLOCK);
}

static Property nrf52840_soc_properties[] = {
    DEFINE_PROP_LINK("memory", NRF52840State, board_memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_UINT32("sram-size", NRF52840State, sram_size, NRF52840_SRAM_SIZE),
    DEFINE_PROP_UINT32("flash-size", NRF52840State, flash_size,
                       NRF52840_FLASH_SIZE),
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf52840_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = nrf52840_soc_realize;
    device_class_set_props(dc, nrf52840_soc_properties);
}

static const TypeInfo nrf52840_soc_info = {
    .name          = TYPE_NRF52840_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF52840State),
    .instance_init = nrf52840_soc_init,
    .class_init    = nrf52840_soc_class_init,
};

static void nrf52840_soc_types(void)
{
    type_register_static(&nrf52840_soc_info);
}
type_init(nrf52840_soc_types)
