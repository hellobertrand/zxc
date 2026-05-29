"""
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
"""

import io as _io

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
    pyzxc_seekable_open,
    pyzxc_seekable_open_reader,
    pyzxc_seekable_num_blocks,
    pyzxc_seekable_decompressed_size,
    pyzxc_seekable_block_comp_size,
    pyzxc_seekable_block_decomp_size,
    pyzxc_seekable_decompress_range,
    pyzxc_seekable_free,
    pyzxc_seek_table_size,
    pyzxc_write_seek_table,
    LEVEL_FASTEST,
    LEVEL_FAST,
    LEVEL_DEFAULT,
    LEVEL_BALANCED,
    LEVEL_COMPACT,
    LEVEL_DENSITY,
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

    # Seekable random-access decompression
    "Seekable",
    "seek_table_size",
    "write_seek_table",

    # io.RawIOBase adapters
    "ZxcReader",
    "ZxcWriter",
    "detect_zxc",

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
    "LEVEL_DENSITY",

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
    (e.g. ``"0.11.0"``). Distinct from the Python package ``__version__``."""
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


class Seekable:
    """Random-access decompression of a seekable ZXC archive.

    Opens a seekable archive (one created with ``seekable=True``) and lets
    you decompress arbitrary byte ranges without reading the whole file.

    Two construction modes:

    **From bytes** (the buffer is copied internally)::

        with zxc.Seekable(compressed_bytes) as s:
            chunk = s.decompress_range(offset, length)

    **From a reader object** with ``size`` (int) and
    ``read_at(length, offset) -> bytes`` attributes::

        class MyReader:
            size = total_compressed_bytes
            def read_at(self, length, offset):
                ...  # return `length` bytes from `offset`

        with zxc.Seekable(MyReader()) as s:
            chunk = s.decompress_range(0, s.decompressed_size)
    """

    __slots__ = ("_handle",)

    def __init__(self, source):
        if isinstance(source, (bytes, bytearray, memoryview)):
            self._handle = pyzxc_seekable_open(source)
        elif hasattr(source, "size") and hasattr(source, "read_at"):
            self._handle = pyzxc_seekable_open_reader(source)
        else:
            raise TypeError(
                "expected bytes/bytearray/memoryview or an object with "
                "'size' (int) and 'read_at(length, offset)' attributes"
            )

    @property
    def num_blocks(self) -> int:
        if self._handle is None:
            raise ValueError("Seekable is closed")
        return pyzxc_seekable_num_blocks(self._handle)

    @property
    def decompressed_size(self) -> int:
        if self._handle is None:
            raise ValueError("Seekable is closed")
        return pyzxc_seekable_decompressed_size(self._handle)

    def block_compressed_size(self, block_idx: int):
        if self._handle is None:
            raise ValueError("Seekable is closed")
        return pyzxc_seekable_block_comp_size(self._handle, block_idx)

    def block_decompressed_size(self, block_idx: int):
        if self._handle is None:
            raise ValueError("Seekable is closed")
        return pyzxc_seekable_block_decomp_size(self._handle, block_idx)

    def decompress_range(self, offset: int, length: int, *, n_threads: int = 0) -> bytes:
        if self._handle is None:
            raise ValueError("Seekable is closed")
        return pyzxc_seekable_decompress_range(self._handle, offset, length, n_threads)

    def close(self) -> None:
        if self._handle is not None:
            pyzxc_seekable_free(self._handle)
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
            # Destructors must not raise: this is best-effort cleanup only.
            return


def seek_table_size(num_blocks: int) -> int:
    """Return the encoded byte size of a seek table for *num_blocks* blocks."""
    return pyzxc_seek_table_size(num_blocks)


def write_seek_table(comp_sizes: list) -> bytes:
    """Write a raw seek table from a list of per-block compressed sizes.

    Low-level helper — most callers should simply pass ``seekable=True`` to
    :func:`stream_compress` instead.
    """
    return pyzxc_write_seek_table(comp_sizes)


class CStream:
    """Push-based, single-threaded compression stream.

    The Python counterpart of the C ``zxc_cstream``. Use this when the
    multi-threaded ``stream_compress`` (which takes ``FILE*``) is not
    appropriate - e.g. async event loops, in-memory streams (``io.BytesIO``),
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


# ============================================================================
# io.RawIOBase adapters over the push streaming API
# ============================================================================
#
# Mirror of the wrappers shipped by the Go and Rust bindings: turn a
# CStream / DStream pair into Python's standard binary file-like protocol so
# ZXC can be plugged into pipelines that expect it — tarfile, requests,
# oras-py / OCI registry clients, etc.
#
# Wrap with ``io.BufferedReader`` / ``io.BufferedWriter`` for buffering when
# performance matters.


