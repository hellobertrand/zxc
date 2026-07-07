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
    pyzxc_train_dict,
    pyzxc_dict_id,
    pyzxc_get_dict_id,
    pyzxc_dict_get_id,
    pyzxc_dict_save,
    pyzxc_dict_load,
    pyzxc_train_dict_huf,
    pyzxc_dict_huf,
    pyzxc_dict_train,
    pyzxc_seekable_set_dict,
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
    LEVEL_ULTRA,
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

    # Dictionary
    "train_dict",
    "dict_id",
    "get_dict_id",
    "dict_get_id",
    "dict_save",
    "dict_load",
    "train_dict_huf",
    "dict_huf",
    "Dictionary",

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
    "LEVEL_ULTRA",

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
    """Return the maximum supported compression level (currently 7)."""
    return pyzxc_max_level()


def default_level() -> int:
    """Return the default compression level (currently 3)."""
    return pyzxc_default_level()


def library_version() -> str:
    """Return the version string reported by the linked native libzxc
    (e.g. ``"0.12.0"``). Distinct from the Python package ``__version__``."""
    return pyzxc_version_string()


def train_dict(samples, max_size: int = 65535) -> bytes:
    """Train a dictionary from a corpus of sample buffers.

    Args:
        samples: A sequence (list/tuple) of bytes-like objects to train from.
        max_size: Maximum dictionary size in bytes (<= 65535).

    Returns:
        Raw dictionary content bytes, suitable for ``compress(..., dict=...)``
        or serialization with :func:`dict_save`.
    """
    return pyzxc_train_dict(samples, max_size)


def dict_id(content: bytes) -> int:
    """Return the 32-bit dictionary ID for raw dictionary *content*.

    Returns 0 if *content* is empty.
    """
    return pyzxc_dict_id(content)


def get_dict_id(archive: bytes) -> int:
    """Return the dictionary ID a ``.zxc`` *archive* requires (0 if none).

    Only the first 16 bytes of the archive are needed.
    """
    return pyzxc_get_dict_id(archive)


def dict_get_id(zxd: bytes) -> int:
    """Return the dictionary ID stored in a ``.zxd`` file (0 if not valid)."""
    return pyzxc_dict_get_id(zxd)


def dict_save(content: bytes, huf_lengths: bytes) -> bytes:
    """Serialize dictionary *content* and its shared literal Huffman table
    (128 bytes, from :func:`train_dict_huf`) into ``.zxd`` file bytes.

    The stored dictionary ID covers both content and table.
    """
    return pyzxc_dict_save(content, huf_lengths)


def train_dict_huf(samples, dict: bytes) -> bytes:
    """Train the shared literal Huffman table for an already-trained *dict*.

    Compresses *samples* with the dictionary and derives canonical code
    lengths from the real post-LZ literal distribution. Returns the 128-byte
    packed table required by :func:`dict_save` and usable as the ``dict_huf``
    argument of :func:`compress` / :func:`decompress`.
    """
    return pyzxc_train_dict_huf(samples, dict)


def dict_huf(zxd: bytes):
    """Return the 128-byte shared Huffman table stored in a ``.zxd`` file,
    or ``None`` if the buffer is not a valid ``.zxd`` file."""
    return pyzxc_dict_huf(zxd)


def dict_load(zxd: bytes):
    """Parse a ``.zxd`` file and return ``(content, dict_id)``.

    The returned content is an owned copy of the dictionary bytes. Prefer
    :class:`Dictionary` (``Dictionary.load(zxd)``) for the full
    (content, table, id) bundle.
    """
    content, _huf, dict_id = pyzxc_dict_load(zxd)
    return content, dict_id


class Dictionary:
    """A trained dictionary: LZ-window content plus its shared literal Huffman
    table, bundled so callers never juggle the pair by hand.

    Create with :meth:`Dictionary.train` (from samples) or
    :meth:`Dictionary.load` (from ``.zxd`` bytes), then pass the instance as
    the ``dict`` argument of :func:`compress` / :func:`decompress` /
    :func:`stream_compress` or :meth:`Seekable.set_dict`.
    """

    __slots__ = ("content", "huf", "id")

    def __init__(self, content: bytes, huf: bytes, id: int):
        if len(huf) != 128:
            raise ValueError("huf must be exactly 128 bytes")
        self.content = content
        self.huf = huf
        self.id = id

    @classmethod
    def train(cls, samples) -> "Dictionary":
        """Train a complete dictionary (content + shared table) from samples."""
        return cls.load(pyzxc_dict_train(samples))

    @classmethod
    def load(cls, zxd: bytes) -> "Dictionary":
        """Parse ``.zxd`` bytes into a Dictionary (owned copies)."""
        content, huf, dict_id = pyzxc_dict_load(zxd)
        return cls(content, huf, dict_id)

    def save(self) -> bytes:
        """Serialize back to ``.zxd`` file bytes."""
        return dict_save(self.content, self.huf)

    def __eq__(self, other):
        return (isinstance(other, Dictionary) and self.content == other.content
                and self.huf == other.huf)

    def __repr__(self):
        return f"Dictionary(id=0x{self.id:08X}, content={len(self.content)} bytes)"


