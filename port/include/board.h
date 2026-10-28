/*
 * What the bootloader and the demo application need from a board.
 * port/<family>/ implements it; nothing above this line knows register names.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stddef.h>
#include <stdint.h>

#include "sota/common.h"

#define BOARD_RESET_POWER    (1u << 0)
#define BOARD_RESET_PIN      (1u << 1)
#define BOARD_RESET_SOFTWARE (1u << 2)
#define BOARD_RESET_WATCHDOG (1u << 3)

/* Clocks, flash wait states, 1 ms tick, console UART at 115200 8N1. */
void board_init(void);

uint32_t board_millis(void);
uint32_t board_cpu_hz(void);
/* Free-running CPU cycle counter, for measurements. */
uint32_t board_cycles(void);

/* BOARD_RESET_* flags for the last reset. Reading clears the hardware flags,
 * so only the bootloader should call it. */
uint32_t board_reset_cause(void);

__attribute__((noreturn)) void board_reset(void);

/* Once started, only a reset stops the watchdog. The bootloader starts it
 * right before jumping, so an application that hangs before it gets going
 * still ends up back in the bootloader. */
void board_watchdog_start(uint32_t timeout_ms);
void board_watchdog_kick(void);

/* Quiesce interrupts and start the image whose vector table is at vtor. */
__attribute__((noreturn)) void board_jump(uint32_t vtor);

/* Classic CAN, 500 kbit/s, 11-bit ids, polled. Only frames with accept_id
 * are received. board_can_send returns nonzero if no mailbox is free. */
void board_can_init(uint32_t accept_id);
int board_can_send(uint32_t id, const uint8_t data[8]);
bool board_can_recv(uint32_t *id, uint8_t data[8], uint8_t *dlc);

const sota_flash_t *board_flash(void);
const sota_layout_t *board_layout(void);

/* Console (polled). console_getc returns -1 when nothing was received.
 * console_putc turns '\n' into "\r\n"; console_send passes bytes unchanged,
 * for the update protocol. */
void console_putc(char c);
void console_send(const uint8_t *data, size_t len);
void console_flush(void);
int console_getc(void);
void console_write(const char *s);
void console_dec(uint32_t v);
void console_hex(uint32_t v);
void console_version(uint32_t packed);

#endif /* BOARD_H */
