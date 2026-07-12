#!/usr/bin/env python3
"""Neutral ROM asset pipeline for the portable Mario Kart 64 source tree.

Stage 1.5 host tool: extracts the runtime-required assets from the accepted
US ROM into a deterministic, platform-neutral generated layout, and verifies
an existing layout against its manifest without needing the ROM.

Modes:
    generate --rom <path> [--out <dir>]   extract assets + write manifest
    verify   [--dir <dir>]                check layout against manifest (no ROM)

Design rules (see STAGE1_5-NOTES.md):
  * The source ROM is never copied into the layout and never committed.
  * Asset bytes are ROM byte-exact (big-endian N64 data) apart from MIO0
    decompression where the runtime expects decompressed data. No byte-order
    conversion happens here; byte-order policy belongs to the platform seam.
  * Extraction is table-driven from the ROM's own course table, so per-course
    ranges, vertex counts, and packed-displaylist offsets carry provenance
    from the ROM itself rather than from any earlier port's build products.
  * Everything is deterministic: stable names, stable ordering, sorted JSON,
    no timestamps.
"""

import argparse
import hashlib
import json
import os
import struct
import sys

PIPELINE_VERSION = "1.1.0"

MANIFEST_NAME = "manifest.json"
MANIFEST_FORMAT = "mk64-neutral-assets"
MANIFEST_FORMAT_VERSION = 1
DEFAULT_OUT_DIR = "rom_assets"

# Accepted source ROM contract: Mario Kart 64 (USA), big-endian .z64.
ACCEPTED_ROM_SHA1 = "579c48e211ae952530ffc8738709f078d5dd215e"
ROM_SIZE = 0xC00000

# The game's own course table (20 entries, 0x30 bytes each) inside the US
# ROM's data segment. Each entry holds the ROM ranges for the course's
# display-list MIO0 segment, vertex/packed-displaylist segment, and
# texture/displaylist offsets segment, plus vertex count and the packed
# displaylist offset (as a segment-0xF address).
COURSE_TABLE_OFFSET = 0x122390
COURSE_TABLE_ENTRY_SIZE = 0x30
COURSE_COUNT = 20

# Shared pool of individually MIO0-compressed course textures; the per-course
# texture tables reference it with segment-0xF offsets.
TEXTURE_POOL_OFFSET = 0x641F70

# Course ids in course-table order (same order the runtime uses).
COURSE_NAMES = [
    "mario_raceway",
    "choco_mountain",
    "bowsers_castle",
    "banshee_boardwalk",
    "yoshi_valley",
    "frappe_snowland",
    "koopa_troopa_beach",
    "royal_raceway",
    "luigi_raceway",
    "moo_moo_farm",
    "toads_turnpike",
    "kalimari_desert",
    "sherbet_land",
    "rainbow_road",
    "wario_stadium",
    "block_fort",
    "skyscraper",
    "double_deck",
    "dks_jungle_parkway",
    "big_donut",
]

# Non-course segments. The audio region is contiguous in ROM:
# banks | tables | sequences | instrument sets.
RAW_SEGMENTS = {
    "audiobanks.bin": (0x966260, 0x13840),
    "audiotables.bin": (0x979AA0, 0x24C4C0),
    "sequences.bin": (0xBC5F60, 0x23170),
    "instrument_sets.bin": (0xBE90E0, 0x100),
}

# MIO0-compressed segments the runtime consumes decompressed.
MIO0_SEGMENTS = {
    "common_data.bin": 0x132B50,
    "ceremony_data.bin": 0x821D10,
}


class RomError(Exception):
    """Source ROM missing or violating the accepted ROM contract."""


class LayoutError(Exception):
    """Generated layout directory is not in a state we can safely write."""


def _sha1(data):
    return hashlib.sha1(data).hexdigest()


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _be32(data, offset):
    return struct.unpack(">I", data[offset:offset + 4])[0]


def load_rom(path):
    """Read and validate the source ROM. Raises RomError on any mismatch."""
    if not os.path.isfile(path):
        raise RomError(
            "source ROM not found: %s (supply the local Mario Kart 64 (USA) "
            ".z64 ROM; it is a user-provided input, never a repository file)" % path
        )
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != ROM_SIZE:
        raise RomError(
            "ROM %s has size %d, expected %d; refusing to extract"
            % (path, len(data), ROM_SIZE)
        )
    digest = _sha1(data)
    if digest != ACCEPTED_ROM_SHA1:
        raise RomError(
            "ROM %s has sha1 %s, expected %s (Mario Kart 64 (USA), big-endian "
            ".z64); refusing to extract" % (path, digest, ACCEPTED_ROM_SHA1)
        )
    return data


