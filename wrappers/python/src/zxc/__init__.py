"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
"""

from ._zxc import (
    pyzxc_compress,
    pyzxc_decompress,
    pyzxc_stream_compress,
    pyzxc_stream_decompress,
    pyzxc_get_decompressed_size,
    LEVEL_FASTEST,
    LEVEL_FAST,
    LEVEL_DEFAULT,
    LEVEL_BALANCED,
    LEVEL_COMPACT,
)

try:
    from ._version import __version__
except ImportError:
    __version__ = "0.0.0-dev"
    
__all__ = [
    # Functions
    "compress",
    "decompress",
    "stream_compress",
    "stream_decompress",
    "pyzxc_get_decompressed_size",

    # Constants 
    "LEVEL_FASTEST",
    "LEVEL_FAST",
    "LEVEL_DEFAULT",
    "LEVEL_BALANCED",
    "LEVEL_COMPACT",
]

def compress(data, level = LEVEL_DEFAULT, checksum = False) -> bytes:
    """Compress a bytes object.

    Args:
        data: Bytes-like object to compress.
        level: Compression level. Use constants like LEVEL_FASTEST, LEVEL_DEFAULT, etc.
        checksum: If True, append a checksum for integrity verification.

    Returns:
        Compressed bytes.
    
    Note:
        This function operates entirely in-memory. For streaming files, use `stream_compress`.
    """
    if len(data) == 0 and not checksum:
        return data
    return pyzxc_compress(data, level, checksum)


def get_decompressed_size(data: bytes) -> int:
    """Get the original decompressed size of a ZXC compressed buffer.

    Args:
        data (bytes): Compressed bytes buffer.

    Returns:
        int: Original uncompressed size in bytes, or 0 if the buffer is invalid or too small.
    
    Note:
        This function does not decompress the data, it only reads the footer for size info.
    """
    return pyzxc_get_decompressed_size(data)

def decompress(data, decompress_size=None, checksum=False) -> bytes:
    """Decompress a bytes object.

    Args:
        data: Compressed bytes.
        decompress_size: Expected size. If None, read from header (slower/safer).
        checksum: If True, verify the checksum appended during compression.

    Returns:
        Decompressed bytes.
    """
    if len(data) == 0 and not checksum:
        return data

    if decompress_size is None:
        decompress_size = get_decompressed_size(data)
        if decompress_size == 0:
            raise ValueError(
                "Invalid ZXC header or data too short to determine size"
            )

    return pyzxc_decompress(data, decompress_size, checksum)

def stream_compress(src, dst, n_threads=0, level=LEVEL_DEFAULT, checksum=False) -> int:
    """Compress data from src to dst (file-like objects).

    Args:
        src: Readable file-like object with `fileno()` support (e.g., open file).
        dst: Writable file-like object with `fileno()` support.
        n_threads: Number of threads to use for compression. 0 uses default.
        level: Compression level. Use constants like LEVEL_FASTEST, LEVEL_DEFAULT, etc.
        checksum: If True, append a checksum for integrity verification.

    Returns:
        Number of bytes written to `dst`.

    Note:
        In-memory streams like `io.BytesIO` are not supported.
        Use the in-memory `compress`/`decompress` functions for buffers.
    """
    if not hasattr(src, "fileno") or not hasattr(dst, "fileno"):
        raise ValueError("src and dst must be open file-like objects")

    if not src.readable():
        raise ValueError("Source file must be readable")

    if not dst.writable():
        raise ValueError("Destination file must be writable")

    # CRITICAL: Flush Python buffers before passing FDs to C
    # to prevent data reordering/corruption.
    if hasattr(src, "flush"):
        src.flush()
    if hasattr(dst, "flush"):
        dst.flush()

    return pyzxc_stream_compress(src, dst, n_threads, level, checksum)

def stream_decompress(src, dst, n_threads=0, checksum=False) -> int:
    """Decompress data from src to dst (file-like objects).

    Args:
        src: Readable file-like object with `fileno()` support.
        dst: Writable file-like object with `fileno()` support.
        n_threads: Number of threads to use for decompression. 0 uses default.
        checksum: If True, verify the checksum appended during compression.

    Returns:
        Number of bytes written to `dst`.

    Note:
        In-memory streams like `io.BytesIO` are not supported.
        Use the in-memory `compress`/`decompress` functions for buffers.
    """
    if not hasattr(src, "fileno") or not hasattr(dst, "fileno"):
        raise ValueError("src and dst must be open file-like objects")

    if not src.readable():
        raise ValueError("Source file must be readable")

    if not dst.writable():
        raise ValueError("Destination file must be writable")

    # CRITICAL: Flush Python buffers before passing FDs to C
    # to prevent data reordering/corruption.
    if hasattr(src, "flush"):
        src.flush()
    if hasattr(dst, "flush"):
        dst.flush()

    return pyzxc_stream_decompress(src, dst, n_threads, checksum)