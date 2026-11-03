# SPDX-License-Identifier: BSD-3-Clause
"""
ZXC - High-performance lossless compression

Copyright (c) Bertrand Lebonnois and contributors.

Compression writes no checksum by default; every read path verifies them.
"""

import io

import pytest
import zxc

PAYLOAD = bytes(range(256)) * 400


def _stream_compress(data, tmp_path):
    src, arc = tmp_path / "in", tmp_path / "arc"
    src.write_bytes(data)
    with open(src, "rb") as s, open(arc, "wb") as d:
        zxc.stream_compress(s, d)
    return arc.read_bytes()


def _writer(data, tmp_path):
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink) as w:
        w.write(data)
    return sink.getvalue()


def _cstream(data, tmp_path):
    with zxc.CStream() as cs:
        return cs.compress(data) + cs.end()


def _ctx(data, tmp_path):
    with zxc.Cctx() as c:
        return c.compress(data)


@pytest.mark.parametrize(
    "write",
    [lambda d, _: zxc.compress(d), _ctx, _cstream, _writer, _stream_compress],
    ids=["compress", "Cctx", "CStream", "ZxcWriter", "stream_compress"],
)
def test_default_compression_writes_no_checksum(write, tmp_path):
    assert zxc.get_frame_info(write(PAYLOAD, tmp_path)).has_checksum is False


def _dctx(arc, _, **kw):
    with zxc.Dctx(**kw) as d:
        return d.decompress(arc)


def _dstream(arc, _, **kw):
    with zxc.DStream(**kw) as d:
        return d.decompress(arc)


def _reader(arc, _, **kw):
    with zxc.ZxcReader(io.BytesIO(arc), **kw) as r:
        return r.read()


def _stream_decompress(arc, tmp_path, **kw):
    src, out = tmp_path / "arc", tmp_path / "out"
    src.write_bytes(arc)
    with open(src, "rb") as s, open(out, "wb") as d:
        zxc.stream_decompress(s, d, **kw)
    return out.read_bytes()


def _seekable(arc, _, **kw):
    with zxc.Seekable(arc, **kw) as s:
        return s.decompress_range(0, s.decompressed_size)


READ_PATHS = pytest.mark.parametrize(
    "read",
    [
        lambda a, _, **kw: zxc.decompress(a, **kw),
        _dctx,
        _dstream,
        _reader,
        _stream_decompress,
        _seekable,
    ],
    ids=["decompress", "Dctx", "DStream", "ZxcReader", "stream_decompress", "Seekable"],
)


def _archive_with_bad_block_checksum():
    arc = bytearray(zxc.compress(PAYLOAD, checksum=True, seekable=True))
    with zxc.Seekable(bytes(arc)) as s:
        block_end = s.block_compressed_size(0)
    arc[16 + block_end - 1] ^= 1  # last byte of the block: its checksum
    return bytes(arc)


@READ_PATHS
def test_default_read_rejects_a_bad_checksum(read, tmp_path):
    with pytest.raises(RuntimeError, match="BAD_CHECKSUM"):
        read(_archive_with_bad_block_checksum(), tmp_path)


@READ_PATHS
def test_opt_out_does_not_report_a_bad_checksum(read, tmp_path):
    arc = _archive_with_bad_block_checksum()
    assert read(arc, tmp_path, checksum=False) == PAYLOAD


@READ_PATHS
def test_archive_without_checksum_decodes(read, tmp_path):
    assert read(zxc.compress(PAYLOAD, seekable=True), tmp_path) == PAYLOAD


@READ_PATHS
def test_none_keeps_verification(read, tmp_path):
    with pytest.raises(RuntimeError, match="BAD_CHECKSUM"):
        read(_archive_with_bad_block_checksum(), tmp_path, checksum=None)


@pytest.mark.parametrize(
    "write",
    [
        lambda d: zxc.compress(d, checksum=None),
        lambda d: zxc.Cctx(checksum=None).compress(d),
        lambda d: (lambda c: c.compress(d) + c.end())(zxc.CStream(checksum=None)),
    ],
    ids=["compress", "Cctx", "CStream"],
)
def test_none_writes_no_checksum(write):
    assert zxc.get_frame_info(write(PAYLOAD)).has_checksum is False


def test_none_writes_no_checksum_through_files(tmp_path):
    sink = io.BytesIO()
    with zxc.ZxcWriter(sink, checksum=None) as w:
        w.write(PAYLOAD)
    assert zxc.get_frame_info(sink.getvalue()).has_checksum is False

    src, arc = tmp_path / "in", tmp_path / "arc"
    src.write_bytes(PAYLOAD)
    with open(src, "rb") as s, open(arc, "wb") as d:
        zxc.stream_compress(s, d, checksum=None)
    assert zxc.get_frame_info(arc.read_bytes()).has_checksum is False
