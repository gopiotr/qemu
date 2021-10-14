/*
 * nRF52840 System-on-Chip RTC peripheral
 */
#ifndef NRF52840_RTC_H
#define NRF52840_RTC_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"
#define TYPE_NRF52840_RTC "nrf52840_soc.rtc"
OBJECT_DECLARE_SIMPLE_TYPE(NRF52840RTCState, NRF52840_RTC)

#define NRF52840_RTC_REG_COUNT 4

#define NRF52840_RTC_TASK_START 0x000
#define NRF52840_RTC_TASK_STOP 0x004
#define NRF52840_RTC_TASK_CLEAR 0x008
#define NRF52840_RTC_TASK_TRIGOVRFLW 0x00C

#define NRF52840_RTC_EVENT_TICK 0x100
#define NRF52840_RTC_EVENT_OVRFLW 0x104

#define NRF52840_RTC_EVENT_COMPARE_0 0x140
#define NRF52840_RTC_EVENT_COMPARE_1 0x144
#define NRF52840_RTC_EVENT_COMPARE_2 0x148
#define NRF52840_RTC_EVENT_COMPARE_3 0x14C

#define NRF52840_RTC_REG_INTENSET 0x304
#define NRF52840_RTC_REG_INTENCLR 0x308
#define NRF52840_RTC_REG_INTEN_MASK 0xF0003

#define NRF52840_RTC_REG_EVTEN 0x340
#define NRF52840_RTC_REG_EVTENSET 0x344
#define NRF52840_RTC_REG_EVTENCLR 0x348

#define NRF52840_RTC_REG_COUNTER 0x504
#define NRF52840_RTC_REG_PRESCALER 0x508
#define NRF52840_RTC_REG_PRESCALER_MASK 0xFFF

#define NRF52840_RTC_REG_CC0 0x540
#define NRF52840_RTC_REG_CC1 0x544
#define NRF52840_RTC_REG_CC2 0x548
#define NRF52840_RTC_REG_CC3 0x54C

struct NRF52840RTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint8_t id;
    QEMUTimer timer;
    int64_t update_counter_ns;
    uint32_t counter;

    bool running;

    uint8_t events_compare[NRF52840_RTC_REG_COUNT];
    uint32_t cc[NRF52840_RTC_REG_COUNT];
    uint32_t inten;
    uint32_t prescaler;

};


#endif
