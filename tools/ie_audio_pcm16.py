#!/usr/bin/env python3
"""Build Intuition Engine PCM16 audio assets for the MK64 voice path.

Inputs are the neutral Stage 1.5 assets:
  - audiobanks.bin
  - audiotables.bin

Outputs:
  - audiotables.pcm16.bin: little-endian signed PCM16 sample pool
  - ie_audio_sample_table.{h,c}: original sample offset -> decoded PCM metadata

The bank walker follows the ALBankFile layout used by MK64. Samples are keyed by
their original sampleAddr offset into the shared audiotables blob; runtime code
derives the same key by subtracting the patched audiotables base.
"""

from __future__ import annotations

import argparse
import re
import struct
import tempfile
from dataclasses import dataclass, field
from pathlib import Path


FRAME_BYTES = {0: 9, 3: 5}
FRAME_SAMPLES = 16
WAVE_ARRAYS = [
    "sSawtoothWaves",
    "sTriangleWaves",
    "sSineWaves",
    "sSquareWaves",
    "sUnknownWave6",
    "gUnknownWave7",
]


def be16(buf: bytes, off: int) -> int:
    return struct.unpack_from(">h", buf, off)[0]


def be32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def bef32(buf: bytes, off: int) -> float:
    return struct.unpack_from(">f", buf, off)[0]


def clamp16(v: int) -> int:
    if v < -32768:
        return -32768
    if v > 32767:
        return 32767
    return v


@dataclass
class Sample:
    addr: int
    size: int
    codec: int
    order: int
    npredictors: int
    book: tuple[int, ...]
    data: bytes
    loop_start: int = 0
    loop_end: int = 0
    loop_count: int = 0
    banks: set[int] = field(default_factory=set)
    tunings: set[float] = field(default_factory=set)

    @property
    def has_loop(self) -> bool:
        return self.loop_count != 0


def read_alseqfile(buf: bytes) -> list[tuple[int, int]]:
    count = struct.unpack_from(">H", buf, 2)[0]
    entries = []
    off = 4
    for _ in range(count):
        entries.append((be32(buf, off), be32(buf, off + 4)))
        off += 8
    return entries


def read_book(buf: bytes, off: int) -> tuple[int, int, tuple[int, ...]]:
    order = be32(buf, off)
    npredictors = be32(buf, off + 4)
    count = 8 * order * npredictors
    book = struct.unpack_from(f">{count}h", buf, off + 8)
    return order, npredictors, book


def read_loop(buf: bytes, off: int) -> tuple[int, int, int]:
    start, end, count, _pad = struct.unpack_from(">IIII", buf, off)
    return start, end, count


