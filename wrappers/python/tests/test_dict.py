"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause

Tests for pre-trained dictionary support.
"""

import pytest
import zxc


def make_samples(n=6):
    """A small corpus of similar records — the kind of data dictionaries help."""
    return [
        b'{"id": %d, "type": "event", "user": "alice", "action": "login"}' % i
        for i in range(n)
    ]


def make_payload(n=4000):
    return b"".join(
        b'{"id": %d, "type": "event", "user": "alice", "action": "click"}\n' % i
        for i in range(n)
    )


@pytest.fixture
def trained_dict():
    return zxc.train_dict(make_samples())


# =========================================================================
# Training
# =========================================================================


class TestTrainDict:
    def test_returns_non_empty_bytes(self):
        d = zxc.train_dict(make_samples())
        assert isinstance(d, bytes)
        assert len(d) > 0
        assert len(d) <= 65535

    def test_respects_max_size(self):
        d = zxc.train_dict(make_samples(), max_size=128)
        assert len(d) <= 128

    def test_rejects_empty_samples(self):
        with pytest.raises(ValueError):
            zxc.train_dict([])

    def test_rejects_bad_max_size(self):
        with pytest.raises(ValueError):
            zxc.train_dict(make_samples(), max_size=0)
        with pytest.raises(ValueError):
            zxc.train_dict(make_samples(), max_size=70000)


# =========================================================================
# Compress / decompress with a dictionary
# =========================================================================


class TestCompressWithDict:
    def test_roundtrip_with_dict(self, trained_dict):
        payload = make_payload()
        arch = zxc.compress(payload, dict=trained_dict, checksum=True)
        out = zxc.decompress(arch, dict=trained_dict, checksum=True)
        assert out == payload

    def test_roundtrip_with_dict_no_size_hint(self, trained_dict):
        payload = make_payload()
        arch = zxc.compress(payload, dict=trained_dict)
        # decompress_size=None -> read from header
        out = zxc.decompress(arch, dict=trained_dict)
        assert out == payload

    def test_decompress_without_dict_raises(self, trained_dict):
        payload = make_payload()
        arch = zxc.compress(payload, dict=trained_dict)
        with pytest.raises(RuntimeError):
            zxc.decompress(arch, decompress_size=len(payload))

    def test_no_dict_still_works(self):
        payload = make_payload()
        arch = zxc.compress(payload)
        assert zxc.get_dict_id(arch) == 0
        assert zxc.decompress(arch) == payload


# =========================================================================
# Dictionary IDs
# =========================================================================


class TestDictId:
    def test_ids_agree_across_surfaces(self, trained_dict):
        payload = make_payload()
        arch = zxc.compress(payload, dict=trained_dict)
        zxd = zxc.dict_save(trained_dict)

        cid = zxc.dict_id(trained_dict)
        assert cid != 0
        assert zxc.get_dict_id(arch) == cid
        assert zxc.dict_get_id(zxd) == cid

    def test_id_from_first_16_bytes(self, trained_dict):
        payload = make_payload()
        arch = zxc.compress(payload, dict=trained_dict)
        assert zxc.get_dict_id(arch[:16]) == zxc.dict_id(trained_dict)

    def test_empty_content_id_is_zero(self):
        assert zxc.dict_id(b"") == 0

    def test_dict_get_id_invalid_is_zero(self):
        assert zxc.dict_get_id(b"not a zxd file") == 0


# =========================================================================
# Save / load roundtrip
# =========================================================================


class TestSaveLoad:
    def test_save_load_roundtrip(self, trained_dict):
        zxd = zxc.dict_save(trained_dict)
        assert isinstance(zxd, bytes)
        assert len(zxd) == 16 + len(trained_dict)  # zxc_dict_save_bound

        content, dict_id = zxc.dict_load(zxd)
        assert content == trained_dict
        assert dict_id == zxc.dict_id(trained_dict)

    def test_loaded_content_compresses(self, trained_dict):
        zxd = zxc.dict_save(trained_dict)
        content, _ = zxc.dict_load(zxd)
        payload = make_payload()
        arch = zxc.compress(payload, dict=content)
        assert zxc.decompress(arch, dict=content) == payload

    def test_load_invalid_raises(self):
        with pytest.raises(RuntimeError):
            zxc.dict_load(b"garbage not a zxd")


# =========================================================================
# Seekable with a dictionary
# =========================================================================


class TestSeekableDict:
    def _build(self, payload, dict_content, tmp_path, level=zxc.LEVEL_DEFAULT):
        src = tmp_path / "src.bin"
        dst = tmp_path / "dst.zxc"
        src.write_bytes(payload)
        with open(src, "rb") as fsrc, open(dst, "wb") as fdst:
            zxc.stream_compress(
                fsrc,
                fdst,
                level=level,
                checksum=True,
                seekable=True,
                dict=dict_content,
            )
        return dst.read_bytes()

    def test_seekable_range_with_dict(self, trained_dict, tmp_path):
        payload = make_payload()
        arch = self._build(payload, trained_dict, tmp_path)

        assert zxc.get_dict_id(arch) == zxc.dict_id(trained_dict)

        with zxc.Seekable(arch) as s:
            s.set_dict(trained_dict)
            assert s.decompressed_size == len(payload)
            # mid-range slice
            chunk = s.decompress_range(500, 1234)
            assert chunk == payload[500:500 + 1234]
            # full range
            full = s.decompress_range(0, s.decompressed_size)
            assert full == payload

    def test_set_dict_after_close_raises(self, trained_dict, tmp_path):
        payload = make_payload()
        arch = self._build(payload, trained_dict, tmp_path)
        s = zxc.Seekable(arch)
        s.close()
        with pytest.raises(ValueError):
            s.set_dict(trained_dict)
