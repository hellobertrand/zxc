# SPDX-License-Identifier: BSD-3-Clause
"""
ZXC - High-performance lossless compression

Copyright (c) Bertrand Lebonnois and contributors.
"""

import errno
import io
import os

import pytest
import zxc


@pytest.mark.parametrize(
    "src,dst,expected_error,match",
    [
        (
            "not a file",
            io.BytesIO(),
            ValueError,
            "src and dst must be open file-like objects",
        ),
        (
            io.BytesIO(b"data"),
            "not a file",
            ValueError,
            "src and dst must be open file-like objects",
        ),
        (io.BytesIO(b"data"), io.BytesIO(), ValueError, "Source file must be readable"),
        (
            io.BytesIO(b"data"),
            io.BytesIO(),
            ValueError,
            "Destination file must be writable",
        ),
        (None, None, None, "valid_file"),
    ],
    ids=[
        "src_not_file",
        "dst_not_file",
        "src_not_readable",
        "dst_not_writable",
        "valid_file",
    ],
)
def test_stream_invalid_src_dst(tmp_path, src, dst, expected_error, match):
    # Helper class to mock fileno behavior
    class MockFile(io.BytesIO):
        def fileno(self):
            return 1

    class NotReadable(MockFile):
        def readable(self):
            return False

    class NotWritable(MockFile):
        def writable(self):
            return False

    if match == "Source file must be readable":
        src = NotReadable(b"data")
        dst = MockFile()
    elif match == "Destination file must be writable":
        src = MockFile(b"data")
        dst = NotWritable()
    elif match == "valid_file":
        src_path = tmp_path / "src.txt"
        dst_path = tmp_path / "dst.zxc"
        src_path.write_bytes(b"hello world")
        src = open(src_path, "rb")
        dst = open(dst_path, "wb")

    try:
        if not expected_error:
            _ = zxc.stream_compress(src, dst)
        else:
            with pytest.raises(expected_error, match=match):
                zxc.stream_compress(src, dst)
    finally:
        if hasattr(src, "close"):
            src.close()
        if hasattr(dst, "close"):
            dst.close()


def _footer_len(arc):
    """Footer bytes of a checksummed archive: digest, the two sizes, lengths."""
    lens = arc[-1]
    return 8 + (lens & 7) + (lens >> 4) + 3


def _flip(arc, at):
    return arc[:at] + bytes([arc[at] ^ 1]) + arc[at + 1 :]


@pytest.mark.parametrize(
    "data,corrupt_func,exc",
    [
        # Flip the last byte of the block's checksum, before the 8-byte EOF
        # block and the footer.
        (
            b"hello world" * 10,
            lambda x: _flip(x, len(x) - _footer_len(x) - 9),
            RuntimeError,
        ),
        (b"a" * 10, lambda x: b"", RuntimeError),
    ],
    ids=["corrupted_data", "invalid_header"],
)
def test_stream_compress_corruption(tmp_path, data, corrupt_func, exc):
    src_file_path = tmp_path / "src.bin"
    compressed_file_path = tmp_path / "compressed.zxc"
    decompressed_file_path = tmp_path / "decompressed.bin"

    src_file_path.write_bytes(data)

    with open(src_file_path, "rb") as src, open(compressed_file_path, "wb") as dst:
        zxc.stream_compress(src, dst, checksum=True)

    compressed_bytes = compressed_file_path.read_bytes()
    corrupted_bytes = corrupt_func(compressed_bytes)
    compressed_file_path.write_bytes(corrupted_bytes)

    with pytest.raises(exc):
        with open(compressed_file_path, "rb") as src, open(
            decompressed_file_path, "wb"
        ) as dst:
            zxc.stream_decompress(src, dst, checksum=True)