def mio0_decode_ex(blob):
    """Decode one MIO0 block.

    Returns (decoded bytes, consumed length of the compressed block).
    Raises ValueError on malformed input.
    """
    if len(blob) < 16 or blob[0:4] != b"MIO0":
        raise ValueError("not a MIO0 block")
    dec_size, comp_off, raw_off = struct.unpack(">III", blob[4:16])
    out = bytearray()
    layout_pos, comp_pos, raw_pos = 16, comp_off, raw_off
    bits = 0
    nbits = 0
    while len(out) < dec_size:
        if nbits == 0:
            if layout_pos >= len(blob):
                raise ValueError("truncated MIO0 layout stream")
            bits = blob[layout_pos]
            layout_pos += 1
            nbits = 8
        if bits & 0x80:
            if raw_pos >= len(blob):
                raise ValueError("truncated MIO0 raw stream")
            out.append(blob[raw_pos])
            raw_pos += 1
        else:
            if comp_pos + 2 > len(blob):
                raise ValueError("truncated MIO0 compressed stream")
            b0, b1 = blob[comp_pos], blob[comp_pos + 1]
            comp_pos += 2
            length = (b0 >> 4) + 3
            dist = ((b0 & 0x0F) << 8 | b1) + 1
            if dist > len(out):
                raise ValueError("MIO0 back-reference before stream start")
            for _ in range(length):
                if len(out) == dec_size:
                    break
                out.append(out[-dist])
        bits = (bits << 1) & 0xFF
        nbits -= 1
    return bytes(out), max(layout_pos, comp_pos, raw_pos)


def mio0_decode(blob):
    """Decode one MIO0 block. Raises ValueError on malformed input."""
    return mio0_decode_ex(blob)[0]


def read_course_table(rom):
    """Parse and validate the ROM's course table."""
    entries = []
    for i, name in enumerate(COURSE_NAMES):
        base = COURSE_TABLE_OFFSET + i * COURSE_TABLE_ENTRY_SIZE
        words = [_be32(rom, base + 4 * j) for j in range(12)]
        (dl_start, dl_end, vertex_start, vertex_end, offsets_start,
         offsets_end, vertex_seg, vertex_count, packed_seg,
         final_dl_offset, textures_seg, unknown_word) = words

        entry = {
            "course_id": i,
            "name": name,
            "dl_start": dl_start,
            "dl_end": dl_end,
            "vertex_start": vertex_start,
            "vertex_end": vertex_end,
            "offsets_start": offsets_start,
            "offsets_end": offsets_end,
            "vertex_count": vertex_count,
            "packed_offset": packed_seg & 0x00FFFFFF,
            "final_displaylist_offset": final_dl_offset,
            "unknown1": unknown_word >> 16,
        }

        for lo, hi in ((dl_start, dl_end), (vertex_start, vertex_end),
                       (offsets_start, offsets_end)):
            if not (0 < lo < hi <= len(rom)):
                raise RomError(
                    "course table entry %d (%s) has ROM range %#x..%#x outside "
                    "the ROM; the ROM does not match the extraction spec"
                    % (i, name, lo, hi)
                )
        if rom[dl_start:dl_start + 4] != b"MIO0":
            raise RomError(
                "course table entry %d (%s) display-list segment at %#x is not "
                "MIO0-compressed; the ROM does not match the extraction spec"
                % (i, name, dl_start)
            )
        if packed_seg >> 24 != 0x0F or vertex_seg != 0x0F000000:
            raise RomError(
                "course table entry %d (%s) has unexpected segment pointers"
                % (i, name)
            )
        if entry["packed_offset"] >= vertex_end - vertex_start:
            raise RomError(
                "course table entry %d (%s) packed displaylist offset %#x is "
                "outside its vertex segment" % (i, name, entry["packed_offset"])
            )
        if textures_seg != 0x09000000:
            raise RomError(
                "course table entry %d (%s) texture table is not at the start "
                "of its offsets segment" % (i, name)
            )
        entries.append(entry)
    return entries


