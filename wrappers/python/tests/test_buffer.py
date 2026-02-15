"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
"""

import pytest
import zxc

@pytest.mark.parametrize("data", [
    None,
    "string",
    123,
    12.5,
    object(),
])

def test_compress_invalid_type(data):
    with pytest.raises(TypeError):
        zxc.compress(data)


@pytest.mark.parametrize(
    "data,corrupt_func,exc",
    [
        (b"hello world" * 10, lambda x: x[:-1] + b"\x01", RuntimeError),  
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
        b"hello world" * 10,   # default
        b"a",                  # single byte 
        b"",
        b"a" * 10_000_000,     # large data 
    ],
    ids=[
        "normal_data",
        "single_byte",
        "empty",
        "large_10mb",
    ],
)

def test_compress_roundtrip(data):
    # test all compression levels
    levels_cnt = zxc.LEVEL_COMPACT 
    for level in range(levels_cnt + 1):
        compressed = zxc.compress(data, level)
    
        out_size = zxc.get_decompressed_size(compressed)
        decompressed = zxc.decompress(compressed, out_size) 
        assert len(data) == len(decompressed)
        assert data == decompressed
    