@pytest.mark.parametrize(
    "data",
    [
        b"hello world" * 10,  # normal
        b"a",  # single byte
        b"",  # empty
        b"a" * 10_000_000,  # large
    ],
    ids=["normal", "single_byte", "empty", "large_10mb"],
)
def test_stream_roundtrip(tmp_path, data):
    src_file_path = tmp_path / "src.txt"
    compressed_file_path = tmp_path / "compressed.zxc"
    decompressed_file_path = tmp_path / "decompressed.txt"

    src_file_path.write_bytes(data)

    for level in range(zxc.LEVEL_FASTEST, zxc.LEVEL_ULTRA + 1):
        with open(src_file_path, "rb") as src, open(
            compressed_file_path, "wb"
        ) as compressed:
            zxc.stream_compress(src, compressed, level=level)

        with open(compressed_file_path, "rb") as compressed, open(
            decompressed_file_path, "wb"
        ) as decompressed:
            zxc.stream_decompress(compressed, decompressed)

        decompressed_data = decompressed_file_path.read_bytes()

        assert decompressed_data == data
        assert len(decompressed_data) == len(data)


def _closed_pipe():
    """A writable file whose reader is gone: every write to it fails."""
    r, w = os.pipe()
    os.close(r)
    return open(w, "wb")


@pytest.mark.parametrize("direction", ["compress", "decompress"])
def test_stream_write_failure_at_flush(tmp_path, direction):
    # A small output stays in the stdio buffer until the final flush, the only
    # write that reaches the pipe: it must raise instead of returning a size.
    src_path = tmp_path / "src.bin"
    src_path.write_bytes(b"hello world" * 10)
    fn = zxc.stream_compress
    if direction == "decompress":
        arc_path = tmp_path / "src.zxc"
        with open(src_path, "rb") as src, open(arc_path, "wb") as dst:
            zxc.stream_compress(src, dst)
        src_path, fn = arc_path, zxc.stream_decompress

    with open(src_path, "rb") as src, _closed_pipe() as dst:
        with pytest.raises(RuntimeError, match="ZXC_ERROR_IO"):
            fn(src, dst)


PAYLOAD = bytes(range(256)) * 400  # larger than a BufferedReader's read-ahead


@pytest.mark.parametrize("size", [1000, len(PAYLOAD)])
def test_stream_compress_starts_at_logical_position(tmp_path, size):
    src_path, arc_path = tmp_path / "src", tmp_path / "arc"
    src_path.write_bytes(PAYLOAD[:size])
    with open(src_path, "rb") as src, open(arc_path, "wb") as dst:
        assert src.read(10) == PAYLOAD[:10]
        zxc.stream_compress(src, dst)
        # Resynced: the old read-ahead must not come back.
        assert src.tell() == size
        assert src.read() == b""
    assert zxc.decompress(arc_path.read_bytes()) == PAYLOAD[10:size]


def test_stream_compress_after_seek_inside_read_ahead(tmp_path):
    src_path, arc_path = tmp_path / "src", tmp_path / "arc"
    src_path.write_bytes(PAYLOAD)
    with open(src_path, "rb") as src, open(arc_path, "wb") as dst:
        src.read(1)
        src.seek(0)
        zxc.stream_compress(src, dst)
    assert zxc.decompress(arc_path.read_bytes()) == PAYLOAD


def test_stream_decompress_starts_at_logical_position(tmp_path):
    arc = zxc.compress(PAYLOAD)
    arc_path, out_path = tmp_path / "arc", tmp_path / "out"
    arc_path.write_bytes(b"HEADER" + arc)
    with open(arc_path, "rb") as src, open(out_path, "wb") as dst:
        assert src.read(6) == b"HEADER"
        zxc.stream_decompress(src, dst)
        assert src.read() == b""
    assert out_path.read_bytes() == PAYLOAD


def test_stream_source_resynced_after_failure(tmp_path):
    src_path, out_path = tmp_path / "src", tmp_path / "out"
    src_path.write_bytes(b"garbage" * 3000)
    with open(src_path, "rb") as src, open(out_path, "wb") as dst:
        src.read(1)
        with pytest.raises(RuntimeError):
            zxc.stream_decompress(src, dst)
        assert src.tell() == os.lseek(src.fileno(), 0, os.SEEK_CUR)


