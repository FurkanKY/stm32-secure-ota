/*
 * The handful of STM32F4 and Cortex-M4 registers this project touches,
 * taken from RM0090 and the Cortex-M4 Generic User Guide. Written out by hand
 * to keep the port small and self-contained.
 */
#ifndef STM32F4_REGS_H
#define STM32F4_REGS_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* RCC */
#define RCC_BASE        0x40023800u
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30u)
#define RCC_APB1ENR     REG32(RCC_BASE + 0x40u)
#define RCC_CSR         REG32(RCC_BASE + 0x74u)
#define RCC_AHB1ENR_GPIOAEN (1u << 0)
#define RCC_APB1ENR_USART2EN (1u << 17)
#define RCC_CSR_RMVF    (1u << 24)
#define RCC_CSR_PINRSTF (1u << 26)
#define RCC_CSR_PORRSTF (1u << 27)
#define RCC_CSR_SFTRSTF (1u << 28)
#define RCC_CSR_IWDGRSTF (1u << 29)

/* GPIOA */
#define GPIOA_BASE      0x40020000u
#define GPIOA_MODER     REG32(GPIOA_BASE + 0x00u)
#define GPIOA_AFRL      REG32(GPIOA_BASE + 0x20u)

/* USART2 (PA2 TX, PA3 RX, AF7; the ST-Link virtual COM port on Nucleo boards) */
#define USART2_BASE     0x40004400u
#define USART2_SR       REG32(USART2_BASE + 0x00u)
#define USART2_DR       REG32(USART2_BASE + 0x04u)
#define USART2_BRR      REG32(USART2_BASE + 0x08u)
#define USART2_CR1      REG32(USART2_BASE + 0x0Cu)
#define USART_SR_ORE    (1u << 3)
#define USART_SR_RXNE   (1u << 5)
#define USART_SR_TC     (1u << 6)
#define USART_SR_TXE    (1u << 7)
#define USART_CR1_RE    (1u << 2)
#define USART_CR1_TE    (1u << 3)
#define USART_CR1_UE    (1u << 13)

/* GPIOB (CAN1 on PB8 RX / PB9 TX, AF9) */
#define GPIOB_BASE      0x40020400u
#define GPIOB_MODER     REG32(GPIOB_BASE + 0x00u)
#define GPIOB_AFRH      REG32(GPIOB_BASE + 0x24u)
#define RCC_AHB1ENR_GPIOBEN (1u << 1)
#define RCC_APB1ENR_CAN1EN (1u << 25)

/* bxCAN 1 (RM0090 section 32) */
#define CAN1_BASE       0x40006400u
#define CAN_MCR         REG32(CAN1_BASE + 0x000u)
#define CAN_MSR         REG32(CAN1_BASE + 0x004u)
#define CAN_TSR         REG32(CAN1_BASE + 0x008u)
#define CAN_RF0R        REG32(CAN1_BASE + 0x00Cu)
#define CAN_BTR         REG32(CAN1_BASE + 0x01Cu)
#define CAN_TIR(n)      REG32(CAN1_BASE + 0x180u + 0x10u * (n))
#define CAN_TDTR(n)     REG32(CAN1_BASE + 0x184u + 0x10u * (n))
#define CAN_TDLR(n)     REG32(CAN1_BASE + 0x188u + 0x10u * (n))
#define CAN_TDHR(n)     REG32(CAN1_BASE + 0x18Cu + 0x10u * (n))
#define CAN_RI0R        REG32(CAN1_BASE + 0x1B0u)
#define CAN_RDT0R       REG32(CAN1_BASE + 0x1B4u)
#define CAN_RDL0R       REG32(CAN1_BASE + 0x1B8u)
#define CAN_RDH0R       REG32(CAN1_BASE + 0x1BCu)
#define CAN_FMR         REG32(CAN1_BASE + 0x200u)
#define CAN_FM1R        REG32(CAN1_BASE + 0x204u)
#define CAN_FS1R        REG32(CAN1_BASE + 0x20Cu)
#define CAN_FFA1R       REG32(CAN1_BASE + 0x214u)
#define CAN_FA1R        REG32(CAN1_BASE + 0x21Cu)
#define CAN_F0R1        REG32(CAN1_BASE + 0x240u)
#define CAN_F0R2        REG32(CAN1_BASE + 0x244u)
#define CAN_MCR_INRQ    (1u << 0)
#define CAN_MCR_SLEEP   (1u << 1)
#define CAN_MCR_TXFP    (1u << 2)
#define CAN_MCR_ABOM    (1u << 6)
#define CAN_MSR_INAK    (1u << 0)
#define CAN_TSR_TME0    (1u << 26)
#define CAN_TIR_TXRQ    (1u << 0)
#define CAN_RF0R_FMP0   (3u << 0)
#define CAN_RF0R_RFOM0  (1u << 5)
#define CAN_RIR_IDE     (1u << 2)
#define CAN_FMR_FINIT   (1u << 0)

