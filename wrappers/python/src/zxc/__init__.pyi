"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
"""

from typing import Protocol, Optional

# ---------- constants ----------
LEVEL_FASTEST: int
LEVEL_FAST: int
LEVEL_DEFAULT: int
LEVEL_BALANCED: int
LEVEL_COMPACT: int

# ---------- types ----------
class FileLike(Protocol):
    """File-like object with a file descriptor.
    
    Note:
        This excludes in-memory streams like io.BytesIO.
        Use real file objects or objects wrapping OS file descriptors.
    """
    def fileno(self) -> int: ...
    def readable(self) -> bool: ...
    def writable(self) -> bool: ...

# ---------- functions ----------
def compress(
    data: bytes,
    level: int = LEVEL_DEFAULT,
    checksum: bool = False
) -> bytes: ...

def decompress(
    data: bytes,
    decompress_size: Optional[int] = None,
    checksum: bool = False
) -> bytes: ...

def stream_compress(
    src: FileLike,
    dst: FileLike,
    n_threads: int = 0,
    level: int = LEVEL_DEFAULT,
    checksum: bool = False
) -> int: ...

def stream_decompress(
    src: FileLike,
    dst: FileLike,
    n_threads: int = 0,
    checksum: bool = False
) -> int: ...

def get_decompressed_size(data: bytes) -> int: ...