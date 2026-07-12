#!/usr/bin/env python3
"""Host tests for the Stage 1.5 neutral ROM asset pipeline.

Run directly: python3 tests/test_rom_assets.py
or via make:  make assets-test

Tests that need the real source ROM are skipped with an explicit message
when no accepted ROM is present; everything else (mio0 decoder, ROM
contract failures, manifest validation, verify-only mode) runs without it.
"""

import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import rom_assets  # noqa: E402

ROM_CANDIDATES = [
    os.path.join(REPO_ROOT, "Mario Kart 64 (USA).z64"),
    os.path.join(REPO_ROOT, "baserom.us.z64"),
]


def find_rom():
    for path in ROM_CANDIDATES:
        if os.path.isfile(path):
            return path
    return None


ROM_PATH = find_rom()

# Mirrors of the runtime buffer contract in include/buffer_sizes.h and
# src/main.c. If these drift, the pipeline invariants below must be
# re-examined, so they are duplicated deliberately.
COURSE_BUF_SIZE = 146464
UNPACK_BUF_SIZE = 51008
UNPACKED_DL_BUF_SIZE = 51016
COMP_VERT_BUF_SIZE = 65536
SEG5_BUF_SIZE = 133120
COMMON_BUF_SIZE = 184664
CEREMONY_BUF_SIZE = 36232
AUDIOBANKS_SIZE = 79936
AUDIOTABLES_SIZE = 2409664
INSTRUMENT_SETS_SIZE = 256
SEQUENCES_SIZE = 143728


class TestMio0Decode(unittest.TestCase):
    def test_synthetic_golden(self):
        # Header: magic, decompressed size 8, comp stream at 0x14, raw at 0x16.
        # Layout bits 1,1,0 -> two literals 'A','B', then a copy of length 6
        # at distance 2 -> "ABABABAB".
        blob = (
            b"MIO0"
            + struct.pack(">III", 8, 0x14, 0x16)
            + bytes([0xC0, 0, 0, 0])
            + bytes([0x30, 0x01])
            + b"AB"
        )
        self.assertEqual(rom_assets.mio0_decode(blob), b"ABABABAB")

    def test_bad_magic_rejected(self):
        with self.assertRaises(ValueError):
            rom_assets.mio0_decode(b"MIO1" + bytes(12))

    def test_truncated_rejected(self):
        with self.assertRaises(ValueError):
            rom_assets.mio0_decode(b"MIO0" + struct.pack(">III", 100, 0x14, 0x16))


class TestRomContract(unittest.TestCase):
    def test_missing_rom_fails_clearly(self):
        missing = os.path.join(tempfile.gettempdir(), "no-such-rom-input.z64")
        with self.assertRaises(rom_assets.RomError) as ctx:
            rom_assets.load_rom(missing)
        self.assertIn("no-such-rom-input.z64", str(ctx.exception))

    def test_wrong_size_fails_clearly(self):
        with tempfile.NamedTemporaryFile(suffix=".z64", delete=False) as f:
            f.write(b"\0" * 1024)
            path = f.name
        try:
            with self.assertRaises(rom_assets.RomError) as ctx:
                rom_assets.load_rom(path)
            self.assertIn("size", str(ctx.exception).lower())
        finally:
            os.unlink(path)

    def test_wrong_hash_fails_clearly(self):
        with tempfile.NamedTemporaryFile(suffix=".z64", delete=False) as f:
            f.write(b"\0" * rom_assets.ROM_SIZE)
            path = f.name
        try:
            with self.assertRaises(rom_assets.RomError) as ctx:
                rom_assets.load_rom(path)
            self.assertIn("sha1", str(ctx.exception).lower())
        finally:
            os.unlink(path)

    def test_generate_requires_rom(self):
        # Clean-room: extraction must fail explicitly, never emit stubs.
        with tempfile.TemporaryDirectory() as out:
            with self.assertRaises(rom_assets.RomError):
                rom_assets.generate("/nonexistent/rom.z64", out)
            self.assertEqual(os.listdir(out), [])


