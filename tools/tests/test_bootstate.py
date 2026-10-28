from otatool.bootstate import (
    BAD,
    CONFIRMED,
    PENDING,
    RECORD_SIZE,
    BootState,
    decode_journal,
    encode_record,
)

ERASED = b"\xff" * RECORD_SIZE


def test_round_trip():
    state = BootState(CONFIRMED, PENDING, active=1, attempts=2, security_floor=9)
    assert decode_journal(encode_record(state, seq=3)) == (3, state)


def test_newest_record_wins_across_pages():
    old = BootState(CONFIRMED, BAD)
    new = BootState(BAD, CONFIRMED, active=1)
    page0 = encode_record(old, seq=1) + ERASED
    page1 = encode_record(new, seq=2) + ERASED
    assert decode_journal(page0, page1) == (2, new)


def test_corrupted_record_ignored():
    good = BootState(CONFIRMED, PENDING)
    torn = bytearray(encode_record(BootState(BAD, BAD), seq=2))
    torn[10] ^= 0x01
    assert decode_journal(encode_record(good, seq=1) + bytes(torn)) == (1, good)


def test_blank_journal():
    assert decode_journal(ERASED * 4) is None
