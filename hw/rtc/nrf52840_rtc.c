/*
 * nRF52840 System-on-Chip RTC peripheral
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/nrf52840.h"
#include "hw/irq.h"
#include "hw/rtc/nrf52840_rtc.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TIMER_CLK_FREQ 32768LL

#define COUNTER_BITWIDTH 24

static uint32_t ns_to_ticks(NRF52840RTCState *s, int64_t ns)
{
    uint32_t freq = TIMER_CLK_FREQ >> s->prescaler;

    return muldiv64(ns, freq, NANOSECONDS_PER_SECOND);
}

static int64_t ticks_to_ns(NRF52840RTCState *s, uint32_t ticks)
{
    uint32_t freq = TIMER_CLK_FREQ >> s->prescaler;

    return muldiv64(ticks, NANOSECONDS_PER_SECOND, freq);
}

/* Returns number of ticks since last call */
static uint32_t update_counter(NRF52840RTCState *s, int64_t now)
{
    uint32_t ticks = ns_to_ticks(s, now - s->update_counter_ns);

    s->counter = (s->counter + ticks) % BIT(COUNTER_BITWIDTH);
    s->update_counter_ns = now;
    return ticks;
}

/* Assumes s->counter is up-to-date */
static void rearm_timer(NRF52840RTCState *s, int64_t now)
{
    int64_t min_ns = INT64_MAX;
    size_t i;

    for (i = 0; i < NRF52840_RTC_REG_COUNT; i++) {
        int64_t delta_ns;

        if (s->events_compare[i]) {
            continue; /* already expired, ignore it for now */
        }

        if (s->cc[i] <= s->counter) {
            delta_ns = ticks_to_ns(s, BIT(COUNTER_BITWIDTH) -
                                      s->counter + s->cc[i]);
        } else {
            delta_ns = ticks_to_ns(s, s->cc[i] - s->counter);
        }

        if (delta_ns < min_ns) {
            min_ns = delta_ns;
        }
    }

    if (min_ns != INT64_MAX) {
        timer_mod_ns(&s->timer, now + min_ns);
    }
}

static void update_irq(NRF52840RTCState *s)
{
    bool flag = false;
    size_t i;

    for (i = 0; i < NRF52840_RTC_REG_COUNT; i++) {
        flag |= s->events_compare[i] && extract32(s->inten, 16 + i, 1);
    }
    qemu_set_irq(s->irq, flag);
}

static void timer_expire(void *opaque)
{
    NRF52840RTCState *s = NRF52840_RTC(opaque);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t cc_remaining[NRF52840_RTC_REG_COUNT];
    uint32_t ticks;
    size_t i;

    for (i = 0; i < NRF52840_RTC_REG_COUNT; i++) {
        if (s->cc[i] > s->counter) {
            cc_remaining[i] = s->cc[i] - s->counter;
        } else {
            cc_remaining[i] = BIT(COUNTER_BITWIDTH) -
                              s->counter + s->cc[i];
        }
    }

    ticks = update_counter(s, now);

    for (i = 0; i < NRF52840_RTC_REG_COUNT; i++) {
        if (cc_remaining[i] <= ticks) {
            s->events_compare[i] = 1;
        }
    }

    update_irq(s);

    rearm_timer(s, now);
}

