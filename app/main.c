/*
 * Demo application. It is built once per slot (the bootloader runs images in
 * place) and in a few variants to exercise the bootloader:
 *   APP_SLOT        0 for slot A, 1 for slot B
 *   APP_NO_CONFIRM  a "broken release" that hangs during its self-test
 *
 * A healthy image confirms itself after a short self-test while it is on
 * trial, then kicks the watchdog from its main loop.
 *
 * Console commands: i = info, r = reset, u = reboot into update mode.
 */
#include "board.h"
#include "sota/boot.h"
#include "sota/bootstate.h"
#include "sota/handoff.h"
#include "sota/image.h"

#ifndef APP_SLOT
#error "APP_SLOT must be 0 or 1"
#endif

#define SELF_TEST_MS 500u

static const sota_image_header_t *own_header(void)
{
    return (const sota_image_header_t *)board_layout()->slot[APP_SLOT].base;
}

static void wait_ms(uint32_t ms)
{
    uint32_t start = board_millis();

    while (board_millis() - start < ms) {
        board_watchdog_kick();
    }
}

static void print_info(void)
{
    const sota_image_header_t *hdr = own_header();

    console_write("app v");
    console_version(hdr->version);
    console_write(" in slot ");
    console_putc(APP_SLOT ? 'B' : 'A');
    console_write(", security counter ");
    console_dec(hdr->security_counter);
    if (_sota_handoff.magic == SOTA_HANDOFF_MAGIC && _sota_handoff.boot_attempt) {
        console_write(", trial boot ");
        console_dec(_sota_handoff.boot_attempt);
    }
    console_putc('\n');
}

static void confirm_if_on_trial(void)
{
    sota_journal_t journal;
    sota_bootstate_t state;

    if (sota_journal_open(&journal, board_flash(), board_layout(), &state) != SOTA_OK ||
        state.slot[APP_SLOT] != SOTA_SLOT_PENDING) {
        return;
    }

    console_write("self-test...\n");
    wait_ms(SELF_TEST_MS);
#ifdef APP_NO_CONFIRM
    console_write("self-test failed, not confirming (simulated bad release)\n");
    for (;;) {
        /* No watchdog kick: the bootloader gets control back. */
    }
#else
    if (sota_boot_confirm(&journal, &state, APP_SLOT, own_header()->security_counter) ==
        SOTA_OK) {
        console_write("image confirmed\n");
    } else {
        console_write("confirm failed\n");
    }
#endif
}

int main(void)
{
    board_init();
    print_info();
    confirm_if_on_trial();
    console_write("app running\n");

    for (;;) {
        board_watchdog_kick();
        switch (console_getc()) {
        case 'i':
            print_info();
            break;
        case 'r':
            console_write("resetting\n");
            board_reset();
        case 'u':
            console_write("rebooting into update mode\n");
            sota_handoff_request(SOTA_REQUEST_UPDATE);
            board_reset();
        default:
            break;
        }
    }
}
