# ZXC Python Bindings

High-performance Python bindings for the **ZXC** asymmetric compressor, optimized for **fast decompression**.  
Designed for *Write Once, Read Many* workloads like ML datasets, game assets, and caches.

## Features

- **Blazing fast decompression** - ZXC is specifically optimized for read-heavy workloads.
- **Buffer protocol support** - works with `bytes`, `bytearray`, `memoryview`, and even NumPy arrays.
- **Releases the GIL** during compression/decompression - true parallelism with Python threads.
- **Stream helpers** - compress/decompress file-like objects.

## Installation (from source)

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc/wrappers/python
python -m venv .venv
source .venv/bin/activate 
pip install .
```

## Quick Start

```python
import zxc

data = b"hello zxc" * 1000

blob = zxc.compress(data)                 # level, checksum and dict are optional
assert zxc.decompress(blob) == data
```

`compress()` and `decompress()` accept any object supporting the buffer
protocol - `bytes`, `bytearray`, `memoryview`, NumPy arrays - and release the
GIL, so Python threads run them in true parallel.

## Compression Levels

```python
zxc.compress(data, level=zxc.LEVEL_FASTEST)   # fastest, largest output
zxc.compress(data, level=zxc.LEVEL_DEFAULT)   # the default
zxc.compress(data, level=zxc.LEVEL_ULTRA)     # smallest output, slowest
```

`LEVEL_FAST`, `LEVEL_BALANCED`, `LEVEL_COMPACT` and `LEVEL_DENSITY` sit between
them. `min_level()`, `max_level()` and `default_level()` return the bounds at
runtime.

## Streaming Files

`stream_compress()` and `stream_decompress()` work on objects exposing a real
file descriptor via `fileno()`, so in-memory streams such as `io.BytesIO` are
not accepted. Both return the number of bytes written and take `n_threads=0` to
mean "one per available core".

```python
with open("input.bin", "rb") as src, open("output.zxc", "wb") as dst:
    written = zxc.stream_compress(src, dst, level=zxc.LEVEL_DEFAULT)

with open("output.zxc", "rb") as src, open("restored.bin", "wb") as dst:
    zxc.stream_decompress(src, dst)
```

For in-memory streams, use the push-based `CStream` / `DStream` instead:

```python
with zxc.CStream(level=zxc.LEVEL_DEFAULT) as cs:
    out = cs.compress(chunk) + cs.end()
```

## File Objects

`ZxcReader` and `ZxcWriter` are `io.RawIOBase` adapters, so they compose with
`io.BufferedReader` and the rest of the `io` stack:

```python
with open("output.zxc", "wb") as f, zxc.ZxcWriter(f) as w:
    w.write(data)

with open("output.zxc", "rb") as f, zxc.ZxcReader(f) as r:
    restored = r.read()
```

`detect_zxc(data)` reports whether a buffer starts with a ZXC file header.

## Random Access

A seekable archive can be decompressed block by block, without reading what
comes before:

```python
blob = ...  # produced with stream_compress(..., seekable=True)

with zxc.Seekable(blob) as s:
    print(s.num_blocks, s.decompressed_size)
    middle = s.decompress_range(offset=1 << 20, length=4096)
```

## Pre-Trained Dictionaries

Dictionaries lift the ratio on many small, similar payloads. The same
dictionary is required at both ends:

```python
d = zxc.Dictionary.train(samples)          # samples: list[bytes]
blob = zxc.compress(data, dict=d.content, dict_huf=d.huf)
back = zxc.decompress(blob, dict=d.content, dict_huf=d.huf)

open("corpus.zxd", "wb").write(d.save())   # persist
d2 = zxc.Dictionary.load(open("corpus.zxd", "rb").read())
```

`get_dict_id(archive)` returns the dictionary id an archive was built with, so
you can pick the right one before decompressing.

## Testing

```bash
cd zxc/wrappers/python
pip install pytest
pytest tests/ -v
```

## License

BSD-3-Clause - see [LICENSE](../../LICENSE).
