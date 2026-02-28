"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
"""

import pytest
import zxc
import io

@pytest.mark.parametrize(
    "src,dst,expected_error,match",
    [
        ("not a file", io.BytesIO(), ValueError, "src and dst must be open file-like objects"),
        (io.BytesIO(b"data"), "not a file", ValueError, "src and dst must be open file-like objects"),
        (io.BytesIO(b"data"), io.BytesIO(), ValueError, "Source file must be readable"),
        (io.BytesIO(b"data"), io.BytesIO(), ValueError, "Destination file must be writable"),
        (None, None, None, "valid_file"),
    ],
    ids=[
        "src_not_file",
        "dst_not_file",
        "src_not_readable",
        "dst_not_writable",
        "valid_file",
    ]
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


@pytest.mark.parametrize(
    "data,corrupt_func,exc",
    [
        (b"hello world" * 10, lambda x: x[:-1] + b"\x01", RuntimeError),
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
        with open(compressed_file_path, "rb") as src, open(decompressed_file_path, "wb") as dst:
            zxc.stream_decompress(src, dst, checksum=True)


@pytest.mark.parametrize("data", [
    b"hello world" * 10,   # normal
    b"a",                  # single byte
    b"",                   # empty
    b"a" * 10_000_000,     # large
], ids=["normal", "single_byte", "empty", "large_10mb"])

def test_stream_roundtrip(tmp_path, data):
    src_file_path = tmp_path / "src.txt"
    compressed_file_path = tmp_path / "compressed.zxc"
    decompressed_file_path = tmp_path / "decompressed.txt"

    src_file_path.write_bytes(data)

    levels_cnt = zxc.LEVEL_COMPACT 
    for level in range(levels_cnt + 1):
        with open(src_file_path, "rb") as src, \
            open(compressed_file_path, "wb") as compressed:
            zxc.stream_compress(src, compressed, level=level)

        with open(compressed_file_path, "rb") as compressed, \
            open(decompressed_file_path, "wb") as decompressed:
            zxc.stream_decompress(compressed, decompressed)

        decompressed_data = decompressed_file_path.read_bytes()

        assert decompressed_data == data
        assert len(decompressed_data) == len(data)