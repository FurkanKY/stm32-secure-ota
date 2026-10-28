/* Reset handler and vector table for STM32F40x/41x (82 peripheral IRQs). */
#include <stdint.h>
#include <string.h>

#include "stm32f4_regs.h"

#define NUM_IRQS 82

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

__attribute__((noreturn)) void Reset_Handler(void);
void Default_Handler(void);

void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t vector_table[16 + NUM_IRQS] = {
    [0] = (vector_t)(uintptr_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [11] = SVC_Handler,
    [12] = DebugMon_Handler,
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 ... 16 + NUM_IRQS - 1] = Default_Handler,
};

void Reset_Handler(void)
{
    memcpy(&_sdata, &_sidata, (size_t)((uintptr_t)&_edata - (uintptr_t)&_sdata));
    memset(&_sbss, 0, (size_t)((uintptr_t)&_ebss - (uintptr_t)&_sbss));

    /* Full access to the FPU (CP10, CP11) before any code may use it. */
    SCB_CPACR |= 0xFu << 20;
    __asm volatile("dsb\n isb");

    main();
    for (;;) {
    }
}

void Default_Handler(void)
{
    /* Unexpected exception: spin and let the watchdog (if running) reset us. */
    for (;;) {
    }
}
