/*
 * nRF52840 Random Number Generator
 *
 * QEMU interface:
 * + Property "period_unfiltered_us": Time between two biased values in
 *   microseconds.
 * + Property "period_filtered_us": Time between two unbiased values in
 *   microseconds.
 * + sysbus MMIO regions 0: Memory Region with tasks, events and registers
 *   to be mapped to the peripherals instance address by the SOC.
 * + Named GPIO output "irq": Interrupt line of the peripheral. Must be
 *   connected to the associated peripheral interrupt line of the NVIC.
 * + Named GPIO output "eep_valrdy": Event set when new random value is ready
 *   to be read.
 * + Named GPIO input "tep_start": Task that triggers start of continuous
 *   generation of random values.
 * + Named GPIO input "tep_stop": Task that ends continuous generation of
 *   random values.
 *
 * Accuracy of the peripheral model:
 * + Stochastic properties of different configurations of the random source
 *   are not modeled.
 * + Generation of unfiltered and filtered random values take at least the
 *   average generation time stated in the production specification;
 *   non-deterministic generation times are not modeled.
 * 
 */

#ifndef NRF52840_RNG_H
#define NRF52840_RNG_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"
#define TYPE_NRF52840_RNG "nrf52840_soc.rng"
OBJECT_DECLARE_SIMPLE_TYPE(NRF52840RNGState, NRF52840_RNG)

#define NRF52840_RNG_SIZE                   0x1000

#define NRF52840_RNG_TASK_START             0x000
#define NRF52840_RNG_TASK_STOP              0x004
#define NRF52840_RNG_EVENT_VALRDY           0x100
#define NRF52840_RNG_REG_SHORTS             0x200
#define NRF52840_RNG_REG_SHORTS_VALRDY_STOP 0
#define NRF52840_RNG_REG_INTEN              0x300
#define NRF52840_RNG_REG_INTEN_VALRDY       0
#define NRF52840_RNG_REG_INTENSET           0x304
#define NRF52840_RNG_REG_INTENCLR           0x308
#define NRF52840_RNG_REG_CONFIG             0x504
#define NRF52840_RNG_REG_CONFIG_DECEN       0
#define NRF52840_RNG_REG_VALUE              0x508

struct NRF52840RNGState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    /* Event End Points */
    qemu_irq eep_valrdy;

    QEMUTimer timer;

    /* Time between generation of successive unfiltered values in us */
    uint16_t period_unfiltered_us;
    /* Time between generation of successive filtered values in us */
    uint16_t period_filtered_us;

    uint8_t value;

    uint32_t active;
    uint32_t event_valrdy;
    uint32_t shortcut_stop_on_valrdy;
    uint32_t interrupt_enabled;
    uint32_t filter_enabled;

};


#endif /* NRF52840_RNG_H */
