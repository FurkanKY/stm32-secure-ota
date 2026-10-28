/*
 * Secure bootloader: pick a slot with the boot core policy, verify it, start
 * the watchdog and jump. Everything that decides *what* to boot lives in
 * lib/ and is unit-tested on the host; this file only wires it to the board.
 */
#include "board.h"
#include "boot_keys.h"
#include "sota/boot.h"
#include "sota/bootstate.h"
#include "sota/handoff.h"
#include "sota/image.h"

#define BOOT_VERSION "0.1.0"

/* Time the application gets to start kicking the watchdog. */
#define APP_WATCHDOG_MS 4000u

static const char *const slot_names[SOTA_NUM_SLOTS] = {"A", "B"};

static sota_keyring_t keyring;

static void print_reset_cause(uint32_t cause)
{
    console_write(cause & BOARD_RESET_WATCHDOG   ? "watchdog"
                  : cause & BOARD_RESET_SOFTWARE ? "software"
                  : cause & BOARD_RESET_POWER    ? "power-on"
                  : cause & BOARD_RESET_PIN      ? "pin"
                                                 : "unknown");
}

static void probe_slot(void *ctx, unsigned slot, sota_slot_info_t *info)
{
    const sota_layout_t *layout = ctx;
    sota_image_header_t hdr;
    uint32_t start = board_cycles();
    sota_err_t err = sota_image_verify(board_flash(), &layout->slot[slot], &keyring, &hdr);
    uint32_t cycles = board_cycles() - start;

    console_write("slot ");
    console_write(slot_names[slot]);
    if (err != SOTA_OK) {
        console_write(": no valid image (error ");
        console_dec((uint32_t)-err);
        console_write(")\n");
        return;
    }
    console_write(": v");
    console_version(hdr.version);
    console_write(" counter ");
    console_dec(hdr.security_counter);
    console_write(", ");
    console_dec(hdr.payload_size);
    console_write(" bytes, verified in ");
    console_dec(cycles / (board_cpu_hz() / 1000u));
    console_write(" ms (");
    console_dec(cycles);
    console_write(" cycles)\n");

    info->valid = true;
    info->security_counter = hdr.security_counter;
}

static __attribute__((noreturn)) void recovery(const char *why)
{
    console_write("recovery: ");
    console_write(why);
    console_write(", waiting for an update\n");
    /* The update protocol is added in M4. */
    for (;;) {
    }
}

int main(void)
{
    const sota_layout_t *layout = board_layout();
    sota_journal_t journal;
    sota_bootstate_t stored;
    sota_decision_t d;

    board_init();
    keyring.keys = boot_pubkeys;
    keyring.count = boot_pubkeys_count;

    uint32_t cause = board_reset_cause();
    console_write("\nsota-boot " BOOT_VERSION ", reset: ");
    print_reset_cause(cause);
    console_putc('\n');

    sota_err_t err = sota_journal_open(&journal, board_flash(), layout, &stored);
    if (err != SOTA_OK && err != SOTA_E_NO_STATE) {
        recovery("boot state unreadable");
    }
    if (sota_handoff_take_request() == SOTA_REQUEST_UPDATE) {
        recovery("update requested by the application");
    }

    sota_boot_decide(&stored, err == SOTA_OK, probe_slot, (void *)layout, &d);
    if (d.changed && sota_journal_append(&journal, &d.state) != SOTA_OK) {
        console_write("warning: could not save boot state\n");
    }
    if (d.slot == SOTA_RECOVERY) {
        recovery("no bootable image");
    }

    unsigned slot = (unsigned)d.slot;
    bool trial = d.state.slot[slot] == SOTA_SLOT_PENDING;

    console_write("booting slot ");
    console_write(slot_names[slot]);
    if (trial) {
        console_write(" (trial ");
        console_dec(d.state.attempts);
        console_putc('/');
        console_dec(SOTA_MAX_BOOT_ATTEMPTS);
        console_putc(')');
    }
    console_putc('\n');

    _sota_handoff.magic = SOTA_HANDOFF_MAGIC;
    _sota_handoff.boot_slot = slot;
    _sota_handoff.boot_attempt = trial ? d.state.attempts : 0;
    _sota_handoff.reset_cause = cause;

    board_watchdog_start(APP_WATCHDOG_MS);
    board_jump(layout->slot[slot].base + SOTA_HEADER_SIZE);
}