def _split_dict_arg(dict, dict_huf):
    """Accept either a Dictionary or raw (content, table) pieces."""
    if isinstance(dict, Dictionary):
        return dict.content, dict.huf
    return dict, dict_huf


def compress(data, level = LEVEL_DEFAULT, checksum = False, dict = None, dict_huf = None) -> bytes:
    """Compress a bytes object.

    Args:
        data: Bytes-like object to compress.
        level: Compression level. Use constants like LEVEL_FASTEST, LEVEL_DEFAULT, etc.
        checksum: If True, append a checksum for integrity verification.
        dict: Optional pre-trained dictionary content (bytes) to prime the
            compressor. Must be passed (matching by ID) to `decompress`.
        dict_huf: Optional shared literal Huffman table (128 bytes, from
            `train_dict_huf` or `dict_huf`). Ignored without `dict`; becomes
            part of the archive's dictionary binding.

    Returns:
        Compressed bytes.

    Note:
        This function operates entirely in-memory. For streaming files, use `stream_compress`.
    """
    dict, dict_huf = _split_dict_arg(dict, dict_huf)
    return pyzxc_compress(data, level, checksum, dict, dict_huf)


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


def decompress(data, decompress_size=None, checksum=False, dict=None, dict_huf=None) -> bytes:
    """Decompress a bytes object.

    Args:
        data: Compressed bytes.
        decompress_size: Expected size. If None, read from header (slower/safer).
        checksum: If True, verify the checksum appended during compression.
        dict: Pre-trained dictionary content (bytes) required if the archive
            was compressed with one. Must match the dictionary ID stored in
            the archive header.
        dict_huf: Shared literal Huffman table (128 bytes) when the archive
            was compressed with one (the dictionary ID binds the pair).

    Returns:
        Decompressed bytes.
    """
    if decompress_size is None:
        decompress_size = get_decompressed_size(data)

    dict, dict_huf = _split_dict_arg(dict, dict_huf)
    return pyzxc_decompress(data, decompress_size, checksum, dict, dict_huf)

def stream_compress(src, dst, n_threads=0, level=LEVEL_DEFAULT, checksum=False, seekable=False, dict=None, dict_huf=None) -> int:
    """Compress data from src to dst (file-like objects).

    Args:
        src: Readable file-like object with `fileno()` support (e.g., open file).
        dst: Writable file-like object with `fileno()` support.
        n_threads: Number of threads to use for compression. 0 uses default.
        level: Compression level. Use constants like LEVEL_FASTEST, LEVEL_DEFAULT, etc.
        checksum: If True, append a checksum for integrity verification.
        seekable: If True, append a seek table for random-access decompression.
        dict: Optional pre-trained dictionary content (bytes). The archive
            records its dictionary ID; decompression must supply the same dict.

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

    dict, dict_huf = _split_dict_arg(dict, dict_huf)
    return pyzxc_stream_compress(src, dst, n_threads, level, checksum, seekable, dict, dict_huf)

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

    def _ensure_open(self) -> None:
        if self._handle is None:
            raise ValueError("Seekable is closed")

    @property
    def num_blocks(self) -> int:
        self._ensure_open()
        return pyzxc_seekable_num_blocks(self._handle)

    @property
    def decompressed_size(self) -> int:
        self._ensure_open()
        return pyzxc_seekable_decompressed_size(self._handle)

    def block_compressed_size(self, block_idx: int):
        self._ensure_open()
        return pyzxc_seekable_block_comp_size(self._handle, block_idx)

    def block_decompressed_size(self, block_idx: int):
        self._ensure_open()
        return pyzxc_seekable_block_decomp_size(self._handle, block_idx)

    def set_dict(self, dict: bytes, dict_huf: bytes | None = None) -> None:
        """Attach a pre-trained dictionary to this seekable handle.

        Required before :meth:`decompress_range` when the archive was
        compressed with a dictionary. The content is copied internally, so
        *dict* may be freed after this call.
        """
        self._ensure_open()
        dict, dict_huf = _split_dict_arg(dict, dict_huf)
        pyzxc_seekable_set_dict(self._handle, dict, dict_huf)

    def decompress_range(self, offset: int, length: int, *, n_threads: int = 0) -> bytes:
        self._ensure_open()
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