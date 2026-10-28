/*
 * Polled bxCAN driver: CAN1 on PB8 (RX) / PB9 (TX), classic CAN at 500 kbit/s,
 * 11-bit identifiers, one acceptance filter feeding FIFO 0.
 */
#include <string.h>

#include "board.h"
#include "stm32f4_regs.h"

/* APB1 = 16 MHz HSI. Prescaler 2 gives 8 MHz time quanta; 1 + 13 + 2 = 16 tq
 * per bit = 500 kbit/s with the sample point at 87.5 %. */
#define CAN_BTR_500K ((0u << 24) | ((2u - 1u) << 20) | ((13u - 1u) << 16) | (2u - 1u))

/* Bounded wait so a missing transceiver cannot hang the bootloader. */
static void wait_msr(uint32_t mask, uint32_t value)
{
    for (uint32_t spin = 0; (CAN_MSR & mask) != value && spin < 1000000u; spin++) {
    }
}

void board_can_init(uint32_t accept_id)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC_APB1ENR |= RCC_APB1ENR_CAN1EN;
    GPIOB_MODER = (GPIOB_MODER & ~(0xFu << 16)) | (0xAu << 16); /* PB8, PB9: AF */
    GPIOB_AFRH = (GPIOB_AFRH & ~0xFFu) | 0x99u;                  /* AF9 = CAN1 */

    CAN_MCR = (CAN_MCR & ~CAN_MCR_SLEEP) | CAN_MCR_INRQ;
    wait_msr(CAN_MSR_INAK, CAN_MSR_INAK);
    CAN_BTR = CAN_BTR_500K;
    CAN_MCR = CAN_MCR_ABOM | CAN_MCR_TXFP; /* leave init, recover from bus-off */
    wait_msr(CAN_MSR_INAK, 0);

    /* Filter bank 0: 32-bit mask mode, exact standard id, data frames only. */
    CAN_FMR |= CAN_FMR_FINIT;
    CAN_FA1R &= ~1u;
    CAN_FS1R |= 1u;
    CAN_FM1R &= ~1u;
    CAN_FFA1R &= ~1u;
    CAN_F0R1 = accept_id << 21;
    CAN_F0R2 = (0x7FFu << 21) | CAN_RIR_IDE | (1u << 1); /* match id, IDE=0, RTR=0 */
    CAN_FA1R |= 1u;
    CAN_FMR &= ~CAN_FMR_FINIT;
}

int board_can_send(uint32_t id, const uint8_t data[8])
{
    uint32_t tsr = CAN_TSR;
    uint32_t lo, hi;

    for (unsigned box = 0; box < 3; box++) {
        if (tsr & (CAN_TSR_TME0 << box)) {
            memcpy(&lo, &data[0], 4);
            memcpy(&hi, &data[4], 4);
            CAN_TDTR(box) = 8;
            CAN_TDLR(box) = lo;
            CAN_TDHR(box) = hi;
            CAN_TIR(box) = (id << 21) | CAN_TIR_TXRQ;
            return 0;
        }
    }
    return -1; /* all three mailboxes busy */
}

bool board_can_recv(uint32_t *id, uint8_t data[8], uint8_t *dlc)
{
    if ((CAN_RF0R & CAN_RF0R_FMP0) == 0) {
        return false;
    }
    uint32_t rir = CAN_RI0R;
    uint32_t lo = CAN_RDL0R;
    uint32_t hi = CAN_RDH0R;

    *id = rir & CAN_RIR_IDE ? rir >> 3 : rir >> 21;
    *dlc = (uint8_t)(CAN_RDT0R & 0xFu);
    if (*dlc > 8) {
        *dlc = 8;
    }
    memcpy(&data[0], &lo, 4);
    memcpy(&data[4], &hi, 4);
    CAN_RF0R |= CAN_RF0R_RFOM0;
    return true;
}