class FakeLayout(unittest.TestCase):
    """Manifest/verify tests built on a small fake layout - no ROM needed."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="rom_assets_test_")
        self.assets = [
            ("alpha.bin", b"alpha-data", {"conversion": "raw", "rom_ranges": [[0, 10]]}),
            ("beta.bin", b"beta-data", {"conversion": "raw", "rom_ranges": [[10, 19]]}),
        ]
        rom_assets.write_layout(self.dir, self.assets, self._rom_info())

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    @staticmethod
    def _rom_info():
        return {
            "sha1": rom_assets.ACCEPTED_ROM_SHA1,
            "size": rom_assets.ROM_SIZE,
        }

    def manifest_path(self):
        return os.path.join(self.dir, rom_assets.MANIFEST_NAME)

    def load_manifest(self):
        with open(self.manifest_path()) as f:
            return json.load(f)

    def save_manifest(self, manifest):
        with open(self.manifest_path(), "w") as f:
            json.dump(manifest, f, indent=2, sort_keys=True)


class TestVerifyOnly(FakeLayout):
    def test_clean_layout_verifies_without_rom(self):
        self.assertEqual(rom_assets.verify(self.dir), [])

    def test_missing_manifest(self):
        os.unlink(self.manifest_path())
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("manifest" in p.lower() for p in problems))

    def test_missing_asset_file(self):
        os.unlink(os.path.join(self.dir, "alpha.bin"))
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("alpha.bin" in p for p in problems))

    def test_stale_extra_file_reported(self):
        with open(os.path.join(self.dir, "stray.bin"), "wb") as f:
            f.write(b"junk")
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("stray.bin" in p for p in problems))

    def test_checksum_drift_reported(self):
        with open(os.path.join(self.dir, "beta.bin"), "wb") as f:
            f.write(b"beta-dat4")
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("beta.bin" in p and "sha256" in p for p in problems))

    def test_size_drift_reported(self):
        with open(os.path.join(self.dir, "beta.bin"), "ab") as f:
            f.write(b"x")
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("beta.bin" in p for p in problems))

    def test_duplicate_ids_rejected(self):
        m = self.load_manifest()
        m["assets"].append(dict(m["assets"][0]))
        m["asset_count"] = len(m["assets"])
        self.save_manifest(m)
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("duplicate" in p.lower() for p in problems))

    def test_unstable_ordering_rejected(self):
        m = self.load_manifest()
        m["assets"].reverse()
        self.save_manifest(m)
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("order" in p.lower() for p in problems))

    def test_asset_count_mismatch_rejected(self):
        m = self.load_manifest()
        m["asset_count"] = 99
        self.save_manifest(m)
        problems = rom_assets.verify(self.dir)
        self.assertTrue(any("asset_count" in p for p in problems))

    def test_malformed_asset_records_rejected(self):
        # A damaged or hand-edited manifest must produce verification
        # problems, never a crash.
        for corrupt in (
            lambda m: m["assets"][0].pop("id"),
            lambda m: m["assets"][0].update(id=None),
            lambda m: m["assets"][0].update(id=7),
            lambda m: m["assets"].__setitem__(0, "not-a-record"),
        ):
            m = self.load_manifest()
            corrupt(m)
            self.save_manifest(m)
            problems = rom_assets.verify(self.dir)
            self.assertTrue(
                any("id" in p.lower() for p in problems),
                "expected a malformed-id problem, got %r" % problems,
            )
            rom_assets.write_layout(self.dir, self.assets, self._rom_info())

    def test_forbidden_asset_ids_rejected(self):
        for bad in ("dc_data/foo.bin", "../escape.bin", "sub/dir.bin", "rom.z64"):
            m = self.load_manifest()
            entry = dict(m["assets"][0])
            entry["id"] = bad
            m["assets"] = sorted(m["assets"] + [entry], key=lambda a: a["id"])
            m["asset_count"] = len(m["assets"])
            self.save_manifest(m)
            problems = rom_assets.verify(self.dir)
            self.assertTrue(
                any(bad in p for p in problems),
                "expected a problem naming %r, got %r" % (bad, problems),
            )
            rom_assets.write_layout(self.dir, self.assets, self._rom_info())


@unittest.skipUnless(ROM_PATH, "accepted source ROM not present; ROM-backed golden checks skipped")
class TestWithRom(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rom = rom_assets.load_rom(ROM_PATH)
        cls.out = tempfile.mkdtemp(prefix="rom_assets_gen_")
        cls.manifest = rom_assets.generate(ROM_PATH, cls.out)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.out, ignore_errors=True)

    def asset(self, asset_id):
        for entry in self.manifest["assets"]:
            if entry["id"] == asset_id:
                return entry
        self.fail("asset %s missing from manifest" % asset_id)

    def read_asset(self, asset_id):
        with open(os.path.join(self.out, asset_id), "rb") as f:
            return f.read()

    def test_course_table_structure(self):
        table = rom_assets.read_course_table(self.rom)
        self.assertEqual(len(table), 20)
        mario = table[0]
        self.assertEqual(mario["name"], "mario_raceway")
        self.assertEqual(mario["vertex_count"], 0x167D)
        self.assertEqual(mario["packed_offset"], 0x96F4)
        for entry in table:
            self.assertEqual(self.rom[entry["dl_start"]:entry["dl_start"] + 4], b"MIO0")
            self.assertLess(entry["dl_start"], entry["dl_end"])
            self.assertLess(entry["vertex_start"], entry["vertex_end"])
            self.assertLess(entry["offsets_start"], entry["offsets_end"])

    def test_asset_inventory(self):
        # 7 global assets + 4 per course.
        self.assertEqual(self.manifest["asset_count"], 7 + 20 * 4)
        self.assertEqual(len(self.manifest["assets"]), self.manifest["asset_count"])
        for required in (
            "audiobanks.bin",
            "audiotables.bin",
            "instrument_sets.bin",
            "sequences.bin",
            "common_data.bin",
            "ceremony_data.bin",
            "course_metadata.bin",
            "banshee_boardwalk_data.bin",
            "mario_raceway_data.bin",
            "mario_raceway_geography.bin",
            "mario_raceway_tex.bin",
            "mario_raceway_offsets.bin",
            "big_donut_geography.bin",
        ):
            self.asset(required)

    def test_audio_sizes_match_runtime_contract(self):
        self.assertEqual(self.asset("audiobanks.bin")["size"], AUDIOBANKS_SIZE)
        self.assertEqual(self.asset("audiotables.bin")["size"], AUDIOTABLES_SIZE)
        self.assertEqual(self.asset("instrument_sets.bin")["size"], INSTRUMENT_SETS_SIZE)
        self.assertEqual(self.asset("sequences.bin")["size"], SEQUENCES_SIZE)

    def test_decompressed_segment_sizes_match_runtime_buffers(self):
        self.assertEqual(self.asset("common_data.bin")["size"], COMMON_BUF_SIZE)
        self.assertEqual(self.asset("ceremony_data.bin")["size"], CEREMONY_BUF_SIZE)
        max_course_data = max(
            self.asset("%s_data.bin" % name)["size"] for name in rom_assets.COURSE_NAMES
        )
        self.assertEqual(max_course_data, COURSE_BUF_SIZE)

    def test_course_buffer_invariants(self):
        table = rom_assets.read_course_table(self.rom)
        for entry in table:
            geography_size = self.asset("%s_geography.bin" % entry["name"])["size"]
            self.assertLessEqual(entry["packed_offset"], COMP_VERT_BUF_SIZE)
            self.assertLessEqual(geography_size - entry["packed_offset"], UNPACK_BUF_SIZE)
            self.assertLessEqual(self.asset("%s_tex.bin" % entry["name"])["size"], SEG5_BUF_SIZE)

    def test_geography_metadata(self):
        meta = self.asset("mario_raceway_geography.bin")["metadata"]
        self.assertEqual(meta["vertex_count"], 0x167D)
        self.assertEqual(meta["packed_offset"], 0x96F4)
        self.assertIn("final_displaylist_offset", meta)

    def test_texture_blob_matches_metadata(self):
        # <course>_tex.bin is the runtime segment-5 image: each texture
        # mio0-decompressed and placed at the cumulative
        # ALIGN16(uncompressed_size) offset, exactly the layout the
        # original game builds on its heap and the offsets the course
        # display lists encode as 0x05-segmented addresses.
        entry = self.asset("mario_raceway_tex.bin")
        blob = self.read_asset("mario_raceway_tex.bin")
        self.assertEqual(len(blob), entry["size"])
        offset = 0
        for tex in entry["metadata"]["textures"]:
            self.assertEqual(tex["blob_offset"], offset)
            rom_start = rom_assets.TEXTURE_POOL_OFFSET + tex["pool_offset"]
            decompressed = rom_assets.mio0_decode(
                self.rom[rom_start:rom_start + tex["compressed_size"]]
            )
            self.assertEqual(len(decompressed), tex["uncompressed_size"])
            self.assertEqual(
                blob[offset:offset + tex["uncompressed_size"]], decompressed
            )
            offset += (tex["uncompressed_size"] + 0xF) & ~0xF
        self.assertEqual(offset, entry["size"])

    def test_texture_blob_offsets_match_decomp_headers(self):
        # Independent cross-check: the decompilation's tracked
        # course_textures.linkonly.h headers carry the true segment-5
        # address of every course texture as a /* 0x05...... */ comment.
        # The generated blob offsets must agree for every course.
        import re

        for name in rom_assets.COURSE_NAMES:
            header = os.path.join(REPO_ROOT, "courses", name, "course_textures.linkonly.h")
            with open(header) as f:
                truth = [int(a, 16) & 0xFFFFFF
                         for a in re.findall(r"/\* (0x05[0-9A-Fa-f]+) \*/", f.read())]
            offsets = [tex["blob_offset"]
                       for tex in self.asset("%s_tex.bin" % name)["metadata"]["textures"]]
            self.assertEqual(offsets, truth, "segment-5 layout drift for %s" % name)

    def test_texture_blob_worst_case_fills_seg5_buffer(self):
        worst = max(
            self.asset("%s_tex.bin" % name)["size"] for name in rom_assets.COURSE_NAMES
        )
        self.assertEqual(worst, SEG5_BUF_SIZE)

    def test_course_metadata_asset(self):
        # course_metadata.bin is the runtime-facing form of the ROM course
        # table: the values load_course needs (vertex count, packed
        # displaylist offset, final displaylist offset, unknown1) in a
        # fixed big-endian layout, in course-id order. It replaces the
        # previous port's compiled-in packoffs[]/vertexCount tables.
        entry = self.asset("course_metadata.bin")
        blob = self.read_asset("course_metadata.bin")
        self.assertEqual(entry["conversion"], "course_metadata")
        self.assertEqual(entry["size"], len(blob))
        self.assertEqual(len(blob), 16 + 20 * 16)
        self.assertEqual(blob[:8], b"MK64META")
        version, count = struct.unpack_from(">II", blob, 8)
        self.assertEqual(version, 1)
        self.assertEqual(count, 20)
        table = rom_assets.read_course_table(self.rom)
        for i, rom_entry in enumerate(table):
            vc, po, fdo, unk = struct.unpack_from(">IIII", blob, 16 + i * 16)
            self.assertEqual(vc, rom_entry["vertex_count"], rom_entry["name"])
            self.assertEqual(po, rom_entry["packed_offset"], rom_entry["name"])
            self.assertEqual(fdo, rom_entry["final_displaylist_offset"], rom_entry["name"])
            self.assertEqual(unk, rom_entry["unknown1"], rom_entry["name"])
        # Provenance: derived from the ROM course table bytes.
        self.assertEqual(
            entry["rom_ranges"],
            [[rom_assets.COURSE_TABLE_OFFSET,
              rom_assets.COURSE_TABLE_OFFSET
              + rom_assets.COURSE_COUNT * rom_assets.COURSE_TABLE_ENTRY_SIZE]],
        )

    def test_unpacked_displaylist_buffer_invariant(self):
        # The runtime expands each course's packed displaylists into a
        # buffer of ALIGN16(final_displaylist_offset) + 8 bytes (the
        # original game's allocation). UNPACKED_DL_BUF_SIZE must cover the
        # worst course exactly.
        table = rom_assets.read_course_table(self.rom)
        worst = max(
            (((entry["final_displaylist_offset"] + 0xF) & ~0xF) + 8) for entry in table
        )
        self.assertEqual(worst, UNPACKED_DL_BUF_SIZE)

    def test_manifest_provenance(self):
        self.assertEqual(self.manifest["rom"]["sha1"], rom_assets.ACCEPTED_ROM_SHA1)
        self.assertEqual(self.manifest["rom"]["size"], rom_assets.ROM_SIZE)
        self.assertEqual(self.manifest["extractor"]["version"], rom_assets.PIPELINE_VERSION)
        for entry in self.manifest["assets"]:
            self.assertIn(
                entry["conversion"],
                ("raw", "mio0", "texture_decompressed_concat", "course_metadata"),
            )
            self.assertTrue(entry["rom_ranges"])
            self.assertNotIn("/", entry["id"])
            self.assertFalse(entry["id"].endswith(".z64"))

    def test_generated_layout_verifies_without_rom(self):
        self.assertEqual(rom_assets.verify(self.out), [])

    def test_determinism(self):
        out2 = tempfile.mkdtemp(prefix="rom_assets_gen2_")
        try:
            rom_assets.generate(ROM_PATH, out2)
            with open(os.path.join(self.out, rom_assets.MANIFEST_NAME), "rb") as f:
                m1 = f.read()
            with open(os.path.join(out2, rom_assets.MANIFEST_NAME), "rb") as f:
                m2 = f.read()
            self.assertEqual(m1, m2)
            for sample in ("common_data.bin", "mario_raceway_geography.bin"):
                with open(os.path.join(out2, sample), "rb") as f:
                    self.assertEqual(self.read_asset(sample), f.read())
        finally:
            shutil.rmtree(out2, ignore_errors=True)

    def test_generate_refuses_stale_files(self):
        out2 = tempfile.mkdtemp(prefix="rom_assets_stale_")
        try:
            with open(os.path.join(out2, "leftover.bin"), "wb") as f:
                f.write(b"old")
            with self.assertRaises(rom_assets.LayoutError) as ctx:
                rom_assets.generate(ROM_PATH, out2)
            self.assertIn("leftover.bin", str(ctx.exception))
        finally:
            shutil.rmtree(out2, ignore_errors=True)

    def test_golden_checksums(self):
        # Golden sha256 values locked at pipeline version 1.0.0
        # (course_metadata.bin and the decompressed-concat form of
        # <course>_tex.bin locked at 1.1.0).
        # audiobanks.bin/audiotables.bin were additionally cross-checked
        # byte-for-byte against the independent upstream extract_assets.py
        # output from the same ROM.
        golden = {
            "course_metadata.bin": "7af051556cb1eeb44b44aac428f574f10313e24993d6fe5f7edeb8f7ff4a8545",
            "audiobanks.bin": "1157131d2773e13babe3ecb412861e8259cc6f6a8a45de640a4fba02a38f24d9",
            "common_data.bin": "266ef993ab5f20150af498d3f67cc7aa94932a696ec12d3eda4b309f6f5a21c0",
            "ceremony_data.bin": "a2cedb9e43f455f322dc6b8e1ac54ca120274d71038a3543f0a4418d4649e596",
            "sequences.bin": "9c0b6da321760bee5f00549e3cdc0a2f34b7b80cc4be8c5c04256bbea56fe3c5",
            "mario_raceway_data.bin": "9fbf4b09a721aa22adff50f5e0aec8198d927202f4d0455e6210d00c584d70c2",
            "mario_raceway_geography.bin": "03cb687d68e0eaca6075e8cf3e6afc67fe3dff568c931a49ab2e71318e141e67",
            "mario_raceway_tex.bin": "b7546820be6ede359afee099dc8eec4e76994f2e3f374fb85db1ce28ef6e489a",
        }
        for asset_id, expected in golden.items():
            data = self.read_asset(asset_id)
            self.assertEqual(
                hashlib.sha256(data).hexdigest(), expected,
                "golden checksum drift for %s" % asset_id,
            )
            self.assertEqual(self.asset(asset_id)["sha256"], expected)


if __name__ == "__main__":
    unittest.main(verbosity=2)
