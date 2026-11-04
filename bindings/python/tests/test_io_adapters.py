# SPDX-License-Identifier: BSD-3-Clause
"""
ZXC - High-performance lossless compression

Copyright (c) Bertrand Lebonnois and contributors.

Tests for the io.RawIOBase adapters (zxc.ZxcReader / zxc.ZxcWriter)
and the zxc.detect_zxc helper.
"""

import errno
import io
import os

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


def test_reader_and_dstream_concatenated():
    """Concatenated archives stream as one, whatever the read boundaries."""
    a, b = b"first frame " * 5000, b"second frame " * 7000
    fa = zxc.compress(a)
    joined = fa + zxc.compress(b, checksum=True)
    # Reads ending exactly on the first footer, then 7-byte reads.
    for size in (len(fa), 7):
        assert zxc.ZxcReader(io.BytesIO(joined), buffer_size=size).read() == a + b
        # Without multistream, the reader stops after the first frame.
        one = zxc.ZxcReader(io.BytesIO(joined), buffer_size=size, multistream=False)
        assert one.read() == a
    ds = zxc.DStream()
    out = b"".join(ds.decompress(joined[i : i + 7]) for i in range(0, len(joined), 7))
    assert out == a + b and ds.finished
    # A corrupt frame raises in the call that decodes it, even with output so far.
    bad = bytearray(zxc.compress(a, checksum=True))
    bad[40] ^= 0xFF
    with pytest.raises(RuntimeError):
        zxc.DStream(checksum=True).decompress(bytes(bad))
    # Junk after a frame: the frame comes out first, the error on the next call.
    ds = zxc.DStream()
    assert ds.decompress(fa + b"junk") == a
    with pytest.raises(RuntimeError):
        ds.decompress(b"")


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


class _ShortRaw(io.RawIOBase):
    def __init__(self):
        self.data = bytearray()

    def writable(self):
        return True

    def write(self, b):
        n = min(len(b), 100)
        self.data += bytes(b[:n])
        return n


def test_writer_completes_short_raw_writes():
    payload = os.urandom(1 << 20)
    sink = _ShortRaw()
    with zxc.ZxcWriter(sink) as w:
        w.write(payload)
    assert zxc.decompress(bytes(sink.data)) == payload


def test_writer_refuses_raw_sink_that_would_block():
    class WouldBlock(io.RawIOBase):
        def writable(self):
            return True

        def write(self, b):
            return None

    w = zxc.ZxcWriter(WouldBlock())
    w.write(b"x")
    with pytest.raises(BlockingIOError):
        w.write(b"y")
    with pytest.raises(BlockingIOError):
        w.close()
    assert not w.closed
    w._dst = io.BytesIO()  # let the finalizer close quietly
    w.close()


def test_writer_refuses_raw_sink_taking_nothing():
    class Stuck(io.RawIOBase):
        def writable(self):
            return True

        def write(self, b):
            return 0

    w = zxc.ZxcWriter(Stuck())
    w.write(b"x")
    with pytest.raises(OSError, match="no bytes"):
        w.write(b"y")
    with pytest.raises(OSError, match="no bytes"):
        w.close()
    assert w.closed


def test_writer_accepts_duck_sink_without_count():
    class Sink:
        def __init__(self):
            self.data = b""

        def write(self, b):
            self.data += bytes(b)

    sink = Sink()
    with zxc.ZxcWriter(sink) as w:
        w.write(b"duck typed")
    assert zxc.decompress(sink.data) == b"duck typed"


class _FlakySink(io.RawIOBase):
    """Fails with *failure* (or would block, for None) until `up` is set."""

    def __init__(self, failure=None):
        self.data = bytearray()
        self.failure = failure
        self.up = False

    def writable(self):
        return True

    def write(self, b):
        if not self.up:
            if self.failure:
                raise self.failure
            return None
        self.data += bytes(b)
        return len(b)


PAYLOAD = bytes(range(256)) * 4000


def test_writer_keeps_output_the_sink_blocked_on():
    sink = _FlakySink()
    w = zxc.ZxcWriter(sink, level=1)
    taken = w.write(PAYLOAD)
    assert taken == len(PAYLOAD)
    with pytest.raises(BlockingIOError):
        w.write(PAYLOAD)  # not taken
    sink.up = True
    taken = w.write(PAYLOAD)
    assert taken == len(PAYLOAD)
    w.close()
    assert zxc.decompress(bytes(sink.data)) == PAYLOAD * 2


def test_writer_flush_drains_after_a_block():
    sink = _FlakySink()
    w = zxc.ZxcWriter(sink, level=1)
    w.write(PAYLOAD)
    with pytest.raises(BlockingIOError):
        w.flush()
    sink.up = True
    w.flush()
    assert sink.data
    w.close()
    assert zxc.decompress(bytes(sink.data)) == PAYLOAD


def test_writer_close_can_be_retried_after_a_block():
    sink = _FlakySink()
    w = zxc.ZxcWriter(sink, level=1)
    w.write(PAYLOAD)
    with pytest.raises(BlockingIOError):
        w.close()
    assert not w.closed
    sink.up = True
    w.close()
    assert w.closed
    assert zxc.decompress(bytes(sink.data)) == PAYLOAD


def test_writer_counts_what_a_blocked_buffered_sink_took():
    class Partial(_FlakySink):
        def write(self, b):
            if not self.up:
                self.up = True
                self.data += bytes(b[:3])
                raise BlockingIOError(errno.EAGAIN, "full", 3)
            return super().write(b)

    sink = Partial()
    with zxc.ZxcWriter(sink, level=1) as w:
        w.write(PAYLOAD)
    assert zxc.decompress(bytes(sink.data)) == PAYLOAD


def test_writer_recovers_from_a_passing_sink_failure():
    sink = _FlakySink(OSError("transient"))
    w = zxc.ZxcWriter(sink, level=1)
    w.write(PAYLOAD)
    sink.up = True
    w.close()
    assert zxc.decompress(bytes(sink.data)) == PAYLOAD


def test_writer_surfaces_a_lasting_sink_failure():
    sink = _FlakySink(OSError("disk full"))
    w = zxc.ZxcWriter(sink, level=1)
    w.write(PAYLOAD)
    with pytest.raises(OSError, match="disk full"):
        w.write(PAYLOAD)
    with pytest.raises(OSError, match="disk full"):
        w.close()
    assert w.closed
