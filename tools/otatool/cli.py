"""Command line entry point: `otatool <command> ...`."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from . import __version__, keys
from .image import (
    HEADER_SIZE,
    ImageError,
    ImageHeader,
    check_vector_table,
    pack_version,
    sign_image,
    unpack_version,
    verify_image,
)
from .layout import load_layout

PASSPHRASE_ENV = "OTATOOL_KEY_PASSPHRASE"


def _passphrase() -> bytes | None:
    value = os.environ.get(PASSPHRASE_ENV)
    return value.encode() if value else None


def cmd_keygen(args: argparse.Namespace) -> int:
    out = Path(args.output)
    if out.exists() and not args.force:
        print(f"error: {out} exists, pass --force to overwrite", file=sys.stderr)
        return 1
    key = keys.generate(out, _passphrase())
    print(f"wrote {out}")
    print(f"public key: {keys.raw_public(key.public_key()).hex()}")
    return 0


def cmd_pubkey(args: argparse.Namespace) -> int:
    public = keys.load_public(Path(args.key))
    if args.c:
        Path(args.c).write_text(keys.to_c_source(public), encoding="utf-8")
        print(f"wrote {args.c}")
    if args.raw:
        Path(args.raw).write_bytes(keys.raw_public(public))
        print(f"wrote {args.raw}")
    if not (args.c or args.raw):
        print(keys.raw_public(public).hex())
    return 0


def cmd_sign(args: argparse.Namespace) -> int:
    payload = Path(args.input).read_bytes()
    max_payload = None
    if args.layout:
        layout = load_layout(args.layout)
        if args.slot is None and args.load_addr is None:
            print("error: --layout needs --slot (or --load-addr)", file=sys.stderr)
            return 1
        load_addr = args.load_addr if args.load_addr is not None else layout.load_addr(args.slot)
        slot = layout.slot_for_addr(load_addr)
        if slot is None:
            print(f"error: 0x{load_addr:08X} is not the start of any slot", file=sys.stderr)
            return 1
        max_payload = layout.max_payload(slot.name)
        if not args.no_vector_check:
            check_vector_table(payload, load_addr, layout.ram_base, layout.ram_size)
    elif args.load_addr is not None:
        load_addr = args.load_addr
    else:
        print("error: give --layout/--slot or --load-addr", file=sys.stderr)
        return 1

    image = sign_image(
        payload,
        keys.load_private(Path(args.key), _passphrase()),
        load_addr=load_addr,
        version=pack_version(args.version),
        security_counter=args.security_counter,
        key_id=args.key_id,
        max_payload=max_payload,
    )
    Path(args.output).write_bytes(image)
    print(
        f"wrote {args.output}: {len(image) - HEADER_SIZE} byte payload at 0x{load_addr:08X}, "
        f"version {args.version}, security counter {args.security_counter}"
    )
    return 0


def cmd_inspect(args: argparse.Namespace) -> int:
    image = Path(args.image).read_bytes()
    header = ImageHeader.from_bytes(image)
    info = {
        "payload_size": header.payload_size,
        "load_addr": f"0x{header.load_addr:08X}",
        "version": unpack_version(header.version),
        "security_counter": header.security_counter,
        "key_id": header.key_id,
        "payload_hash": header.payload_hash.hex(),
        "signature": header.signature.hex(),
    }
    if args.layout:
        slot = load_layout(args.layout).slot_for_addr(header.load_addr)
        info["slot"] = slot.name if slot else None
    status = 0
    if args.key:
        try:
            verify_image(image, keys.load_public(Path(args.key)))
            info["verified"] = True
        except ImageError as exc:
            info["verified"] = False
            info["error"] = str(exc)
            status = 2

    if args.json:
        print(json.dumps(info, indent=2))
    else:
        width = max(len(k) for k in info)
        for name, value in info.items():
            print(f"{name:<{width}}  {value}")
    return status


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="otatool", description=__doc__)
    parser.add_argument("--version", action="version", version=f"otatool {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("keygen", help="generate an Ed25519 signing key")
    p.add_argument("output", help="private key PEM to write")
    p.add_argument("--force", action="store_true", help="overwrite an existing file")
    p.set_defaults(func=cmd_keygen)

    p = sub.add_parser("pubkey", help="export the public key (hex, raw or C source)")
    p.add_argument("key", help="private or public key file")
    p.add_argument("--c", metavar="FILE", help="write a C key table for the bootloader")
    p.add_argument("--raw", metavar="FILE", help="write the raw 32-byte key")
    p.set_defaults(func=cmd_pubkey)

    p = sub.add_parser("sign", help="wrap a raw binary in a signed header")
    p.add_argument("input", help="raw application binary (objcopy -O binary)")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--key", required=True, help="Ed25519 private key PEM")
    p.add_argument("--layout", help="boards/<board>/layout.json")
    p.add_argument("--slot", help="slot the binary was linked for (A or B)")
    p.add_argument("--load-addr", type=lambda s: int(s, 0), help="override the load address")
    p.add_argument("--version", required=True, help="MAJOR.MINOR.PATCH")
    p.add_argument("--security-counter", type=int, default=0)
    p.add_argument("--key-id", type=int, default=0)
    p.add_argument("--no-vector-check", action="store_true",
                   help="skip the vector table sanity check")
    p.set_defaults(func=cmd_sign)

    p = sub.add_parser("inspect", help="print and optionally verify an image header")
    p.add_argument("image")
    p.add_argument("--key", help="verify the signature and hash with this key")
    p.add_argument("--layout", help="also report which slot the image targets")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_inspect)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except (ImageError, ValueError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
