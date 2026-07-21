from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack a prelinked BBK 9588 binary")
    parser.add_argument("raw", type=Path)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--title", required=True)
    parser.add_argument("--category", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--icon", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if not args.raw.is_file():
        raise SystemExit(f"raw binary not found: {args.raw}")
    if args.icon is not None and not args.icon.is_file():
        raise SystemExit(f"icon PNG not found: {args.icon}")
    if not (args.sdk / "bda_packer" / "build.py").is_file():
        raise SystemExit(f"SDK checkout not found: {args.sdk}")

    sys.path.insert(0, str(args.sdk))
    from bda_packer.build import (  # pylint: disable=import-outside-toplevel
        ENTRY_OFFSET,
        ENTRY_VA,
        ICON_SPECS,
        ICON_SIZES,
        ICON_START,
        RUNTIME_FILE_BASE,
        build_icons,
    )
    from bda_packer.header import (  # pylint: disable=import-outside-toplevel
        BdaHeaderFields,
        write_header,
    )
    from bda_packer.validate import validate_bda  # pylint: disable=import-outside-toplevel
    from bda_packer.vx_icon import (  # pylint: disable=import-outside-toplevel
        make_vx,
        read_png,
        resize_cover,
        rgb565_bytes,
    )

    code = args.raw.read_bytes()
    data = bytearray(b"\0" * ENTRY_OFFSET)
    if args.icon is None:
        icons = build_icons(None, (0, 0, 0))
    else:
        source_width, source_height, source_pixels = read_png(args.icon)
        icon_blocks = []
        for width, height in ICON_SPECS:
            resized = resize_cover(
                source_width,
                source_height,
                source_pixels,
                width,
                height,
            )
            pixels = rgb565_bytes(
                resized,
                (0, 0, 0),
                transparent_key=(255, 0, 255),
            )
            icon_blocks.append(make_vx(width, height, pixels))
        icons = b"".join(icon_blocks)
    expected_icon_bytes = ENTRY_OFFSET - ICON_START
    if len(icons) != expected_icon_bytes:
        raise SystemExit(
            f"icon area size is 0x{len(icons):x}, expected 0x{expected_icon_bytes:x}"
        )
    data[ICON_START:ENTRY_OFFSET] = icons
    data.extend(code)
    data.extend(b"\0" * (-len(data) & 3))

    fields = BdaHeaderFields(
        category=args.category,
        file_size_minus_4=len(data) - 4,
        entry_offset=ENTRY_OFFSET,
        icon_start=ICON_START,
        icon0_size=ICON_SIZES[0],
        icon1_size=ICON_SIZES[1],
        icon2_size=ICON_SIZES[2],
        icon3_size=ICON_SIZES[3],
    )
    write_header(data, fields, args.title)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)

    report = validate_bda(args.output)
    if not report["ok"]:
        args.output.unlink(missing_ok=True)
        raise SystemExit("BDA validation failed: " + "; ".join(report["errors"]))

    print(f"output={args.output}")
    print(f"size=0x{len(data):x}")
    print(f"entry_offset=0x{ENTRY_OFFSET:x}")
    print(f"entry_va=0x{ENTRY_VA:x}")
    print(f"runtime_file_base=0x{RUNTIME_FILE_BASE:x}")
    print(f"icon={args.icon if args.icon is not None else 'default'}")
    print(f"checksum_ok={report['checksum_ok']}")


if __name__ == "__main__":
    main()
