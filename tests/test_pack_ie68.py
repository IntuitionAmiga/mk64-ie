#!/usr/bin/env python3
import os
import struct
import subprocess
import sys
import tempfile
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKER = ROOT / "ie" / "pack_ie68.py"
HDR_FOFF = 0x5FF000
DATA_FOFF = 0xFFF000
DATA_LIMIT_FOFF = 0x7FFF000
HEADER_FMT = ">IIIII"
ENTRY_FMT = ">48sII"

SPEC = importlib.util.spec_from_file_location("pack_ie68", PACKER)
assert SPEC is not None and SPEC.loader is not None
PACK_MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK_MODULE)


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def run_pack(tmp: Path, game_size: int) -> subprocess.CompletedProcess:
    loader = tmp / "loader.ie68"
    game = tmp / "game.bin"
    assets = tmp / "assets"
    out = tmp / "mariokart64.ie68"

    write(loader, b"L" * 0x400)
    write(game, b"G" * game_size)
    write(assets / "mario_raceway_geography.bin", b"A" * 0x200)
    write(assets / "mario_raceway_tex.bin", b"B" * 0x300)
    return subprocess.run(
        [sys.executable, str(PACKER), str(loader), str(game), str(assets), "-", str(out)],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def test_pack_ranges_stay_below_texture_store(tmp: Path) -> None:
    result = run_pack(tmp, 0x1000)
    assert result.returncode == 0, result.stderr.decode()

    out = tmp / "mariokart64.ie68"
    data = out.read_bytes()
    magic0, magic1, game_off, game_size, toc_count = struct.unpack_from(
        HEADER_FMT, data, HDR_FOFF
    )
    assert (magic0, magic1) == (0x4D4B3634, 0x50414B32)
    assert toc_count == 2
    assert game_off + game_size <= DATA_LIMIT_FOFF

    pos = HDR_FOFF + struct.calcsize(HEADER_FMT)
    for _ in range(toc_count):
        raw_name, off, size = struct.unpack_from(ENTRY_FMT, data, pos)
        pos += struct.calcsize(ENTRY_FMT)
        assert raw_name.split(b"\0", 1)[0]
        assert DATA_FOFF <= off
        assert off + size <= DATA_LIMIT_FOFF


def test_pack_rejects_texture_store_overlap(tmp: Path) -> None:
    result = run_pack(tmp, DATA_LIMIT_FOFF - DATA_FOFF + 1)
    assert result.returncode != 0
    assert b"beyond pack data limit" in result.stderr


def test_pack_rejects_worker_ram_overlap() -> None:
    # File offset 0x27f000 linear-loads at the M68K worker base 0x280000.
    try:
        PACK_MODULE.validate_blob_spans([("svc/bad.ie68", 0x27F000, 4)])
    except ValueError as exc:
        assert "M68K worker RAM" in str(exc)
    else:
        raise AssertionError("worker-RAM overlap was accepted")


def test_pack_rejects_second_m68k_window_overlap() -> None:
    # File offset 0x41f000 linear-loads at the second M68K worker base 0x420000.
    try:
        PACK_MODULE.validate_blob_spans([("svc/bad2.ie68", 0x41F000, 4)])
    except ValueError as exc:
        assert "M68K worker 2 RAM" in str(exc)
    else:
        raise AssertionError("second M68K worker window overlap was accepted")


def test_pack_rejects_expanded_mailbox_overlap() -> None:
    # File offset 0x791000 linear-loads at 0x792000, inside the mailbox region
    # only after the 0x400-stride expansion (old end was 0x791800). Guards the
    # rings the new layout added past the retired end address.
    try:
        PACK_MODULE.validate_blob_spans([("svc/bad3.ie68", 0x791000, 4)])
    except ValueError as exc:
        assert "coprocessor mailbox" in str(exc)
    else:
        raise AssertionError("expanded-mailbox overlap was accepted")


def main() -> int:
    with tempfile.TemporaryDirectory() as d:
        test_pack_ranges_stay_below_texture_store(Path(d) / "ok")
    with tempfile.TemporaryDirectory() as d:
        test_pack_rejects_texture_store_overlap(Path(d) / "overflow")
    test_pack_rejects_worker_ram_overlap()
    test_pack_rejects_second_m68k_window_overlap()
    test_pack_rejects_expanded_mailbox_overlap()
    print("test_pack_ie68: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
