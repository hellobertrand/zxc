"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
"""

import pytest
import zxc


def build_payload(size):
    return bytes((i * 31) & 0xFF for i in range(size))


def build_seekable_archive(payload, level=zxc.LEVEL_DEFAULT):
    return zxc.compress(payload, level=level, checksum=True)


def build_seekable_archive_stream(payload, tmp_path, level=zxc.LEVEL_DEFAULT):
    """Compress via the streaming path with seekable=True."""
    src = tmp_path / "src.bin"
    dst = tmp_path / "dst.zxc"
    src.write_bytes(payload)
    with open(src, "rb") as fsrc, open(dst, "wb") as fdst:
        zxc.stream_compress(fsrc, fdst, level=level, checksum=True, seekable=True)
    return dst.read_bytes()


# =========================================================================
# Queries
# =========================================================================


class TestSeekableQueries:
    def test_reports_size_and_blocks(self, tmp_path):
        payload = build_payload(64 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        with zxc.Seekable(compressed) as s:
            assert s.decompressed_size == len(payload)
            assert s.num_blocks >= 1

    def test_per_block_sizes(self, tmp_path):
        payload = build_payload(64 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        with zxc.Seekable(compressed) as s:
            nb = s.num_blocks
            assert s.block_compressed_size(0) > 0
            assert s.block_decompressed_size(0) > 0
            assert s.block_compressed_size(nb) is None
            assert s.block_decompressed_size(nb) is None


# =========================================================================
# decompress_range
# =========================================================================


class TestDecompressRange:
    def test_full_range_roundtrip(self, tmp_path):
        payload = build_payload(64 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        with zxc.Seekable(compressed) as s:
            out = s.decompress_range(0, len(payload))
            assert out == payload

    def test_mid_range_slice(self, tmp_path):
        payload = build_payload(64 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        with zxc.Seekable(compressed) as s:
            off, length = 1024, 8192
            out = s.decompress_range(off, length)
            assert out == payload[off : off + length]

    def test_zero_length(self, tmp_path):
        payload = build_payload(4096)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        with zxc.Seekable(compressed) as s:
            assert s.decompress_range(0, 0) == b""

    def test_all_levels(self, tmp_path):
        payload = build_payload(32 * 1024)
        for level in range(zxc.LEVEL_FASTEST, zxc.LEVEL_ULTRA + 1):
            compressed = build_seekable_archive_stream(payload, tmp_path, level=level)
            with zxc.Seekable(compressed) as s:
                assert s.decompress_range(0, len(payload)) == payload


# =========================================================================
# Error handling
# =========================================================================


class TestSeekableErrors:
    def test_garbage_buffer(self):
        with pytest.raises(RuntimeError):
            zxc.Seekable(b"\x01\x02\x03\x04\x05\x06\x07\x08")

    def test_empty_buffer(self):
        with pytest.raises(ValueError):
            zxc.Seekable(b"")

    def test_methods_after_close(self, tmp_path):
        payload = build_payload(4096)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        s = zxc.Seekable(compressed)
        s.close()
        with pytest.raises(ValueError):
            _ = s.num_blocks
        # close is idempotent
        s.close()

    def test_bad_type(self):
        with pytest.raises(TypeError):
            zxc.Seekable(42)

    def test_non_seekable_archive_rejected(self):
        payload = build_payload(4096)
        compressed = zxc.compress(payload)
        with pytest.raises(RuntimeError):
            zxc.Seekable(compressed)


# =========================================================================
# Reader callback
# =========================================================================


class TestSeekableReader:
    def test_roundtrip_via_reader(self, tmp_path):
        payload = build_payload(128 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        calls = [0]

        class Reader:
            size = len(compressed)

            def read_at(self, length, offset):
                calls[0] += 1
                return compressed[offset : offset + length]

        with zxc.Seekable(Reader()) as s:
            assert calls[0] == 3  # header, footer, seek table
            assert s.decompressed_size == len(payload)
            out = s.decompress_range(0, len(payload))
            assert out == payload

            before = calls[0]
            chunk = s.decompress_range(2048, 1024)
            assert chunk == payload[2048 : 2048 + 1024]
            assert calls[0] - before == 1

    def test_reader_exception_propagates(self, tmp_path):
        # The reader's own exception (with its message) must reach the caller,
        # not be shadowed by a generic RuntimeError.
        payload = build_payload(128 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)
        attempted = [0]

        class BadReader:
            size = len(compressed)

            def read_at(self, length, offset):
                attempted[0] += 1
                if attempted[0] > 3:
                    raise IOError("boom")
                return compressed[offset : offset + length]

        with zxc.Seekable(BadReader()) as s:
            with pytest.raises(IOError, match="boom"):
                s.decompress_range(0, len(payload))

    def test_reader_multithreaded_decode(self, tmp_path):
        # Regression: multi-threaded decode with a Python reader used to
        # invoke the callback from library worker threads without the GIL,
        # corrupting the interpreter.
        payload = build_payload(2 * 1024 * 1024)
        compressed = build_seekable_archive_stream(payload, tmp_path)

        class Reader:
            size = len(compressed)

            def read_at(self, length, offset):
                return compressed[offset : offset + length]

        with zxc.Seekable(Reader()) as s:
            assert s.num_blocks >= 2
            out = s.decompress_range(0, len(payload), n_threads=4)
            assert out == payload

    def test_rejects_missing_attrs(self):
        with pytest.raises(TypeError):
            zxc.Seekable(object())

    def test_rejects_garbage_reader(self, tmp_path):
        class GarbageReader:
            size = 64

            def read_at(self, length, offset):
                return b"\x00" * length

        with pytest.raises(RuntimeError):
            zxc.Seekable(GarbageReader())


# =========================================================================
# Low-level seek table helpers
# =========================================================================


class TestSeekTableHelpers:
    def test_roundtrip(self):
        comp_sizes = [128, 256, 200, 4]
        sz = zxc.seek_table_size(len(comp_sizes))
        assert sz > 0
        buf = zxc.write_seek_table(comp_sizes)
        assert len(buf) == sz

    def test_rejects_empty(self):
        with pytest.raises(ValueError):
            zxc.write_seek_table([])