def read_texture_table(rom, offsets_start, offsets_end):
    """Parse a course's texture table (16-byte entries, zero-terminated)."""
    textures = []
    pos = offsets_start
    while pos + 16 <= offsets_end:
        seg_addr, compressed_size, uncompressed_size, padding = struct.unpack(
            ">IIII", rom[pos:pos + 16]
        )
        if seg_addr == 0:
            break
        if seg_addr >> 24 != 0x0F or padding != 0:
            raise RomError(
                "texture table entry at ROM %#x does not look like a texture "
                "reference" % pos
            )
        pool_offset = seg_addr & 0x00FFFFFF
        rom_start = TEXTURE_POOL_OFFSET + pool_offset
        if rom[rom_start:rom_start + 4] != b"MIO0":
            raise RomError(
                "texture at pool offset %#x (ROM %#x) is not MIO0-compressed"
                % (pool_offset, rom_start)
            )
        textures.append({
            "pool_offset": pool_offset,
            "rom_start": rom_start,
            "compressed_size": compressed_size,
            "uncompressed_size": uncompressed_size,
        })
        pos += 16
    if not textures:
        raise RomError("empty texture table at ROM %#x" % offsets_start)
    return textures


def _check_sequences_span(rom):
    """Self-check: the sequence table must describe exactly its spec size."""
    start, size = RAW_SEGMENTS["sequences.bin"]
    count = struct.unpack(">H", rom[start + 2:start + 4])[0]
    end = 0
    for i in range(count):
        off, sz = struct.unpack(">II", rom[start + 4 + 8 * i:start + 12 + 8 * i])
        end = max(end, off + sz)
    if end != size:
        raise RomError(
            "sequence table spans %#x bytes, extraction spec says %#x"
            % (end, size)
        )


COURSE_METADATA_MAGIC = b"MK64META"
COURSE_METADATA_VERSION = 1


def build_course_metadata(table):
    """Fixed big-endian runtime form of the ROM course table.

    Header: 8-byte magic, u32 version, u32 course count. Then one 16-byte
    record per course in course-id order: vertex_count, packed_offset,
    final_displaylist_offset, unknown1 (all u32 big-endian). Consumers own
    byte order at the platform seam.
    """
    blob = bytearray()
    blob += COURSE_METADATA_MAGIC
    blob += struct.pack(">II", COURSE_METADATA_VERSION, len(table))
    for entry in table:
        blob += struct.pack(
            ">IIII",
            entry["vertex_count"],
            entry["packed_offset"],
            entry["final_displaylist_offset"],
            entry["unknown1"],
        )
    return bytes(blob)


def build_assets(rom):
    """Extract every runtime asset. Returns a list of (id, bytes, entry)."""
    _check_sequences_span(rom)
    assets = []

    for asset_id, (start, size) in RAW_SEGMENTS.items():
        assets.append((asset_id, rom[start:start + size], {
            "conversion": "raw",
            "rom_ranges": [[start, start + size]],
        }))

    for asset_id, start in MIO0_SEGMENTS.items():
        data, consumed = mio0_decode_ex(rom[start:])
        assets.append((asset_id, data, {
            "conversion": "mio0",
            "rom_ranges": [[start, start + consumed]],
        }))

    table = read_course_table(rom)

    assets.append(("course_metadata.bin", build_course_metadata(table), {
        "conversion": "course_metadata",
        "rom_ranges": [[COURSE_TABLE_OFFSET,
                        COURSE_TABLE_OFFSET + COURSE_COUNT * COURSE_TABLE_ENTRY_SIZE]],
    }))

    for entry in table:
        name = entry["name"]

        dl = rom[entry["dl_start"]:entry["dl_end"]]
        assets.append(("%s_data.bin" % name, mio0_decode(dl), {
            "conversion": "mio0",
            "rom_ranges": [[entry["dl_start"], entry["dl_end"]]],
            "metadata": {"course_id": entry["course_id"]},
        }))

        assets.append((
            "%s_geography.bin" % name,
            rom[entry["vertex_start"]:entry["vertex_end"]],
            {
                "conversion": "raw",
                "rom_ranges": [[entry["vertex_start"], entry["vertex_end"]]],
                "metadata": {
                    "course_id": entry["course_id"],
                    "vertex_count": entry["vertex_count"],
                    "packed_offset": entry["packed_offset"],
                    "final_displaylist_offset": entry["final_displaylist_offset"],
                    "unknown1": entry["unknown1"],
                },
            },
        ))

        assets.append((
            "%s_offsets.bin" % name,
            rom[entry["offsets_start"]:entry["offsets_end"]],
            {
                "conversion": "raw",
                "rom_ranges": [[entry["offsets_start"], entry["offsets_end"]]],
                "metadata": {"course_id": entry["course_id"]},
            },
        ))

        # <course>_tex.bin is the ready-to-use segment-5 image: every
        # texture from the course's texture table, mio0-decompressed and
        # placed at the cumulative ALIGN16(uncompressed_size) offset. This
        # reproduces the layout the original game builds on its heap, so
        # the 0x05-segmented texture addresses inside the course display
        # lists resolve without runtime relocation (cross-checked against
        # the decompilation's course_textures.linkonly.h headers).
        textures = read_texture_table(rom, entry["offsets_start"], entry["offsets_end"])
        blob = bytearray()
        tex_meta = []
        ranges = []
        for tex in textures:
            compressed = rom[tex["rom_start"]:tex["rom_start"] + tex["compressed_size"]]
            decompressed = mio0_decode(compressed)
            if len(decompressed) != tex["uncompressed_size"]:
                raise RomError(
                    "texture at pool offset %#x decompresses to %d bytes, "
                    "table says %d" % (
                        tex["pool_offset"], len(decompressed),
                        tex["uncompressed_size"],
                    )
                )
            tex_meta.append({
                "pool_offset": tex["pool_offset"],
                "compressed_size": tex["compressed_size"],
                "uncompressed_size": tex["uncompressed_size"],
                "blob_offset": len(blob),
            })
            ranges.append([tex["rom_start"], tex["rom_start"] + tex["compressed_size"]])
            blob += decompressed
            blob += bytes(-len(blob) % 16)
        assets.append(("%s_tex.bin" % name, bytes(blob), {
            "conversion": "texture_decompressed_concat",
            "rom_ranges": ranges,
            "metadata": {"course_id": entry["course_id"], "textures": tex_meta},
        }))

    assets.sort(key=lambda a: a[0])
    return assets