class ZxcReader(_io.RawIOBase):
    """Decompresses a ZXC frame read from a binary file-like object.

    Implements the standard :class:`io.RawIOBase` interface so it can be
    plugged into any code that expects a readable binary stream.

    Raises :class:`OSError` with ``errno=None`` if the underlying source is
    drained before the ZXC footer is reached (truncated frame).

    Example::

        with open("data.zxc", "rb") as f, zxc.ZxcReader(f) as r:
            payload = r.read()

    The wrapped reader is **not** closed by :meth:`close`.
    """

    def __init__(self, fileobj, *, checksum: bool = False, buffer_size: int = 0):
        super().__init__()
        if not hasattr(fileobj, "read"):
            raise TypeError("fileobj must have a .read() method")
        self._src = fileobj
        self._ds = DStream(checksum=checksum)
        if buffer_size <= 0:
            buffer_size = self._ds.in_size
        self._bufsize = buffer_size
        self._pending = b""    # decompressed bytes not yet returned
        self._inbuf = b""      # compressed bytes pulled from src, not yet fed
        self._eof_src = False  # True once src.read() returned empty bytes

    def readable(self) -> bool:
        return True

    def readinto(self, b) -> int:
        if self.closed:
            raise ValueError("I/O operation on closed reader")
        n_out = len(b)
        if n_out == 0:
            return 0

        while not self._pending:
            if self._ds.finished:
                return 0
            if not self._inbuf and not self._eof_src:
                chunk = self._src.read(self._bufsize)
                if not chunk:
                    self._eof_src = True
                else:
                    self._inbuf = chunk

            produced = self._ds.decompress(self._inbuf)
            self._inbuf = b""
            if produced:
                self._pending = produced
                break
            if self._eof_src:
                if self._ds.finished:
                    return 0
                raise OSError("zxc: input drained before footer (truncated stream)")

        take = min(n_out, len(self._pending))
        b[:take] = self._pending[:take]
        self._pending = self._pending[take:]
        return take

    def close(self) -> None:
        if self.closed:
            return
        try:
            self._ds.close()
        finally:
            super().close()


class ZxcWriter(_io.RawIOBase):
    """Compresses bytes written to it and forwards the ZXC frame to a binary
    file-like object.

    Implements the standard :class:`io.RawIOBase` interface.

    The frame is finalised (residual block, EOF marker, footer) when
    :meth:`close` is called. **You must close the writer to obtain a valid
    archive** — closing the wrapped file before the writer leaves a truncated
    frame on disk. Use the context-manager form to make this automatic::

        with open("out.zxc", "wb") as f, zxc.ZxcWriter(f) as w:
            w.write(payload)

    The wrapped writer is **not** closed by :meth:`close`.
    """

    def __init__(self, fileobj, *, level: int = LEVEL_DEFAULT, checksum: bool = False):
        super().__init__()
        if not hasattr(fileobj, "write"):
            raise TypeError("fileobj must have a .write() method")
        self._dst = fileobj
        self._cs = CStream(level=level, checksum=checksum)
        self._finalized = False

    def writable(self) -> bool:
        return True

    def write(self, b) -> int:
        if self.closed:
            raise ValueError("I/O operation on closed writer")
        if self._finalized:
            raise ValueError("ZxcWriter already finalised")
        mv = memoryview(b)
        if mv.nbytes == 0:
            return 0
        chunk = self._cs.compress(bytes(mv))
        if chunk:
            self._dst.write(chunk)
        return mv.nbytes

    def close(self) -> None:
        if self.closed:
            return
        try:
            if not self._finalized:
                tail = self._cs.end()
                if tail:
                    self._dst.write(tail)
                self._finalized = True
        finally:
            self._cs.close()
            super().close()


# Magic word identifying a ZXC file frame: little-endian 0x9CB02EF5.
_ZXC_MAGIC_LE = b"\xf5\x2e\xb0\x9c"


def detect_zxc(data) -> bool:
    """Return ``True`` if *data* starts with the ZXC file magic word.

    Useful for content-type sniffing in containers / object stores that need
    to dispatch on media type (e.g. OCI). Cheap and side-effect free; does
    not validate the rest of the header or the footer.
    """
    if data is None:
        return False
    mv = memoryview(data)
    return len(mv) >= 4 and bytes(mv[:4]) == _ZXC_MAGIC_LE