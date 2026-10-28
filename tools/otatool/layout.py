"""Board flash layout, read from boards/<board>/layout.json.

The same JSON file drives the linker scripts (via CMake), so host tools and
firmware cannot disagree about where a slot starts.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Slot:
    name: str
    base: int
    size: int


@dataclass(frozen=True)
class Layout:
    name: str
    flash_base: int
    header_size: int
    ram_base: int
    ram_size: int
    slots: dict[str, Slot]

    def slot(self, name: str) -> Slot:
        try:
            return self.slots[name.upper()]
        except KeyError:
            raise ValueError(
                f"board {self.name} has no slot {name!r} (have {', '.join(self.slots)})"
            ) from None

    def load_addr(self, slot: str) -> int:
        return self.slot(slot).base + self.header_size

    def max_payload(self, slot: str) -> int:
        return self.slot(slot).size - self.header_size

    def slot_for_addr(self, load_addr: int) -> Slot | None:
        for s in self.slots.values():
            if s.base + self.header_size == load_addr:
                return s
        return None


def _int(value: str | int) -> int:
    return value if isinstance(value, int) else int(value, 0)


def load_layout(path: str | Path) -> Layout:
    raw = json.loads(Path(path).read_text(encoding="utf-8"))
    flash_base = _int(raw["flash_base"])
    slots = {
        name.upper(): Slot(name.upper(), flash_base + _int(s["offset"]), _int(s["size"]))
        for name, s in raw["slots"].items()
    }
    return Layout(
        name=raw["name"],
        flash_base=flash_base,
        header_size=_int(raw["header_size"]),
        ram_base=_int(raw["ram"]["base"]),
        ram_size=_int(raw["ram"]["size"]),
        slots=slots,
    )
