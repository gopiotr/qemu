/*
 * Nordic Semiconductor nRF52840 non-volatile memory
 *
 * It provides an interface to erase regions in flash memory.
 * Furthermore it provides the user and factory information registers.
 *
 * QEMU interface:
 * + sysbus MMIO regions 0: NVMC peripheral registers
 * + sysbus MMIO regions 1: FICR peripheral registers
 * + sysbus MMIO regions 2: UICR peripheral registers
 * + flash-size property: flash size in bytes.
 *
 * Accuracy of the peripheral model:
 * + Code regions (MPU configuration) are disregarded.
 * 
 */
#ifndef NRF52840_NVM_H
#define NRF52840_NVM_H

#include "hw/sysbus.h"
#include "qom/object.h"
#define TYPE_NRF52840_NVM "nrf52840_soc.nvm"
OBJECT_DECLARE_SIMPLE_TYPE(NRF52840NVMState, NRF52840_NVM)

#define NRF52840_FICR_FIXTURE_SIZE 776
#define NRF52840_UICR_FIXTURE_SIZE 352

#define NRF52840_NVMC_SIZE         0x1000

#define NRF52840_UICR_PSELRESET_0  0x200
#define NRF52840_UICR_PSELRESET_1  0x204

#define NRF52840_NVMC_READY        0x400
#define NRF52840_NVMC_READY_READY  0x01
#define NRF52840_NVMC_CONFIG       0x504
#define NRF52840_NVMC_CONFIG_MASK  0x03
#define NRF52840_NVMC_CONFIG_WEN   0x01
#define NRF52840_NVMC_CONFIG_EEN   0x02
#define NRF52840_NVMC_ERASEPCR1    0x508
#define NRF52840_NVMC_ERASEPCR0    0x510
#define NRF52840_NVMC_ERASEALL     0x50C
#define NRF52840_NVMC_ERASEUICR    0x514
#define NRF52840_NVMC_ERASE        0x01

#define NRF52840_UICR_SIZE         0x100

struct NRF52840NVMState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion ficr;
    MemoryRegion uicr;
    MemoryRegion flash;

    uint32_t ficr_content[NRF52840_FICR_FIXTURE_SIZE];
    uint32_t uicr_content[NRF52840_UICR_FIXTURE_SIZE];
    uint32_t flash_size;
    uint8_t *storage;

    uint32_t config;

};


#endif