def _manifest_for(assets, rom_info):
    manifest_assets = []
    for asset_id, data, entry in assets:
        record = {
            "id": asset_id,
            "size": len(data),
            "sha256": _sha256(data),
            "conversion": entry["conversion"],
            "rom_ranges": entry["rom_ranges"],
        }
        if entry.get("metadata"):
            record["metadata"] = entry["metadata"]
        manifest_assets.append(record)
    return {
        "format": MANIFEST_FORMAT,
        "format_version": MANIFEST_FORMAT_VERSION,
        "extractor": {"name": "rom_assets.py", "version": PIPELINE_VERSION},
        "rom": {"sha1": rom_info["sha1"], "size": rom_info["size"]},
        "asset_count": len(manifest_assets),
        "assets": manifest_assets,
    }


def _dump_manifest(manifest):
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def write_layout(out_dir, assets, rom_info):
    """Write asset files and the manifest. Returns the manifest dict."""
    os.makedirs(out_dir, exist_ok=True)
    manifest = _manifest_for(assets, rom_info)
    for asset_id, data, _entry in assets:
        with open(os.path.join(out_dir, asset_id), "wb") as f:
            f.write(data)
    with open(os.path.join(out_dir, MANIFEST_NAME), "w") as f:
        f.write(_dump_manifest(manifest))
    return manifest


def generate(rom_path, out_dir):
    """Extract all assets from the ROM into out_dir. Fails on stale files."""
    rom = load_rom(rom_path)
    assets = build_assets(rom)
    expected = {asset_id for asset_id, _d, _e in assets} | {MANIFEST_NAME}
    if os.path.isdir(out_dir):
        stale = sorted(set(os.listdir(out_dir)) - expected)
        if stale:
            raise LayoutError(
                "output directory %s contains files the pipeline does not "
                "own: %s; remove them (or the directory) and re-run"
                % (out_dir, ", ".join(stale))
            )
    return write_layout(out_dir, assets,
                        {"sha1": _sha1(rom), "size": len(rom)})


def _validate_asset_id(asset_id):
    problems = []
    if "/" in asset_id or "\\" in asset_id or ".." in asset_id:
        problems.append("asset id %r must be a bare file name" % asset_id)
    if "dc_data" in asset_id:
        problems.append(
            "asset id %r resurrects the removed console-port layout" % asset_id
        )
    if asset_id.lower().endswith(".z64") or asset_id == MANIFEST_NAME:
        problems.append("asset id %r may not be listed as a generated asset" % asset_id)
    return problems


