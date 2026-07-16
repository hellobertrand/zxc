"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause

Tests for the io.RawIOBase adapters (zxc.ZxcReader / zxc.ZxcWriter)
and the zxc.detect_zxc helper.
"""

import io

import pytest
import zxc


def _roundtrip(data: bytes, *, level=zxc.LEVEL_DEFAULT, checksum=False) -> bytes:
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink, level=level, checksum=checksum) as w:
        w.write(data)
    sink.seek(0)
    with zxc.ZxcReader(sink, checksum=checksum) as r:
        return r.read()


def test_writer_reader_small_roundtrip():
    data = b"Hello io.RawIOBase bridge over ZXC!"
    assert _roundtrip(data) == data


def test_writer_reader_large_roundtrip():
    data = bytes(((i * 13) % 251 for i in range(2 * 1024 * 1024)))
    assert _roundtrip(data) == data


def test_writer_many_small_writes():
    sink = io.BytesIO()
    want = bytearray()
    with zxc.ZxcWriter(sink) as w:
        for i in range(4096):
            chunk = bytes((i & 0xFF, (i >> 8) & 0xFF, (i ^ 0x5A) & 0xFF))
            want.extend(chunk)
            w.write(chunk)
    sink.seek(0)
    with zxc.ZxcReader(sink) as r:
        got = r.read()
    assert got == bytes(want)


def test_writer_reader_with_checksum():
    data = bytes((i % 251 for i in range(32 * 1024)))
    assert _roundtrip(data, checksum=True) == data


def test_writer_buffered_wrapping():
    """ZxcWriter wrapped in io.BufferedWriter — typical use case."""
    sink = io.BytesIO()
    raw = zxc.ZxcWriter(sink)
    bw = io.BufferedWriter(raw, buffer_size=8192)
    payload = bytes((i % 256 for i in range(200_000)))
    bw.write(payload)
    bw.close()  # flushes BufferedWriter, then closes ZxcWriter (finalises frame)

    sink.seek(0)
    with zxc.ZxcReader(sink) as r:
        assert r.read() == payload


def test_reader_buffered_wrapping():
    """ZxcReader wrapped in io.BufferedReader for line/iteration semantics."""
    sink = io.BytesIO()
    text = b"line one\nline two\nline three\n"
    with zxc.ZxcWriter(sink) as w:
        w.write(text)
    sink.seek(0)
    raw = zxc.ZxcReader(sink)
    br = io.BufferedReader(raw, buffer_size=64)
    assert br.readline() == b"line one\n"
    assert br.readline() == b"line two\n"
    assert br.readline() == b"line three\n"
    assert br.readline() == b""
    br.close()


def test_writer_close_idempotent():
    sink = io.BytesIO()
    w = zxc.ZxcWriter(sink)
    w.write(b"abc")
    w.close()
    w.close()  # must not raise
    sink.seek(0)
    with zxc.ZxcReader(sink) as r:
        assert r.read() == b"abc"


def test_reader_close_idempotent():
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink) as w:
        w.write(b"xyz")
    sink.seek(0)
    r = zxc.ZxcReader(sink)
    r.close()
    r.close()  # must not raise


def test_reader_truncated_frame_raises():
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink) as w:
        w.write(b"A" * 32_768)
    full = sink.getvalue()
    truncated = full[: len(full) // 2]
    src = io.BytesIO(truncated)
    with pytest.raises(OSError, match="truncated"):
        with zxc.ZxcReader(src) as r:
            r.read()


def test_writer_underlying_not_closed():
    """ZxcWriter.close() must NOT close the wrapped sink."""
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink) as w:
        w.write(b"hello")
    assert not sink.closed
    assert sink.tell() > 0


def test_reader_underlying_not_closed():
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink) as w:
        w.write(b"hello")
    sink.seek(0)
    with zxc.ZxcReader(sink) as r:
        r.read()
    assert not sink.closed


# ---------------------------------------------------------------------------
# detect_zxc
# ---------------------------------------------------------------------------


def test_detect_zxc_positive_compress():
    frame = zxc.compress(b"sniff me")
    assert zxc.detect_zxc(frame)


def test_detect_zxc_positive_writer():
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink) as w:
        w.write(b"hi")
    assert zxc.detect_zxc(sink.getvalue())


@pytest.mark.parametrize(
    "data",
    [
        b"",
        b"\xf5\x2e\xb0",  # too short
        b"\x00\x00\x00\x00",
        b"not a zxc frame at all",
        bytes(4),
    ],
)
def test_detect_zxc_negative(data):
    assert not zxc.detect_zxc(data)


def test_detect_zxc_accepts_memoryview_and_bytearray():
    frame = zxc.compress(b"x")
    assert zxc.detect_zxc(memoryview(frame))
    assert zxc.detect_zxc(bytearray(frame))
