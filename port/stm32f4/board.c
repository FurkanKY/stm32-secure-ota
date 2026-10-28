#include "board.h"

#include "sota_board_layout.h"
#include "stm32f4_regs.h"

/* Reset clock: 16 MHz HSI, no PLL. Fast enough for the bootloader and it
 * behaves the same in Renode and on hardware. */
#define CPU_HZ 16000000u
#define CONSOLE_BAUD 115200u

static volatile uint32_t ms_ticks;
static bool dwt_counts;

void SysTick_Handler(void)
{
    ms_ticks++;
}

void board_init(void)
{
    FLASH_ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN; /* 0 wait states */

    /* USART2 on PA2 (TX) / PA3 (RX), alternate function 7. */
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB1ENR |= RCC_APB1ENR_USART2EN;
    GPIOA_MODER = (GPIOA_MODER & ~(0xFu << 4)) | (0xAu << 4);
    GPIOA_AFRL = (GPIOA_AFRL & ~(0xFFu << 8)) | (0x77u << 8);
    USART2_BRR = (CPU_HZ + CONSOLE_BAUD / 2) / CONSOLE_BAUD;
    USART2_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    SYST_RVR = CPU_HZ / 1000u - 1u;
    SYST_CVR = 0;
    SYST_CSR = 0x7u; /* processor clock, interrupt, enable */

    SCB_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;
    uint32_t before = DWT_CYCCNT;
    for (volatile int i = 0; i < 16; i++) {
    }
    dwt_counts = DWT_CYCCNT != before; /* not every simulator models the DWT */

    __asm volatile("cpsie i" ::: "memory");
}

uint32_t board_millis(void)
{
    return ms_ticks;
}

uint32_t board_cpu_hz(void)
{
    return CPU_HZ;
}

uint32_t board_cycles(void)
{
    if (dwt_counts) {
        return DWT_CYCCNT;
    }
    /* Fallback: SysTick counts down once per CPU cycle within each 1 ms. */
    uint32_t ms, val;
    do {
        ms = ms_ticks;
        val = SYST_CVR;
    } while (ms != ms_ticks);
    return ms * (CPU_HZ / 1000u) + (SYST_RVR - val);
}

uint32_t board_reset_cause(void)
{
    uint32_t csr = RCC_CSR;
    uint32_t cause;

    /* NRST is pulsed by every reset source, so the pin flag comes last. */
    if (csr & RCC_CSR_IWDGRSTF) {
        cause = BOARD_RESET_WATCHDOG;
    } else if (csr & RCC_CSR_SFTRSTF) {
        cause = BOARD_RESET_SOFTWARE;
    } else if (csr & RCC_CSR_PORRSTF) {
        cause = BOARD_RESET_POWER;
    } else if (csr & RCC_CSR_PINRSTF) {
        cause = BOARD_RESET_PIN;
    } else {
        cause = 0;
    }
    RCC_CSR |= RCC_CSR_RMVF;
    return cause;
}

void board_reset(void)
{
    console_flush(); /* let the last bytes leave the UART */
    __asm volatile("dsb" ::: "memory");
    SCB_AIRCR = SCB_AIRCR_RESET;
    __asm volatile("dsb" ::: "memory");
    for (;;) {
    }
}

void board_watchdog_start(uint32_t timeout_ms)
{
    /* LSI (~32 kHz) / 64 = 500 Hz, so one count is 2 ms; max ~8 s. */
    uint32_t reload = timeout_ms / 2u;

    if (reload > 0xFFFu) {
        reload = 0xFFFu;
    }
    IWDG_KR = IWDG_KEY_START;
    IWDG_KR = IWDG_KEY_ACCESS;
    IWDG_PR = 4u;
    IWDG_RLR = reload;
    for (uint32_t spin = 0; (IWDG_SR & 0x3u) != 0 && spin < 100000u; spin++) {
    }
    IWDG_KR = IWDG_KEY_RELOAD;
}

void board_watchdog_kick(void)
{
    IWDG_KR = IWDG_KEY_RELOAD;
}

void board_jump(uint32_t vtor)
{
    const volatile uint32_t *vectors = (const volatile uint32_t *)vtor;
    uint32_t sp = vectors[0];
    uint32_t pc = vectors[1];

    /* Hand over a quiet core: no tick, nothing enabled or pending. */
    __asm volatile("cpsid i" ::: "memory");
    SYST_CSR = 0;
    SCB_ICSR = SCB_ICSR_PENDSTCLR;
    for (unsigned i = 0; i < 8; i++) {
        NVIC_ICER(i) = 0xFFFFFFFFu;
        NVIC_ICPR(i) = 0xFFFFFFFFu;
    }
    SCB_VTOR = vtor;
    __asm volatile("dsb\n isb" ::: "memory");

    __asm volatile(
        "msr msp, %0\n"
        "cpsie i\n"
        "bx %1\n"
        :
        : "r"(sp), "r"(pc)
        : "memory");
    __builtin_unreachable();
}

const sota_layout_t *board_layout(void)
{
    static const sota_layout_t layout = {
        .slot = {
            {SOTA_SLOT_A_BASE, SOTA_SLOT_A_SIZE},
            {SOTA_SLOT_B_BASE, SOTA_SLOT_B_SIZE},
        },
        .state_page = {
            {SOTA_STATE0_BASE, SOTA_STATE_PAGE_SIZE},
            {SOTA_STATE1_BASE, SOTA_STATE_PAGE_SIZE},
        },
    };
    return &layout;
}

static void uart_tx(uint8_t b)
{
    while (!(USART2_SR & USART_SR_TXE)) {
    }
    USART2_DR = b;
}

void console_putc(char c)
{
    if (c == '\n') {
        uart_tx('\r');
    }
    uart_tx((uint8_t)c);
}

void console_send(const uint8_t *data, size_t len)
{
    while (len--) {
        uart_tx(*data++);
    }
}

void console_flush(void)
{
    while (!(USART2_SR & USART_SR_TC)) {
    }
}

int console_getc(void)
{
    uint32_t sr = USART2_SR;

    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        return (int)(USART2_DR & 0xFFu); /* SR then DR read also clears ORE */
    }
    return -1;
}
