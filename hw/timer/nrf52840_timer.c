/*
 * nRF52840 System-on-Chip Timer peripheral
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/nrf52840.h"
#include "hw/irq.h"
#include "hw/timer/nrf52840_timer.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
// #include "trace.h"

#define TIMER_CLK_FREQ 64000000LL

static uint32_t const bitwidths[] = {16, 8, 24, 32};

static uint32_t ns_to_ticks(NRF52840TimerState *s, int64_t ns)
{
    uint32_t freq = TIMER_CLK_FREQ >> s->prescaler;

    return muldiv64(ns, freq, NANOSECONDS_PER_SECOND);
}

static int64_t ticks_to_ns(NRF52840TimerState *s, uint32_t ticks)
{
    uint32_t freq = TIMER_CLK_FREQ >> s->prescaler;

    return muldiv64(ticks, NANOSECONDS_PER_SECOND, freq);
}

/* Returns number of ticks since last call */
static uint32_t update_counter(NRF52840TimerState *s, int64_t now)
{
    uint32_t ticks = ns_to_ticks(s, now - s->update_counter_ns);

    s->counter = (s->counter + ticks) % BIT(bitwidths[s->bitmode]);
    s->update_counter_ns = now;
    return ticks;
}

/* Assumes s->counter is up-to-date */
static void rearm_timer(NRF52840TimerState *s, int64_t now)
{
    int64_t min_ns = INT64_MAX;
    size_t i;

    for (i = 0; i < NRF52840_TIMER_REG_COUNT; i++) {
        int64_t delta_ns;

        if (s->events_compare[i]) {
            continue; /* already expired, ignore it for now */
        }

        if (s->cc[i] <= s->counter) {
            delta_ns = ticks_to_ns(s, BIT(bitwidths[s->bitmode]) -
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

static void update_irq(NRF52840TimerState *s)
{
    bool flag = false;
    size_t i;

    for (i = 0; i < NRF52840_TIMER_REG_COUNT; i++) {
        flag |= s->events_compare[i] && extract32(s->inten, 16 + i, 1);
    }
    qemu_set_irq(s->irq, flag);
}

static void timer_expire(void *opaque)
{
    NRF52840TimerState *s = NRF52840_TIMER(opaque);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t cc_remaining[NRF52840_TIMER_REG_COUNT];
    bool should_stop = false;
    uint32_t ticks;
    size_t i;

    for (i = 0; i < NRF52840_TIMER_REG_COUNT; i++) {
        if (s->cc[i] > s->counter) {
            cc_remaining[i] = s->cc[i] - s->counter;
        } else {
            cc_remaining[i] = BIT(bitwidths[s->bitmode]) -
                              s->counter + s->cc[i];
        }
    }

    ticks = update_counter(s, now);

    for (i = 0; i < NRF52840_TIMER_REG_COUNT; i++) {
        if (cc_remaining[i] <= ticks) {
            s->events_compare[i] = 1;

            if (s->shorts & BIT(i)) {
                s->timer_start_ns = now;
                s->update_counter_ns = s->timer_start_ns;
                s->counter = 0;
            }

            should_stop |= s->shorts & BIT(i + 8);
        }
    }

    update_irq(s);

    if (should_stop) {
        s->running = false;
        timer_del(&s->timer);
    } else {
        rearm_timer(s, now);
    }
}

static void counter_compare(NRF52840TimerState *s)
{
    uint32_t counter = s->counter;
    size_t i;

    for (i = 0; i < NRF52840_TIMER_REG_COUNT; i++) {
        if (counter == s->cc[i]) {
            s->events_compare[i] = 1;

            if (s->shorts & BIT(i)) {
                s->counter = 0;
            }
        }
    }
}

static uint64_t nrf52840_timer_read(void *opaque, hwaddr offset, unsigned int size)
{
    NRF52840TimerState *s = NRF52840_TIMER(opaque);
    uint64_t r = 0;

    switch (offset) {
    case NRF52840_TIMER_EVENT_COMPARE_0 ... NRF52840_TIMER_EVENT_COMPARE_3:
        r = s->events_compare[(offset - NRF52840_TIMER_EVENT_COMPARE_0) / 4];
        break;
    case NRF52840_TIMER_REG_SHORTS:
        r = s->shorts;
        break;
    case NRF52840_TIMER_REG_INTENSET:
        r = s->inten;
        break;
    case NRF52840_TIMER_REG_INTENCLR:
        r = s->inten;
        break;
    case NRF52840_TIMER_REG_MODE:
        r = s->mode;
        break;
    case NRF52840_TIMER_REG_BITMODE:
        r = s->bitmode;
        break;
    case NRF52840_TIMER_REG_PRESCALER:
        r = s->prescaler;
        break;
    case NRF52840_TIMER_REG_CC0 ... NRF52840_TIMER_REG_CC3:
        r = s->cc[(offset - NRF52840_TIMER_REG_CC0) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    // trace_nrf52840_timer_read(s->id, offset, r, size);

    return r;
}

static void nrf52840_timer_write(void *opaque, hwaddr offset,
                       uint64_t value, unsigned int size)
{
    NRF52840TimerState *s = NRF52840_TIMER(opaque);
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    size_t idx;

    // trace_nrf52840_timer_write(s->id, offset, value, size);

    switch (offset) {
    case NRF52840_TIMER_TASK_START:
        if (value == NRF52840_TRIGGER_TASK && s->mode == NRF52840_TIMER_TIMER) {
            s->running = true;
            s->timer_start_ns = now - ticks_to_ns(s, s->counter);
            s->update_counter_ns = s->timer_start_ns;
            rearm_timer(s, now);
        }
        break;
    case NRF52840_TIMER_TASK_STOP:
    case NRF52840_TIMER_TASK_SHUTDOWN:
        if (value == NRF52840_TRIGGER_TASK) {
            s->running = false;
            timer_del(&s->timer);
        }
        break;
    case NRF52840_TIMER_TASK_COUNT:
        if (value == NRF52840_TRIGGER_TASK && s->mode == NRF52840_TIMER_COUNTER) {
            s->counter = (s->counter + 1) % BIT(bitwidths[s->bitmode]);
            counter_compare(s);
        }
        break;
    case NRF52840_TIMER_TASK_CLEAR:
        if (value == NRF52840_TRIGGER_TASK) {
            s->timer_start_ns = now;
            s->update_counter_ns = s->timer_start_ns;
            s->counter = 0;
            if (s->running) {
                rearm_timer(s, now);
            }
        }
        break;
    case NRF52840_TIMER_TASK_CAPTURE_0 ... NRF52840_TIMER_TASK_CAPTURE_3:
        if (value == NRF52840_TRIGGER_TASK) {
            if (s->running) {
                timer_expire(s); /* update counter and all state */
            }

            idx = (offset - NRF52840_TIMER_TASK_CAPTURE_0) / 4;
            s->cc[idx] = s->counter;
            // trace_nrf52840_timer_set_count(s->id, idx, s->counter);
        }
        break;
    case NRF52840_TIMER_EVENT_COMPARE_0 ... NRF52840_TIMER_EVENT_COMPARE_3:
        if (value == NRF52840_EVENT_CLEAR) {
            s->events_compare[(offset - NRF52840_TIMER_EVENT_COMPARE_0) / 4] = 0;

            if (s->running) {
                timer_expire(s); /* update counter and all state */
            }
        }
        break;
    case NRF52840_TIMER_REG_SHORTS:
        s->shorts = value & NRF52840_TIMER_REG_SHORTS_MASK;
        break;
    case NRF52840_TIMER_REG_INTENSET:
        s->inten |= value & NRF52840_TIMER_REG_INTEN_MASK;
        break;
    case NRF52840_TIMER_REG_INTENCLR:
        s->inten &= ~(value & NRF52840_TIMER_REG_INTEN_MASK);
        break;
    case NRF52840_TIMER_REG_MODE:
        s->mode = value;
        break;
    case NRF52840_TIMER_REG_BITMODE:
        if (s->mode == NRF52840_TIMER_TIMER && s->running) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "%s: erroneous change of BITMODE while timer is running\n",
                    __func__);
        }
        s->bitmode = value & NRF52840_TIMER_REG_BITMODE_MASK;
        break;
    case NRF52840_TIMER_REG_PRESCALER:
        if (s->mode == NRF52840_TIMER_TIMER && s->running) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "%s: erroneous change of PRESCALER while timer is running\n",
                __func__);
        }
        s->prescaler = value & NRF52840_TIMER_REG_PRESCALER_MASK;
        break;
    case NRF52840_TIMER_REG_CC0 ... NRF52840_TIMER_REG_CC3:
        if (s->running) {
            timer_expire(s); /* update counter */
        }

        idx = (offset - NRF52840_TIMER_REG_CC0) / 4;
        s->cc[idx] = value % BIT(bitwidths[s->bitmode]);

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

static const MemoryRegionOps timer_ops = {
    .read =  nrf52840_timer_read,
    .write = nrf52840_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void nrf52840_timer_init(Object *obj)
{
    NRF52840TimerState *s = NRF52840_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &timer_ops, s,
                          TYPE_NRF52840_TIMER, NRF52840_PERIPHERAL_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, timer_expire, s);
}

static void nrf52840_timer_reset(DeviceState *dev)
{
    NRF52840TimerState *s = NRF52840_TIMER(dev);

    timer_del(&s->timer);
    s->timer_start_ns = 0x00;
    s->update_counter_ns = 0x00;
    s->counter = 0x00;
    s->running = false;

    memset(s->events_compare, 0x00, sizeof(s->events_compare));
    memset(s->cc, 0x00, sizeof(s->cc));

    s->shorts = 0x00;
    s->inten = 0x00;
    s->mode = 0x00;
    s->bitmode = 0x00;
    s->prescaler = 0x00;
}

static int nrf52840_timer_post_load(void *opaque, int version_id)
{
    NRF52840TimerState *s = NRF52840_TIMER(opaque);

    if (s->running && s->mode == NRF52840_TIMER_TIMER) {
        timer_expire(s);
    }
    return 0;
}

static const VMStateDescription vmstate_nrf52840_timer = {
    .name = TYPE_NRF52840_TIMER,
    .version_id = 1,
    .post_load = nrf52840_timer_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER(timer, NRF52840TimerState),
        VMSTATE_INT64(timer_start_ns, NRF52840TimerState),
        VMSTATE_INT64(update_counter_ns, NRF52840TimerState),
        VMSTATE_UINT32(counter, NRF52840TimerState),
        VMSTATE_BOOL(running, NRF52840TimerState),
        VMSTATE_UINT8_ARRAY(events_compare, NRF52840TimerState,
                            NRF52840_TIMER_REG_COUNT),
        VMSTATE_UINT32_ARRAY(cc, NRF52840TimerState, NRF52840_TIMER_REG_COUNT),
        VMSTATE_UINT32(shorts, NRF52840TimerState),
        VMSTATE_UINT32(inten, NRF52840TimerState),
        VMSTATE_UINT32(mode, NRF52840TimerState),
        VMSTATE_UINT32(bitmode, NRF52840TimerState),
        VMSTATE_UINT32(prescaler, NRF52840TimerState),
        VMSTATE_END_OF_LIST()
    }
};

static Property nrf52840_timer_properties[] = {
    DEFINE_PROP_UINT8("id", NRF52840TimerState, id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void nrf52840_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = nrf52840_timer_reset;
    dc->vmsd = &vmstate_nrf52840_timer;
    device_class_set_props(dc, nrf52840_timer_properties);
}

static const TypeInfo nrf52840_timer_info = {
    .name = TYPE_NRF52840_TIMER,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NRF52840TimerState),
    .instance_init = nrf52840_timer_init,
    .class_init = nrf52840_timer_class_init
};

static void nrf52840_timer_register_types(void)
{
    type_register_static(&nrf52840_timer_info);
}

type_init(nrf52840_timer_register_types)
