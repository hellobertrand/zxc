# SPDX-License-Identifier: BSD-3-Clause
"""
ZXC - High-performance lossless compression

Copyright (c) Bertrand Lebonnois and contributors.
"""

import pytest
import zxc


@pytest.mark.parametrize(
    "data",
    [
        None,
        "string",
        123,
        12.5,
        object(),
    ],
)
def test_compress_invalid_type(data):
    with pytest.raises(TypeError):
        zxc.compress(data)


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
def test_compress_corruption(data, corrupt_func, exc):
    compressed = zxc.compress(data, checksum=True)
    corrupted = corrupt_func(compressed)

    with pytest.raises(exc):
        zxc.decompress(corrupted, len(data), checksum=True)


@pytest.mark.parametrize(
    "data",
    [
        b"hello world" * 10,  # default
        b"a",  # single byte
        b"",
        b"a" * 10_000_000,  # large data
    ],
    ids=[
        "normal_data",
        "single_byte",
        "empty",
        "large_10mb",
    ],
)
def test_compress_roundtrip(data):
    # test all compression levels (1..LEVEL_ULTRA)
    for level in range(zxc.LEVEL_FASTEST, zxc.LEVEL_ULTRA + 1):
        compressed = zxc.compress(data, level)

        out_size = zxc.get_decompressed_size(compressed)
        decompressed = zxc.decompress(compressed, out_size)
        assert len(data) == len(decompressed)
        assert data == decompressed


@pytest.mark.parametrize("checksum", [False, True])
def test_get_frame_info(checksum):
    data = b"frame info " * 500
    comp = zxc.compress(data, checksum=checksum)
    info = zxc.get_frame_info(comp)
    assert info.decompressed_size == len(data)
    assert info.compressed_size == len(comp)
    assert info.has_checksum is checksum
    assert (info.digest != 0) is checksum
    assert info.has_seek_table is False
    assert info.dict_id == 0
    assert info.block_size >= 4096
    assert info.format_version > 0


def test_get_frame_info_invalid():
    comp = zxc.compress(b"x" * 100)
    with pytest.raises(RuntimeError):
        zxc.get_frame_info(comp + b"\0")  # the frame no longer spans the buffer
    with pytest.raises(RuntimeError):
        zxc.get_frame_info(b"not a zxc frame at all, long enough")
