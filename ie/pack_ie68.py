#!/usr/bin/env python3
"""
ie/pack_ie68.py - build the self-contained mariokart64.ie68 (PAK2 layout).

IE loads the file linearly at 0x1000 (file offset F -> mem 0x1000+F). Device
apertures swallow mem 0xA0000-0x5FFFFF and the guest stack grows down from
just below 0xFF0000, so the pack places:

    [ loader ]                file 0,        mem 0x1000
    [ zero pad across the aperture hole ]
    [ header (5 u32) + TOC ]  file 0x5FF000, mem 0x600000  (survives runtime)
    [ zero pad ]
    [ asset blobs ]           file 0xFFF000, mem 0x1000000 (high window)
    [ game image ]            (4-aligned, after the blobs)

The loader copies the game image to 0x10000000 and jumps; asset blobs are
served in place from the in-RAM TOC. All fields big-endian (68k).

Usage: pack_ie68.py <loader.ie68> <game.bin> <rom_assets_dir> <tnl.ie64|-> <out.ie68> [gfx.ie68|-] [audio.ie68|-]
"""
import os
import struct
import sys

from typing import List, Tuple

HDR_FOFF = 0x5FF000    # IE_PACK_HDR_FOFF  (mem 0x600000)
DATA_FOFF = 0xFFF000   # IE_PACK_DATA_FOFF (mem 0x1000000)
DATA_LIMIT_FOFF = 0x7FFF000  # IE_PACK_DATA_LIMIT_FOFF (mem 0x08000000)
LOAD_BASE = 0x1000       # IE linear-load base: memory = file offset + LOAD_BASE
# Keep in sync with IntuitionEngine/coprocessor_constants.go. Pack blobs must
# never occupy worker-private RAM or the shared coprocessor mailbox.
COPROC_RESERVED_RANGES = (
    (0x280000, 0x300000, "M68K worker RAM"),
    (0x320000, 0x3A0000, "x86 worker RAM"),
    (0x3A0000, 0x420000, "IE64 worker RAM"),
    (0x420000, 0x4A0000, "M68K worker 2 RAM"),
    (0x790000, 0x793000, "coprocessor mailbox"),
)
MAGIC0 = 0x4D4B3634    # "MK64"
MAGIC1 = 0x50414B32    # "PAK2"
NAME_LEN = 48
HEADER_FMT = ">IIIII"
ENTRY_FMT = ">%dsII" % NAME_LEN


def align4(n: int) -> int:
    return (n + 3) & ~3


def validate_blob_spans(entries: List[Tuple[str, int, int]]) -> None:
    """Reject TOC blobs whose linear-load memory spans hit coprocessor RAM."""
    for name, file_offset, size in entries:
        mem_start = LOAD_BASE + file_offset
        mem_end = mem_start + size
        for reserved_start, reserved_end, reserved_name in COPROC_RESERVED_RANGES:
            if mem_start < reserved_end and mem_end > reserved_start:
                raise ValueError(
                    "pack: blob %s memory span 0x%x-0x%x intersects %s 0x%x-0x%x"
                    % (name, mem_start, mem_end, reserved_name,
                       reserved_start, reserved_end)
                )


def main() -> int:
    if len(sys.argv) not in (6, 7, 8):
        sys.stderr.write(__doc__)
        return 2
    loader_path, game_path, assets_dir, svc_path, out_path = sys.argv[1:6]
    gfx_svc_path = sys.argv[6] if len(sys.argv) >= 7 else "-"
    audio_svc_path = sys.argv[7] if len(sys.argv) == 8 else "-"

    loader = open(loader_path, "rb").read()
    game = open(game_path, "rb").read()
    if len(loader) > HDR_FOFF:
        sys.stderr.write("pack: loader %d exceeds header offset 0x%x\n" % (len(loader), HDR_FOFF))
        return 3

    assets: List[Tuple[str, bytes]] = []
    for fn in sorted(os.listdir(assets_dir)):
        full = os.path.join(assets_dir, fn)
        if not os.path.isfile(full):
            continue
        if len(fn.encode()) >= NAME_LEN:
            sys.stderr.write("pack: asset name too long for TOC: %s\n" % fn)
            return 3
        assets.append((fn, open(full, "rb").read()))
    if svc_path != "-":
        assets.append(("svc/tnl.ie64", open(svc_path, "rb").read()))
    if gfx_svc_path != "-":
        assets.append(("svc/gfx.ie68", open(gfx_svc_path, "rb").read()))
    if audio_svc_path != "-":
        assets.append(("svc/audio.ie68", open(audio_svc_path, "rb").read()))

    # Lay out blobs then the game image in the high window.
    entries = []
    foff = DATA_FOFF
    for name, blob in assets:
        entries.append((name, foff, len(blob)))
        foff = align4(foff + len(blob))
    try:
        validate_blob_spans(entries)
    except ValueError as exc:
        sys.stderr.write(str(exc) + "\n")
        return 3
    game_off = foff

    toc = b"".join(struct.pack(ENTRY_FMT, n.encode(), o, s) for n, o, s in entries)
    header = struct.pack(HEADER_FMT, MAGIC0, MAGIC1, game_off, len(game), len(entries))
    if HDR_FOFF + len(header) + len(toc) > DATA_FOFF:
        sys.stderr.write("pack: header+TOC overflow the header window\n")
        return 3

    with open(out_path, "wb") as out:
        out.write(loader)
        out.write(b"\x00" * (HDR_FOFF - len(loader)))
        out.write(header)
        out.write(toc)
        out.write(b"\x00" * (DATA_FOFF - HDR_FOFF - len(header) - len(toc)))
        for (_, off, _), (name, blob) in zip(entries, assets):
            pos = out.tell()
            if pos != off:
                out.write(b"\x00" * (off - pos))
            out.write(blob)
        pos = out.tell()
        if pos != game_off:
            out.write(b"\x00" * (game_off - pos))
        out.write(game)

    total = game_off + len(game)
    if total > DATA_LIMIT_FOFF:
        sys.stderr.write("pack: image extends to 0x%x, beyond pack data limit 0x%x\n"
                         % (total, DATA_LIMIT_FOFF))
        return 3
    sys.stderr.write("pack: %s: %d assets, game %d bytes @0x%x, total %.1f MiB\n"
                     % (out_path, len(entries), len(game), game_off, total / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