/* Flash interface */
#define FLASH_IF_BASE   0x40023C00u
#define FLASH_ACR       REG32(FLASH_IF_BASE + 0x00u)
#define FLASH_KEYR      REG32(FLASH_IF_BASE + 0x04u)
#define FLASH_SR        REG32(FLASH_IF_BASE + 0x0Cu)
#define FLASH_CR        REG32(FLASH_IF_BASE + 0x10u)
#define FLASH_KEY1      0x45670123u
#define FLASH_KEY2      0xCDEF89ABu
#define FLASH_ACR_PRFTEN (1u << 8)
#define FLASH_ACR_ICEN  (1u << 9)
#define FLASH_ACR_DCEN  (1u << 10)
#define FLASH_ACR_ICRST (1u << 11)
#define FLASH_ACR_DCRST (1u << 12)
#define FLASH_SR_EOP    (1u << 0)
#define FLASH_SR_OPERR  (1u << 1)
#define FLASH_SR_WRPERR (1u << 4)
#define FLASH_SR_PGAERR (1u << 5)
#define FLASH_SR_PGPERR (1u << 6)
#define FLASH_SR_PGSERR (1u << 7)
#define FLASH_SR_BSY    (1u << 16)
#define FLASH_SR_ERRORS (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_PGPERR | FLASH_SR_PGSERR)
#define FLASH_CR_PG     (1u << 0)
#define FLASH_CR_SER    (1u << 1)
#define FLASH_CR_SNB_POS 3u
#define FLASH_CR_PSIZE_X32 (2u << 8)
#define FLASH_CR_STRT   (1u << 16)
#define FLASH_CR_LOCK   (1u << 31)

/* Independent watchdog, clocked by the ~32 kHz LSI */
#define IWDG_BASE       0x40003000u
#define IWDG_KR         REG32(IWDG_BASE + 0x00u)
#define IWDG_PR         REG32(IWDG_BASE + 0x04u)
#define IWDG_RLR        REG32(IWDG_BASE + 0x08u)
#define IWDG_SR         REG32(IWDG_BASE + 0x0Cu)
#define IWDG_KEY_RELOAD 0xAAAAu
#define IWDG_KEY_ACCESS 0x5555u
#define IWDG_KEY_START  0xCCCCu

/* Cortex-M4 system control */
#define SYST_CSR        REG32(0xE000E010u)
#define SYST_RVR        REG32(0xE000E014u)
#define SYST_CVR        REG32(0xE000E018u)
#define NVIC_ICER(n)    REG32(0xE000E180u + 4u * (n))
#define NVIC_ICPR(n)    REG32(0xE000E280u + 4u * (n))
#define SCB_ICSR        REG32(0xE000ED04u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_AIRCR       REG32(0xE000ED0Cu)
#define SCB_CPACR       REG32(0xE000ED88u)
#define SCB_DEMCR       REG32(0xE000EDFCu)
#define DWT_CTRL        REG32(0xE0001000u)
#define DWT_CYCCNT      REG32(0xE0001004u)
#define SCB_AIRCR_RESET 0x05FA0004u
#define SCB_ICSR_PENDSTCLR (1u << 25)
#define DEMCR_TRCENA    (1u << 24)

#endif /* STM32F4_REGS_H */