static uint64_t nrf52840_rtc_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF52840RTCState *s = NRF52840_RTC(opaque);
    uint64_t r = 0;

    switch (offset) {
    case NRF52840_RTC_EVENT_COMPARE_0 ... NRF52840_RTC_EVENT_COMPARE_3:
        r = s->events_compare[(offset - NRF52840_RTC_EVENT_COMPARE_0) / 4];
        break;
    case NRF52840_RTC_REG_INTENSET:
    case NRF52840_RTC_REG_INTENCLR:
        r = s->inten;
        break;
    case NRF52840_RTC_REG_COUNTER:
        timer_expire(s);
        r = s->counter;
        break;
    case NRF52840_RTC_REG_PRESCALER:
        r = s->prescaler;
        break;
    case NRF52840_RTC_REG_CC0 ... NRF52840_RTC_REG_CC3:
        r = s->cc[(offset - NRF52840_RTC_REG_CC0) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    // trace_nrf52840_rtc_read(s->id, offset, r, size);

    return r;
}

static void nrf52840_rtc_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned int size)
{
    NRF52840RTCState *s = NRF52840_RTC(opaque);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    size_t idx;

    // trace_nrf52840_rtc_write(s->id, offset, value, size);

    switch (offset) {
    case NRF52840_RTC_TASK_START:
        if (value == NRF52840_TRIGGER_TASK) {
            s->running = true;
            s->update_counter_ns = now - ticks_to_ns(s, s->counter);
            rearm_timer(s, now);
        }
        break;
    case NRF52840_RTC_TASK_STOP:
        if (value == NRF52840_TRIGGER_TASK) {
            s->running = false;
            timer_del(&s->timer);
        }
        break;
    case NRF52840_RTC_TASK_CLEAR:
        if (value == NRF52840_TRIGGER_TASK) {
            s->update_counter_ns = now;
            s->counter = 0;
            if (s->running) {
                rearm_timer(s, now);
            }
        }
        break;
    case NRF52840_RTC_EVENT_COMPARE_0 ... NRF52840_RTC_EVENT_COMPARE_3:
        if (value == NRF52840_EVENT_CLEAR) {
            s->events_compare[(offset - NRF52840_RTC_EVENT_COMPARE_0) / 4] = 0;

            if (s->running) {
                timer_expire(s); /* update counter and all state */
            }
        }
        break;
    case NRF52840_RTC_REG_INTENSET:
        s->inten |= value & NRF52840_RTC_REG_INTEN_MASK;
        break;
    case NRF52840_RTC_REG_INTENCLR:
        s->inten &= ~(value & NRF52840_RTC_REG_INTEN_MASK);
        break;
    case NRF52840_RTC_REG_PRESCALER:
        if (s->running) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: erroneous change of PRESCALER while RTC is running\n",
                __func__);
        }
        s->prescaler = value & NRF52840_RTC_REG_PRESCALER_MASK;
        break;
    case NRF52840_RTC_REG_CC0 ... NRF52840_RTC_REG_CC3:
        if (s->running) {
            timer_expire(s); /* update counter */
        }

        idx = (offset - NRF52840_RTC_REG_CC0) / 4;
        s->cc[idx] = value % BIT(COUNTER_BITWIDTH);

        if (s->running) {
            rearm_timer(s, now);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_irq(s);
}

static const MemoryRegionOps rtc_ops = {
    .read =  nrf52840_rtc_read,
    .write = nrf52840_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void nrf52840_rtc_init(Object *obj)
{
    NRF52840RTCState *s = NRF52840_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &rtc_ops, s,
                          TYPE_NRF52840_RTC, NRF52840_PERIPHERAL_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, timer_expire, s);
}

static void nrf52840_rtc_reset(DeviceState *dev)
{
    NRF52840RTCState *s = NRF52840_RTC(dev);

    timer_del(&s->timer);
    s->update_counter_ns = 0x00;
    s->counter = 0x00;
    s->running = false;

    memset(s->events_compare, 0x00, sizeof(s->events_compare));
    memset(s->cc, 0x00, sizeof(s->cc));

    s->inten = 0x00;
    s->prescaler = 0x00;
}

static int nrf52840_rtc_post_load(void *opaque, int version_id)
{
    NRF52840RTCState *s = NRF52840_RTC(opaque);

    if (s->running) {
        timer_expire(s);
    }
    return 0;
}

static const VMStateDescription vmstate_nrf52840_rtc = {
    .name = TYPE_NRF52840_RTC,
    .version_id = 1,
    .post_load = nrf52840_rtc_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(timer, NRF52840RTCState),
        VMSTATE_INT64(update_counter_ns, NRF52840RTCState),
        VMSTATE_UINT32(counter, NRF52840RTCState),
        VMSTATE_BOOL(running, NRF52840RTCState),
        VMSTATE_UINT8_ARRAY(events_compare, NRF52840RTCState,
                            NRF52840_RTC_REG_COUNT),
        VMSTATE_UINT32_ARRAY(cc, NRF52840RTCState, NRF52840_RTC_REG_COUNT),
        VMSTATE_UINT32(inten, NRF52840RTCState),
        VMSTATE_UINT32(prescaler, NRF52840RTCState),
        VMSTATE_END_OF_LIST()
    }
};

static Property nrf52840_rtcproperties[] = {
    DEFINE_PROP_UINT8("id", NRF52840RTCState, id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf52840_rtc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = nrf52840_rtc_reset;
    dc->vmsd = &vmstate_nrf52840_rtc;
    device_class_set_props(dc, nrf52840_rtcproperties);
}

static const TypeInfo nrf52840_rtc_info = {
    .name = TYPE_NRF52840_RTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF52840RTCState),
    .instance_init = nrf52840_rtc_init,
    .class_init = nrf52840_rtc_class_init
};

static void nrf52840_rtc_register_types(void)
{
    type_register_static(&nrf52840_rtc_info);
}

type_init(nrf52840_rtc_register_types)
