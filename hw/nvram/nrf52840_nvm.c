/*
 * Nordic Semiconductor nRF52840 non-volatile memory
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/nrf52840.h"
#include "hw/nvram/nrf52840_nvm.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

static uint64_t ficr_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF52840NVMState *s = NRF52840_NVM(opaque);

    assert(offset < sizeof(s->ficr_content));
    return s->ficr_content[offset / 4];
}

static void ficr_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned int size)
{
    /* Intentionally do nothing */
}

static const MemoryRegionOps ficr_ops = {
    .read = ficr_read,
    .write = ficr_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static uint64_t uicr_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF52840NVMState *s = NRF52840_NVM(opaque);
    uint64_t r = 0;

    assert(offset < sizeof(s->uicr_content));

    switch (offset) {
    case NRF52840_UICR_PSELRESET_0:
    case NRF52840_UICR_PSELRESET_1:
        r = 0x0;
        break;
    default:
        r = s->uicr_content[offset / 4];
        break;
    }

    return r;
}

static void uicr_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned int size)
{
    NRF52840NVMState *s = NRF52840_NVM(opaque);

    assert(offset < sizeof(s->uicr_content));
    s->uicr_content[offset / 4] = value;
}

static const MemoryRegionOps uicr_ops = {
    .read = uicr_read,
    .write = uicr_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN
};


static uint64_t io_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF52840NVMState *s = NRF52840_NVM(opaque);
    uint64_t r = 0;

    switch (offset) {
    case NRF52840_NVMC_READY:
        r = NRF52840_NVMC_READY_READY;
        break;
    case NRF52840_NVMC_CONFIG:
        r = s->config;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n", __func__, offset);
        break;
    }

    return r;
}

static void io_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned int size)
{
    NRF52840NVMState *s = NRF52840_NVM(opaque);

    switch (offset) {
    case NRF52840_NVMC_CONFIG:
        s->config = value & NRF52840_NVMC_CONFIG_MASK;
        break;
    case NRF52840_NVMC_ERASEPCR0:
    case NRF52840_NVMC_ERASEPCR1:
        if (s->config & NRF52840_NVMC_CONFIG_EEN) {
            /* Mask in-page sub address */
            value &= ~(NRF52840_PAGE_SIZE - 1);
            if (value <= (s->flash_size - NRF52840_PAGE_SIZE)) {
                memset(s->storage + value, 0xFF, NRF52840_PAGE_SIZE);
                memory_region_flush_rom_device(&s->flash, value,
                                               NRF52840_PAGE_SIZE);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Flash erase at 0x%" HWADDR_PRIx" while flash not erasable.\n",
            __func__, offset);
        }
        break;
    case NRF52840_NVMC_ERASEALL:
        if (value == NRF52840_NVMC_ERASE) {
            if (s->config & NRF52840_NVMC_CONFIG_EEN) {
                memset(s->storage, 0xFF, s->flash_size);
                memory_region_flush_rom_device(&s->flash, 0, s->flash_size);
                memset(s->uicr_content, 0xFF, sizeof(s->uicr_content));
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: Flash not erasable.\n",
                              __func__);
            }
        }
        break;
    case NRF52840_NVMC_ERASEUICR:
        if (value == NRF52840_NVMC_ERASE) {
            memset(s->uicr_content, 0xFF, sizeof(s->uicr_content));
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad write offset 0x%" HWADDR_PRIx "\n", __func__, offset);
    }
}

static const MemoryRegionOps io_ops = {
        .read = io_read,
        .write = io_write,
        .impl.min_access_size = 4,
        .impl.max_access_size = 4,
        .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t flash_read(void *opaque, hwaddr offset, unsigned size)
{
    /*
     * This is a rom_device MemoryRegion which is always in
     * romd_mode (we never put it in MMIO mode), so reads always
     * go directly to RAM and never come here.
     */
    g_assert_not_reached();
}

static void flash_write(void *opaque, hwaddr offset, uint64_t value,
        unsigned int size)
{
    NRF52840NVMState *s = NRF52840_NVM(opaque);

    if (s->config & NRF52840_NVMC_CONFIG_WEN) {
        uint32_t oldval;

        assert(offset + size <= s->flash_size);

        /* NOR Flash only allows bits to be flipped from 1's to 0's on write */
        oldval = ldl_le_p(s->storage + offset);
        oldval &= value;
        stl_le_p(s->storage + offset, oldval);

        memory_region_flush_rom_device(&s->flash, offset, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: Flash write 0x%" HWADDR_PRIx" while flash not writable.\n",
                __func__, offset);
    }
}



static const MemoryRegionOps flash_ops = {
    .read = flash_read,
    .write = flash_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void nrf52840_nvm_init(Object *obj)
{
    NRF52840NVMState *s = NRF52840_NVM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &io_ops, s, "nrf52840_soc.nvmc",
                          NRF52840_NVMC_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    memory_region_init_io(&s->ficr, obj, &ficr_ops, s, "nrf52840_soc.ficr",
                          sizeof(s->ficr_content));
    sysbus_init_mmio(sbd, &s->ficr);

    memory_region_init_io(&s->uicr, obj, &uicr_ops, s, "nrf52840_soc.uicr",
                          sizeof(s->uicr_content));
    sysbus_init_mmio(sbd, &s->uicr);
}

static void nrf52840_nvm_realize(DeviceState *dev, Error **errp)
{
    NRF52840NVMState *s = NRF52840_NVM(dev);
    Error *err = NULL;

    memory_region_init_rom_device(&s->flash, OBJECT(dev), &flash_ops, s,
        "nrf52840_soc.flash", s->flash_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    s->storage = memory_region_get_ram_ptr(&s->flash);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->flash);
}

static void nrf52840_nvm_reset(DeviceState *dev)
{
    NRF52840NVMState *s = NRF52840_NVM(dev);

    s->config = 0x00;
    memset(s->ficr_content, 0xFF, sizeof(s->ficr_content));
    memset(s->uicr_content, 0xFF, sizeof(s->uicr_content));
}

static Property nrf52840_nvm_properties[] = {
    DEFINE_PROP_UINT32("flash-size", NRF52840NVMState, flash_size, 0x40000),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_nvm = {
    .name = "nrf52840_soc.nvm",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(ficr_content, NRF52840NVMState,
                NRF52840_FICR_FIXTURE_SIZE),
        VMSTATE_UINT32_ARRAY(uicr_content, NRF52840NVMState,
                NRF52840_UICR_FIXTURE_SIZE),
        VMSTATE_UINT32(config, NRF52840NVMState),
        VMSTATE_END_OF_LIST()
    }
};

static void nrf52840_nvm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, nrf52840_nvm_properties);
    dc->vmsd = &vmstate_nvm;
    dc->realize = nrf52840_nvm_realize;
    dc->reset = nrf52840_nvm_reset;
}

static const TypeInfo nrf52840_nvm_info = {
    .name = TYPE_NRF52840_NVM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF52840NVMState),
    .instance_init = nrf52840_nvm_init,
    .class_init = nrf52840_nvm_class_init
};

static void nrf52840_nvm_register_types(void)
{
    type_register_static(&nrf52840_nvm_info);
}

type_init(nrf52840_nvm_register_types)
