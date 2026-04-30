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
    pyzxc_min_level,
    pyzxc_max_level,
    pyzxc_default_level,
    pyzxc_version_string,
    pyzxc_cstream_create,
    pyzxc_cstream_compress,
    pyzxc_cstream_end,
    pyzxc_cstream_in_size,
    pyzxc_cstream_out_size,
    pyzxc_cstream_free,
    pyzxc_dstream_create,
    pyzxc_dstream_decompress,
    pyzxc_dstream_finished,
    pyzxc_dstream_in_size,
    pyzxc_dstream_out_size,
    pyzxc_dstream_free,
    LEVEL_FASTEST,
    LEVEL_FAST,
    LEVEL_DEFAULT,
    LEVEL_BALANCED,
    LEVEL_COMPACT,
    ERROR_MEMORY,
    ERROR_DST_TOO_SMALL,
    ERROR_SRC_TOO_SMALL,
    ERROR_BAD_MAGIC,
    ERROR_BAD_VERSION,
    ERROR_BAD_HEADER,
    ERROR_BAD_CHECKSUM,
    ERROR_CORRUPT_DATA,
    ERROR_BAD_OFFSET,
    ERROR_OVERFLOW,
    ERROR_IO,
    ERROR_NULL_INPUT,
    ERROR_BAD_BLOCK_TYPE,
    ERROR_BAD_BLOCK_SIZE,
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
    "get_decompressed_size",

    # Push streaming
    "CStream",
    "DStream",

    # Library info helpers
    "min_level",
    "max_level",
    "default_level",
    "library_version",

    # Constants
    "LEVEL_FASTEST",
    "LEVEL_FAST",
    "LEVEL_DEFAULT",
    "LEVEL_BALANCED",
    "LEVEL_COMPACT",

    # Error Constants
    "ERROR_MEMORY",
    "ERROR_DST_TOO_SMALL",
    "ERROR_SRC_TOO_SMALL",
    "ERROR_BAD_MAGIC",
    "ERROR_BAD_VERSION",
    "ERROR_BAD_HEADER",
    "ERROR_BAD_CHECKSUM",
    "ERROR_CORRUPT_DATA",
    "ERROR_BAD_OFFSET",
    "ERROR_OVERFLOW",
    "ERROR_IO",
    "ERROR_NULL_INPUT",
    "ERROR_BAD_BLOCK_TYPE",
    "ERROR_BAD_BLOCK_SIZE",
]

def min_level() -> int:
    """Return the minimum supported compression level (currently 1)."""
    return pyzxc_min_level()


def max_level() -> int:
    """Return the maximum supported compression level (currently 5)."""
    return pyzxc_max_level()


def default_level() -> int:
    """Return the default compression level (currently 3)."""
    return pyzxc_default_level()


def library_version() -> str:
    """Return the version string reported by the linked native libzxc
    (e.g. ``"0.10.0"``). Distinct from the Python package ``__version__``."""
    return pyzxc_version_string()


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