def verify(layout_dir):
    """Validate a generated layout against its manifest. No ROM required.

    Returns a list of human-readable problems; empty means the layout is
    complete, unmodified, and free of stale files.
    """
    problems = []
    manifest_path = os.path.join(layout_dir, MANIFEST_NAME)
    if not os.path.isfile(manifest_path):
        return ["manifest not found: %s (run generate first)" % manifest_path]
    try:
        with open(manifest_path) as f:
            manifest = json.load(f)
    except (OSError, ValueError) as exc:
        return ["manifest unreadable: %s" % exc]

    if manifest.get("format") != MANIFEST_FORMAT:
        problems.append("manifest format is %r, expected %r"
                        % (manifest.get("format"), MANIFEST_FORMAT))
    if manifest.get("format_version") != MANIFEST_FORMAT_VERSION:
        problems.append("manifest format_version is %r, expected %r"
                        % (manifest.get("format_version"), MANIFEST_FORMAT_VERSION))
    for field in ("sha1", "size"):
        if field not in manifest.get("rom", {}):
            problems.append("manifest rom provenance is missing %r" % field)
    if "version" not in manifest.get("extractor", {}):
        problems.append("manifest extractor provenance is missing 'version'")

    assets = manifest.get("assets", [])
    if not isinstance(assets, list):
        return problems + ["manifest 'assets' is not a list"]
    if manifest.get("asset_count") != len(assets):
        problems.append("asset_count is %r but manifest lists %d assets"
                        % (manifest.get("asset_count"), len(assets)))

    # Malformed records must surface as problems, never as crashes: only
    # well-formed string ids take part in the dedup/ordering/file checks.
    records = []
    ids = []
    for index, record in enumerate(assets):
        asset_id = record.get("id") if isinstance(record, dict) else None
        if not isinstance(asset_id, str) or not asset_id:
            problems.append(
                "asset record %d is malformed: missing or non-string id (%r)"
                % (index, record if not isinstance(record, dict) else asset_id)
            )
            continue
        records.append(record)
        ids.append(asset_id)

    seen = set()
    for asset_id in ids:
        if asset_id in seen:
            problems.append("duplicate asset id %r in manifest" % asset_id)
        seen.add(asset_id)
    if ids != sorted(ids):
        problems.append("manifest asset ordering is not stable (ids must be sorted)")

    for record in records:
        asset_id = record["id"]
        problems.extend(_validate_asset_id(asset_id))
        if "/" in asset_id or "\\" in asset_id or ".." in asset_id:
            continue
        path = os.path.join(layout_dir, asset_id)
        if not os.path.isfile(path):
            problems.append("asset %s is missing from the layout" % asset_id)
            continue
        with open(path, "rb") as f:
            data = f.read()
        if len(data) != record.get("size"):
            problems.append("asset %s has size %d, manifest says %r"
                            % (asset_id, len(data), record.get("size")))
        digest = _sha256(data)
        if digest != record.get("sha256"):
            problems.append("asset %s sha256 mismatch: file %s, manifest %r "
                            "(stale or modified output)"
                            % (asset_id, digest, record.get("sha256")))

    expected = set(ids) | {MANIFEST_NAME}
    for name in sorted(os.listdir(layout_dir)):
        if name not in expected:
            problems.append("unexpected file in layout (stale output?): %s" % name)

    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Neutral ROM asset pipeline: deterministic extraction "
                    "from the accepted US ROM, plus ROM-free verification."
    )
    sub = parser.add_subparsers(dest="mode", required=True)

    gen = sub.add_parser("generate", help="extract assets from the source ROM")
    gen.add_argument("--rom", required=True, help="path to the local .z64 ROM")
    gen.add_argument("--out", default=DEFAULT_OUT_DIR,
                     help="output layout directory (default: %(default)s)")

    ver = sub.add_parser("verify", help="verify an existing layout (no ROM)")
    ver.add_argument("--dir", default=DEFAULT_OUT_DIR,
                     help="layout directory (default: %(default)s)")

    args = parser.parse_args(argv)

    if args.mode == "generate":
        try:
            manifest = generate(args.rom, args.out)
        except (RomError, LayoutError) as exc:
            print("error: %s" % exc, file=sys.stderr)
            return 1
        print("extracted %d assets into %s (manifest: %s)"
              % (manifest["asset_count"], args.out,
                 os.path.join(args.out, MANIFEST_NAME)))
        return 0

    problems = verify(args.dir)
    if problems:
        for p in problems:
            print("problem: %s" % p, file=sys.stderr)
        print("verification FAILED: %d problem(s) in %s"
              % (len(problems), args.dir), file=sys.stderr)
        return 1
    print("verification OK: %s matches its manifest" % args.dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
