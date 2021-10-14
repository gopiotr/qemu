/*
 * Nordic Semiconductor nRF52840
 */

#ifndef NRF52840_SOC_H
#define NRF52840_SOC_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"
#include "hw/char/nrf52840_uart.h"
#include "hw/misc/nrf52840_rng.h"
//#include "hw/gpio/nrf52840_gpio.h"
#include "hw/nvram/nrf52840_nvm.h"
#include "hw/rtc/nrf52840_rtc.h"
#include "hw/timer/nrf52840_timer.h"
#include "hw/misc/nrf52840_clock.h"
#include "qom/object.h"

#define TYPE_NRF52840_SOC "nrf52840-soc"
OBJECT_DECLARE_SIMPLE_TYPE(NRF52840State, NRF52840_SOC)

#define NRF52840_NUM_TIMERS 3
#define NRF52840_NUM_RTCS 3

struct NRF52840State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMv7MState cpu;

    NRF52840UARTState uart;
    NRF52840RNGState rng;
    NRF52840NVMState nvm;
    //NRF52840GPIOState gpio;
    NRF52840TimerState timer[NRF52840_NUM_TIMERS];
    NRF52840RTCState rtc[NRF52840_NUM_RTCS];
    NRF52840CLOCKState clock;

    MemoryRegion iomem;
    MemoryRegion sram;
    MemoryRegion flash;
    //MemoryRegion twi;

    uint32_t sram_size;
    uint32_t flash_size;

    MemoryRegion *board_memory;

    MemoryRegion container;

};

#endif
