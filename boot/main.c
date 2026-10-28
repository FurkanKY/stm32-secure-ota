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
#include "sota/proto.h"
#include "sota/update.h"

#define BOOT_VERSION "0.2.0"
#define BOOT_VERSION_PACKED 0x00020000u

/* After an update request from the application, give up waiting for the
 * host after this long without a valid frame and boot normally again. */
#define REQUEST_IDLE_MS 30000u

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

static void send_frame(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    console_send(data, len);
}

/* Serve the update protocol on the console UART. From here on the UART
 * carries binary frames only, so nothing else is printed. */
static __attribute__((noreturn)) void recovery(const char *why, sota_journal_t *journal,
                                               sota_bootstate_t *state, bool requested)
{
    static sota_update_t update;
    static sota_proto_t proto;
    uint32_t last_frame = board_millis();

    console_write("recovery: ");
    console_write(why);
    console_write(", waiting for an update\n");

    sota_update_init(&update, board_flash(), board_layout(), &keyring, journal, state);
    sota_proto_init(&proto, &update, BOOT_VERSION_PACKED, send_frame, NULL);

    for (;;) {
        int c = console_getc();

        if (c >= 0 && sota_proto_feed(&proto, (uint8_t)c)) {
            last_frame = board_millis();
            if (proto.reset_requested) {
                board_reset();
            }
        }
        if (requested && board_millis() - last_frame > REQUEST_IDLE_MS) {
            board_reset(); /* nobody came: the request flag is gone, boot normally */
        }
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
        recovery("boot state unreadable", &journal, &stored, false);
    }
    if (sota_handoff_take_request() == SOTA_REQUEST_UPDATE) {
        recovery("update requested by the application", &journal, &stored, true);
    }

    sota_boot_decide(&stored, err == SOTA_OK, probe_slot, (void *)layout, &d);
    if (d.changed && sota_journal_append(&journal, &d.state) != SOTA_OK) {
        console_write("warning: could not save boot state\n");
    }
    if (d.slot == SOTA_RECOVERY) {
        recovery("no bootable image", &journal, &d.state, false);
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
