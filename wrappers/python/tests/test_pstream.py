"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause

Tests for the push streaming API (zxc.CStream / zxc.DStream).
"""

import pytest
import zxc


def _roundtrip(data: bytes, *, level=zxc.LEVEL_DEFAULT, checksum=False) -> bytes:
    cs = zxc.CStream(level=level, checksum=checksum)
    chunks = []
    # Feed in 17-byte slices to exercise the buffering/state machine.
    step = max(1, min(17, len(data)))
    for i in range(0, len(data), step):
        chunks.append(cs.compress(data[i : i + step]))
    chunks.append(cs.end())
    cs.close()
    compressed = b"".join(chunks)

    ds = zxc.DStream(checksum=checksum)
    out_chunks = []
    step = max(1, min(31, len(compressed)))
    for i in range(0, len(compressed), step):
        out_chunks.append(ds.decompress(compressed[i : i + step]))
    assert ds.finished
    ds.close()
    return b"".join(out_chunks)


def test_pstream_small_roundtrip():
    data = b"Hello pstream! Round-trip through the Python push API."
    assert _roundtrip(data) == data


def test_pstream_with_checksum():
    data = bytes((i % 251 for i in range(32 * 1024)))
    assert _roundtrip(data, checksum=True) == data


def test_pstream_multi_block():
    # Larger than one default block (256 KB) to force multiple blocks.
    data = bytes(((i * 7) % 256 for i in range(512 * 1024)))
    assert _roundtrip(data) == data


def test_pstream_size_hints():
    cs = zxc.CStream()
    assert cs.in_size > 0
    assert cs.out_size > 0
    cs.close()

    ds = zxc.DStream()
    assert ds.in_size > 0
    assert ds.out_size > 0
    assert ds.finished is False
    ds.close()


def test_pstream_context_manager():
    data = b"context manager exit closes the stream"
    with zxc.CStream() as cs:
        compressed = cs.compress(data) + cs.end()
    with zxc.DStream() as ds:
        assert ds.decompress(compressed) == data
        assert ds.finished


def test_pstream_use_after_close():
    cs = zxc.CStream()
    cs.close()
    with pytest.raises(ValueError):
        cs.compress(b"foo")

    ds = zxc.DStream()
    ds.close()
    with pytest.raises(ValueError):
        ds.decompress(b"foo")


def test_pstream_truncated_stream_not_finished():
    data = b"some data " * 1000
    cs = zxc.CStream()
    compressed = cs.compress(data) + cs.end()
    cs.close()

    # Drop the last 5 bytes (partial footer) — decoder should not finalise.
    ds = zxc.DStream()
    ds.decompress(compressed[:-5])
    assert not ds.finished
    ds.close()