@pytest.mark.parametrize("fn", [zxc.stream_compress, zxc.stream_decompress])
def test_stream_refuses_text_source(tmp_path, fn):
    src_path = tmp_path / "src"
    src_path.write_bytes(b"data")
    with open(src_path, "r") as src, open(tmp_path / "out", "wb") as dst:
        with pytest.raises(ValueError, match="binary mode"):
            fn(src, dst)


def _feed_pipe(data):
    r, w = os.pipe()
    with open(w, "wb") as f:
        f.write(data)
    return open(r, "rb")


def test_stream_accepts_untouched_pipe(tmp_path):
    arc_path = tmp_path / "arc"
    with _feed_pipe(b"x" * 4000) as src, open(arc_path, "wb") as dst:
        zxc.stream_compress(src, dst)
    assert zxc.decompress(arc_path.read_bytes()) == b"x" * 4000


def test_stream_decompress_with_dictionary(tmp_path):
    samples = [(b"sample %d lorem ipsum dolor sit amet " % i) * 30 for i in range(200)]
    d = zxc.Dictionary.train(samples)
    data = samples[3] * 10
    src_path, arc_path, out_path = tmp_path / "s", tmp_path / "a", tmp_path / "o"
    src_path.write_bytes(data)
    with open(src_path, "rb") as src, open(arc_path, "wb") as dst:
        zxc.stream_compress(src, dst, dict=d)

    def run(**kwargs):
        with open(arc_path, "rb") as src, open(out_path, "wb") as dst:
            return zxc.stream_decompress(src, dst, **kwargs)

    assert run(dict=d) == len(data)
    assert out_path.read_bytes() == data
    assert run(dict=d.content, dict_huf=d.huf) == len(data)
    with pytest.raises(RuntimeError, match="DICT_REQUIRED"):
        run()
    with pytest.raises(RuntimeError, match="DICT_MISMATCH"):
        run(dict=b"other" * 100)
    with pytest.raises(ValueError, match="128 bytes"):
        run(dict=d.content, dict_huf=b"short")
    with pytest.raises(TypeError):
        run(dict=1)


@pytest.mark.parametrize("fn", [zxc.stream_compress, zxc.stream_decompress])
def test_stream_memory_file_keeps_its_error(tmp_path, fn):
    # src fails first: the exception must surface, with dst left uncalled.
    with pytest.raises(io.UnsupportedOperation):
        fn(io.BytesIO(b"data"), io.BytesIO())
    with open(tmp_path / "s", "wb+") as src, pytest.raises(io.UnsupportedOperation):
        fn(src, io.BytesIO())


class _NoSeekEnd(io.FileIO):
    def seek(self, pos, whence=os.SEEK_SET):
        if whence == os.SEEK_END:
            raise OSError(errno.EINVAL, "no SEEK_END")
        return super().seek(pos, whence)


def test_stream_source_refusing_seek_end(tmp_path):
    src_path, arc_path = tmp_path / "src", tmp_path / "arc"
    src_path.write_bytes(PAYLOAD)
    with io.BufferedReader(_NoSeekEnd(src_path)) as src, open(arc_path, "wb") as dst:
        src.read(10)
        zxc.stream_compress(src, dst)
        assert src.read() == b""
    assert zxc.decompress(arc_path.read_bytes()) == PAYLOAD[10:]


@pytest.mark.skipif(not os.path.exists("/proc/cpuinfo"), reason="needs /proc")
def test_stream_compress_proc_file(tmp_path):
    arc_path = tmp_path / "arc"
    with open("/proc/cpuinfo", "rb") as f:
        expected = f.read()
    with open("/proc/cpuinfo", "rb") as src, open(arc_path, "wb") as dst:
        zxc.stream_compress(src, dst)
    assert zxc.decompress(arc_path.read_bytes()) == expected
