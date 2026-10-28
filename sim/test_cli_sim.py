"""The otatool command line against the simulated board, over Renode's TCP UART."""

from conftest import REPO

from otatool.cli import main
from renode import Renode

LAYOUT = str(REPO / "boards" / "stm32f407vg" / "layout.json")


def test_otatool_update_and_info_over_tcp(build_dir, capsys):
    renode = Renode(build_dir)
    try:
        renode.setup(factory="app_v1_a", connect_uart=False)
        renode.start()
        port = f"tcp:127.0.0.1:{renode.uart_port}"
        # One image per slot: the tool must pick the one for the target slot (B).
        images = [str(build_dir / "app" / f"{n}.signed.bin") for n in ("app_v3_a", "app_v2_b")]

        assert main(["update", "--port", port, "--layout", LAYOUT, *images]) == 0
        out = capsys.readouterr().out
        assert "using" in out and "app_v2_b.signed.bin for slot B" in out
        assert "rebooting into trial boot" in out

        # The new image confirms itself; asking again shows the new state.
        assert main(["info", "--port", port, "--reset"]) == 0
        out = capsys.readouterr().out
        assert "slot A      confirmed  v1.0.0" in out
        assert "slot B      confirmed  v2.0.0     active" in out
    finally:
        renode.close()
