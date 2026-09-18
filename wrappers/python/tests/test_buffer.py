"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
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


@pytest.mark.parametrize(
    "data,corrupt_func,exc",
    [
        # Flip the last byte of the block's checksum: it precedes the 8-byte
        # EOF block and the 16-byte footer (digest + size).
        (
            b"hello world" * 10,
            lambda x: x[:-25] + bytes([x[-25] ^ 1]) + x[-24:],
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


def test_max_output_size():
    data = b"hello world" * 100
    arc = zxc.compress(data)
    assert zxc.decompress(arc, max_output_size=len(data)) == data
    assert zxc.decompress(arc, max_output_size=None) == data

    # The cap fails exactly like a genuine undersized destination.
    with pytest.raises(RuntimeError) as genuine:
        zxc.decompress(arc, len(data) - 1)
    for kwargs in ({}, {"decompress_size": len(data)}):
        with pytest.raises(RuntimeError, match="ZXC_ERROR_DST_TOO_SMALL") as capped:
            zxc.decompress(arc, max_output_size=len(data) - 1, **kwargs)
        assert capped.value.args == genuine.value.args


def test_max_output_size_stops_a_bomb():
    data = bytes(64 << 20)
    arc = zxc.compress(data)
    with pytest.raises(RuntimeError, match="ZXC_ERROR_DST_TOO_SMALL"):
        zxc.decompress(arc, max_output_size=1 << 20)
    assert zxc.decompress(arc) == data


@pytest.mark.parametrize(
    "cap,exc", [(-1, ValueError), (1.5, TypeError), ("1", TypeError)]
)
def test_max_output_size_invalid(cap, exc):
    with pytest.raises(exc):
        zxc.decompress(zxc.compress(b"x"), max_output_size=cap)