def parse_samples(audiobanks_path: Path, audiotables_path: Path) -> tuple[int, list[Sample]]:
    banks = audiobanks_path.read_bytes()
    tables = audiotables_path.read_bytes()
    ctl_entries = read_alseqfile(banks)
    tbl_entries = read_alseqfile(tables)
    tbl_base = tbl_entries[0][0]
    samples: dict[int, Sample] = {}

    def read_sound(bank_base: int, sound_off: int, bank_idx: int, tuning: float) -> None:
        if sound_off == 0:
            return
        sample_off = be32(banks, bank_base + sound_off)
        if sample_off == 0:
            return
        sample_header = bank_base + sample_off
        sample_addr = be32(banks, sample_header + 4)
        loop_off = be32(banks, sample_header + 8)
        book_off = be32(banks, sample_header + 12)
        sample_size = be32(banks, sample_header + 16)

        if sample_addr in samples:
            samples[sample_addr].banks.add(bank_idx)
            samples[sample_addr].tunings.add(tuning)
            return

        order, npredictors, book = read_book(banks, bank_base + book_off)
        loop_start = loop_end = loop_count = 0
        if loop_off:
            loop_start, loop_end, loop_count = read_loop(banks, bank_base + loop_off)
        data = tables[tbl_base + sample_addr : tbl_base + sample_addr + sample_size]
        sample = Sample(
            addr=sample_addr,
            size=sample_size,
            codec=0,
            order=order,
            npredictors=npredictors,
            book=book,
            data=data,
            loop_start=loop_start,
            loop_end=loop_end,
            loop_count=loop_count,
        )
        sample.banks.add(bank_idx)
        sample.tunings.add(tuning)
        samples[sample_addr] = sample

    for bank_idx, (bank_off, bank_len) in enumerate(ctl_entries):
        if bank_len == 0:
            continue
        num_inst = be32(banks, bank_off)
        num_drums = be32(banks, bank_off + 4)
        bank_base = bank_off + 0x10
        drums_off = be32(banks, bank_base)
        for inst_idx in range(num_inst):
            inst_ptr = be32(banks, bank_base + 4 + inst_idx * 4)
            if inst_ptr == 0:
                continue
            inst = bank_base + inst_ptr
            for sound_idx in range(3):
                sound = inst + 8 + sound_idx * 8
                sample_ptr = be32(banks, sound)
                tuning = bef32(banks, sound + 4)
                if sample_ptr != 0 and tuning != 0.0:
                    read_sound(bank_base, sound - bank_base, bank_idx, tuning)
        if drums_off:
            for drum_idx in range(num_drums):
                drum_ptr = be32(banks, bank_base + drums_off + drum_idx * 4)
                if drum_ptr == 0:
                    continue
                sound = bank_base + drum_ptr + 4
                sample_ptr = be32(banks, sound)
                tuning = bef32(banks, sound + 4)
                if sample_ptr != 0 and tuning != 0.0:
                    read_sound(bank_base, sound - bank_base, bank_idx, tuning)

    return tbl_base, [samples[k] for k in sorted(samples)]


def expand_codebook(book: tuple[int, ...], order: int, npredictors: int) -> list[list[list[int]]]:
    table = [[[0] * (order + 8) for _ in range(8)] for _ in range(npredictors)]
    bi = 0
    for pred in range(npredictors):
        page = table[pred]
        for j in range(order):
            for k in range(8):
                page[k][j] = book[bi]
                bi += 1
        for k in range(1, 8):
            page[k][order] = page[k - 1][order - 1]
        page[0][order] = 1 << 11
        for k in range(1, 8):
            for j in range(k):
                page[j][k + order] = 0
            for j in range(k, 8):
                page[j][k + order] = page[j - k][order]
    return table


def inner_product(length: int, v1: list[int], v2: list[int]) -> int:
    total = 0
    for i in range(length):
        total += v1[i] * v2[i]
    return total // 2048