def stream_compress(src, dst, n_threads=0, level=LEVEL_DEFAULT, checksum=False, seekable=False) -> int:
    """Compress data from src to dst (file-like objects).

    Args:
        src: Readable file-like object with `fileno()` support (e.g., open file).
        dst: Writable file-like object with `fileno()` support.
        n_threads: Number of threads to use for compression. 0 uses default.
        level: Compression level. Use constants like LEVEL_FASTEST, LEVEL_DEFAULT, etc.
        checksum: If True, append a checksum for integrity verification.
        seekable: If True, append a seek table for random-access decompression.

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

    return pyzxc_stream_compress(src, dst, n_threads, level, checksum, seekable)

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


class CStream:
    """Push-based, single-threaded compression stream.

    The Python counterpart of the C ``zxc_cstream``. Use this when the
    multi-threaded ``stream_compress`` (which takes ``FILE*``) is not
    appropriate — e.g. async event loops, in-memory streams (``io.BytesIO``),
    network protocols, or callback-driven libraries.

    The stream is not thread-safe. Each call to :meth:`compress` returns the
    compressed bytes produced from the input fed in this call (may be empty
    if the input was small enough to stay inside the internal block
    accumulator). :meth:`end` flushes the residual block, the EOF marker and
    the file footer; you MUST call it to produce a valid ZXC archive.

    Example::

        cs = zxc.CStream(level=zxc.LEVEL_DEFAULT, checksum=True)
        chunks = [cs.compress(part) for part in source]
        chunks.append(cs.end())
        archive = b"".join(chunks)
        cs.close()

    Supports the context-manager protocol::

        with zxc.CStream(level=3) as cs:
            out = cs.compress(data) + cs.end()
    """

    __slots__ = ("_handle",)

    def __init__(self, level: int = LEVEL_DEFAULT, checksum: bool = False, block_size: int = 0):
        self._handle = pyzxc_cstream_create(level, checksum, block_size)

    def compress(self, data) -> bytes:
        """Push *data* into the stream and return any compressed bytes
        produced this call.

        May return ``b""`` if the input fit entirely into the internal block
        accumulator. The returned bytes must be written verbatim to the sink
        in order.
        """
        if self._handle is None:
            raise ValueError("CStream is closed")
        return pyzxc_cstream_compress(self._handle, data)

    def end(self) -> bytes:
        """Finalise the stream and return the trailing bytes (residual
        block + EOF + file footer). Call exactly once after the last
        :meth:`compress`.
        """
        if self._handle is None:
            raise ValueError("CStream is closed")
        return pyzxc_cstream_end(self._handle)

    @property
    def in_size(self) -> int:
        """Suggested input chunk size (block size, in bytes)."""
        if self._handle is None:
            return 0
        return pyzxc_cstream_in_size(self._handle)

    @property
    def out_size(self) -> int:
        """Suggested output chunk size to never trigger a partial drain."""
        if self._handle is None:
            return 0
        return pyzxc_cstream_out_size(self._handle)

    def close(self) -> None:
        """Release native resources. Idempotent."""
        if self._handle is not None:
            pyzxc_cstream_free(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass


class DStream:
    """Push-based, single-threaded decompression stream.

    The Python counterpart of the C ``zxc_dstream``. Feed compressed bytes
    via :meth:`decompress`; each call returns the decompressed bytes
    produced so far. After all input has been fed, :attr:`finished` becomes
    ``True`` once the file footer is validated.

    Example::

        ds = zxc.DStream(checksum=True)
        out = b"".join(ds.decompress(chunk) for chunk in compressed_chunks)
        if not ds.finished:
            raise ValueError("truncated stream")
        ds.close()
    """

    __slots__ = ("_handle",)

    def __init__(self, checksum: bool = False):
        self._handle = pyzxc_dstream_create(checksum)

    def decompress(self, data) -> bytes:
        """Push *data* and return the decompressed bytes produced.

        May return ``b""`` if the parser is still waiting for more input
        (e.g. mid-header).
        """
        if self._handle is None:
            raise ValueError("DStream is closed")
        return pyzxc_dstream_decompress(self._handle, data)

    @property
    def finished(self) -> bool:
        """``True`` once the decoder has reached and validated the file
        footer. Useful to detect truncated input."""
        if self._handle is None:
            return False
        return pyzxc_dstream_finished(self._handle)

    @property
    def in_size(self) -> int:
        """Suggested input chunk size for the decompressor."""
        if self._handle is None:
            return 0
        return pyzxc_dstream_in_size(self._handle)

    @property
    def out_size(self) -> int:
        """Suggested output chunk size for the decompressor."""
        if self._handle is None:
            return 0
        return pyzxc_dstream_out_size(self._handle)

    def close(self) -> None:
        """Release native resources. Idempotent."""
        if self._handle is not None:
            pyzxc_dstream_free(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass