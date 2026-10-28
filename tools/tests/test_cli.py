import json

import pytest

from otatool.cli import main

from conftest import fake_firmware


@pytest.fixture
def keyfile(tmp_path):
    path = tmp_path / "key.pem"
    assert main(["keygen", str(path)]) == 0
    return path


def sign(tmp_path, keyfile, layout_path, slot, binary, *extra):
    out = tmp_path / f"app_{slot}.signed.bin"
    code = main(["sign", str(binary), "-o", str(out), "--key", str(keyfile),
                 "--layout", str(layout_path), "--slot", slot,
                 "--version", "1.0.0", *extra])
    return code, out


def test_keygen_refuses_to_overwrite(keyfile, capsys):
    assert main(["keygen", str(keyfile)]) == 1
    assert "exists" in capsys.readouterr().err


def test_sign_and_inspect_both_slots(tmp_path, keyfile, layout, layout_path, capsys):
    for slot in "AB":
        binary = tmp_path / f"app_{slot}.bin"
        binary.write_bytes(fake_firmware(layout.load_addr(slot)))
        code, out = sign(tmp_path, keyfile, layout_path, slot, binary,
                         "--security-counter", "2")
        assert code == 0
        capsys.readouterr()

        assert main(["inspect", str(out), "--key", str(keyfile),
                     "--layout", str(layout_path), "--json"]) == 0
        info = json.loads(capsys.readouterr().out)
        assert info["slot"] == slot
        assert info["verified"] is True
        assert info["security_counter"] == 2


def test_sign_rejects_binary_for_wrong_slot(tmp_path, keyfile, layout, layout_path, capsys):
    binary = tmp_path / "app.bin"
    binary.write_bytes(fake_firmware(layout.load_addr("A")))
    code, _ = sign(tmp_path, keyfile, layout_path, "B", binary)
    assert code == 1
    assert "different slot" in capsys.readouterr().err


def test_inspect_reports_tampering(tmp_path, keyfile, layout, layout_path, capsys):
    binary = tmp_path / "app.bin"
    binary.write_bytes(fake_firmware(layout.load_addr("A")))
    _, out = sign(tmp_path, keyfile, layout_path, "A", binary)
    data = bytearray(out.read_bytes())
    data[-1] ^= 0xFF
    out.write_bytes(data)
    capsys.readouterr()
    assert main(["inspect", str(out), "--key", str(keyfile), "--json"]) == 2
    assert json.loads(capsys.readouterr().out)["verified"] is False


def test_pubkey_c_export(tmp_path, keyfile):
    c_file = tmp_path / "keys.c"
    assert main(["pubkey", str(keyfile), "--c", str(c_file)]) == 0
    text = c_file.read_text()
    assert "const uint8_t boot_pubkeys[][32]" in text
    assert text.count("0x") == 32


def test_layout_slots(layout):
    assert layout.load_addr("A") == 0x08020200
    assert layout.load_addr("b") == 0x08080200
    assert layout.max_payload("A") == 0x60000 - 0x200