def decode_frame(frame: bytes, state: list[int], order: int, coef_tbl: list[list[list[int]]]) -> None:
    header = frame[0]
    scale = 1 << ((header >> 4) & 0xF)
    predictor = header & 0xF
    frame_size = len(frame)
    nibbles = [0] * FRAME_SAMPLES
    if frame_size == 5:
        for i in range(0, FRAME_SAMPLES, 4):
            c = frame[1 + i // 4]
            nibbles[i + 0] = (c >> 6) & 0x3
            nibbles[i + 1] = (c >> 4) & 0x3
            nibbles[i + 2] = (c >> 2) & 0x3
            nibbles[i + 3] = c & 0x3
    else:
        for i in range(0, FRAME_SAMPLES, 2):
            c = frame[1 + i // 2]
            nibbles[i + 0] = (c >> 4) & 0xF
            nibbles[i + 1] = c & 0xF
    for i, nibble in enumerate(nibbles):
        if frame_size == 5:
            if nibble >= 2:
                nibble -= 4
        elif nibble >= 8:
            nibble -= 16
        nibbles[i] = nibble * scale

    coef_page = coef_tbl[predictor]
    for j in range(2):
        in_vec = [0] * 16
        for i in range(order):
            in_vec[i] = state[(2 - j) * 8 - order + i]
        for i in range(8):
            idx = j * 8 + i
            in_vec[order + i] = nibbles[idx]
            state[idx] = inner_product(order + i, coef_page[i], in_vec) + nibbles[idx]


def decode_vadpcm(sample: Sample) -> list[int]:
    frame_size = FRAME_BYTES[sample.codec]
    coef_tbl = expand_codebook(sample.book, sample.order, sample.npredictors)
    state = [0] * 16
    out: list[int] = []
    for off in range(0, len(sample.data) - frame_size + 1, frame_size):
        decode_frame(sample.data[off : off + frame_size], state, sample.order, coef_tbl)
        out.extend(clamp16(v) for v in state)
    return out


def parse_wave_arrays(data_c: Path) -> list[list[int]]:
    source = data_c.read_text()
    waves = []
    for name in WAVE_ARRAYS:
        match = re.search(rf"s16\s+{name}\[256\]\s*=\s*\{{(.*?)\}};", source, re.S)
        if not match:
            raise SystemExit(f"missing wave array {name} in {data_c}")
        body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.S)
        vals = [int(v) for v in re.findall(r"-?\d+", body)]
        if len(vals) != 256:
            raise SystemExit(f"{name}: got {len(vals)} values, want 256")
        waves.append(vals)
    return waves


def c_array_pcm16le_rows(name: str, rows: list[list[int]]) -> str:
    lines = [f"const uint8_t {name}[{len(rows)}][512] = {{"]
    for row in rows:
        lines.append("    {")
        for i in range(0, len(row), 8):
            bytes_le = []
            for v in row[i : i + 8]:
                word = v & 0xFFFF
                bytes_le.append(f"0x{word & 0xFF:02X}")
                bytes_le.append(f"0x{word >> 8:02X}")
            lines.append(f"        {', '.join(bytes_le)},")
        lines.append("    },")
    lines.append("};")
    return "\n".join(lines)


def pcm16le_bytes(row: list[int]) -> bytes:
    out = bytearray()
    for v in row:
        word = v & 0xFFFF
        out.append(word & 0xFF)
        out.append((word >> 8) & 0xFF)
    return bytes(out)


def self_test() -> None:
    zero_book = (0,) * 16
    adpcm = Sample(
        addr=0,
        size=9,
        codec=0,
        order=2,
        npredictors=1,
        book=zero_book,
        data=bytes([0x00, 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF]),
    )
    got = decode_vadpcm(adpcm)
    want = [0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1]
    if got != want:
        raise SystemExit(f"adpcm decode self-test failed: got {got}, want {want}")

    small_adpcm = Sample(
        addr=0,
        size=5,
        codec=3,
        order=2,
        npredictors=1,
        book=zero_book,
        data=bytes([0x00, 0x1B, 0x1B, 0x1B, 0x1B]),
    )
    got = decode_vadpcm(small_adpcm)
    want = [0, 1, -2, -1] * 4
    if got != want:
        raise SystemExit(f"small-adpcm decode self-test failed: got {got}, want {want}")

    with tempfile.TemporaryDirectory() as td:
        data_c = Path(td) / "data.c"
        arrays = "\n".join(f"s16 {name}[256] = {{{', '.join(str(i - 128) for i in range(256))}}};" for name in WAVE_ARRAYS)
        data_c.write_text(arrays)
        waves = parse_wave_arrays(data_c)
    packed = pcm16le_bytes(waves[0])
    if packed[:8] != bytes([0x80, 0xFF, 0x81, 0xFF, 0x82, 0xFF, 0x83, 0xFF]):
        raise SystemExit(f"synth-wave little-endian self-test failed: first bytes {packed[:8].hex()}")
    print("ie_audio_pcm16: self-test PASS")


def emit(audiobanks: Path, audiotables: Path, data_c: Path, pcm_out: Path, c_out: Path, h_out: Path) -> None:
    tbl_base, samples = parse_samples(audiobanks, audiotables)
    blob = bytearray()
    descs = []
    for sample in samples:
        pcm = decode_vadpcm(sample)
        pcm_offset = len(blob)
        blob.extend(struct.pack(f"<{len(pcm)}h", *pcm))
        loop_start = min(sample.loop_start, len(pcm))
        loop_end = min(sample.loop_end, len(pcm))
        if loop_end < loop_start:
            loop_end = loop_start
        descs.append(
            {
                "src_offset": sample.addr,
                "pcm_offset": pcm_offset,
                "byte_len": len(pcm) * 2,
                "sample_count": len(pcm),
                "loop_start_bytes": loop_start * 2,
                "loop_len_bytes": (loop_end - loop_start) * 2,
                "loop_count": sample.loop_count,
            }
        )

    pcm_out.parent.mkdir(parents=True, exist_ok=True)
    c_out.parent.mkdir(parents=True, exist_ok=True)
    h_out.parent.mkdir(parents=True, exist_ok=True)
    pcm_out.write_bytes(blob)

    h_out.write_text(
        "\n".join(
            [
                "/* Auto-generated by tools/ie_audio_pcm16.py. Do not edit. */",
                "#ifndef IE_AUDIO_SAMPLE_TABLE_H",
                "#define IE_AUDIO_SAMPLE_TABLE_H",
                "",
                "#include <stdint.h>",
                "",
                "typedef struct {",
                "    uint32_t src_offset;",
                "    uint32_t pcm_offset;",
                "    uint32_t byte_len;",
                "    uint32_t sample_count;",
                "    uint32_t loop_start_bytes;",
                "    uint32_t loop_len_bytes;",
                "    uint32_t loop_count;",
                "} IEAudioSampleDesc;",
                "",
                f"#define IE_AUDIO_SAMPLE_COUNT {len(descs)}",
                f"#define IE_AUDIO_PCM16_SIZE {len(blob)}u",
                f"#define IE_AUDIO_TABLE_BASE_OFFSET 0x{tbl_base:X}u",
                "#define IE_SYNTH_WAVEFORM_COUNT 6",
                "#define IE_SYNTH_WAVE_SAMPLES 256",
                "#define IE_SYNTH_WAVE_BYTES (IE_SYNTH_WAVE_SAMPLES * 2)",
                "#define IE_SYNTH_WAVE_HARMONICS 4",
                "#define IE_SYNTH_WAVE_SEGMENT_SAMPLES 64",
                "#define IE_SYNTH_WAVE_SEGMENT_BYTES (IE_SYNTH_WAVE_SEGMENT_SAMPLES * 2)",
                "",
                "extern const IEAudioSampleDesc gIEAudioSampleTable[IE_AUDIO_SAMPLE_COUNT];",
                "extern const uint8_t gIESynthWaveLE[IE_SYNTH_WAVEFORM_COUNT][IE_SYNTH_WAVE_BYTES];",
                "",
                "#endif /* IE_AUDIO_SAMPLE_TABLE_H */",
                "",
            ]
        )
    )

    c_lines = [
        "/* Auto-generated by tools/ie_audio_pcm16.py. Do not edit. */",
        '#include "ie_audio_sample_table.h"',
        "",
        "const IEAudioSampleDesc gIEAudioSampleTable[IE_AUDIO_SAMPLE_COUNT] = {",
    ]
    for d in descs:
        c_lines.append(
            "    { "
            f"0x{d['src_offset']:06X}, 0x{d['pcm_offset']:06X}, {d['byte_len']}u, "
            f"{d['sample_count']}u, {d['loop_start_bytes']}u, {d['loop_len_bytes']}u, {d['loop_count']}u "
            "},"
        )
    c_lines.extend(["};", "", c_array_pcm16le_rows("gIESynthWaveLE", parse_wave_arrays(data_c)), ""])
    c_out.write_text("\n".join(c_lines))
    print(
        f"ie_audio_pcm16: {len(descs)} samples, pcm16 {len(blob):,} B -> {pcm_out}, "
        f"table -> {c_out}/{h_out}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--audiobanks", type=Path)
    parser.add_argument("--audiotables", type=Path)
    parser.add_argument("--data-c", default=Path("src/audio/data.c"), type=Path)
    parser.add_argument("--pcm-out", type=Path)
    parser.add_argument("--c-out", type=Path)
    parser.add_argument("--h-out", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    for attr in ("audiobanks", "audiotables", "pcm_out", "c_out", "h_out"):
        if getattr(args, attr) is None:
            parser.error(f"--{attr.replace('_', '-')} is required unless --self-test is used")
    emit(args.audiobanks, args.audiotables, args.data_c, args.pcm_out, args.c_out, args.h_out)


if __name__ == "__main__":
    main()
