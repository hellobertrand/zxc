# ZXC Python Bindings

High-performance Python bindings for the **ZXC** asymmetric compressor, optimized for **fast decompression**.  
Designed for *Write Once, Read Many* workloads like ML datasets, game assets, and caches.

## Features

- **Blazing fast decompression** — ZXC is specifically optimized for read-heavy workloads.
- **Buffer protocol support** — works with `bytes`, `bytearray`, `memoryview`, and even NumPy arrays.
- **Releases the GIL** during compression/decompression — true parallelism with Python threads.
- **Stream helpers** — compress/decompress file-like objects.

## Installation (from source)

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc/wrappers/python
python -m venv .venv
source .venv/bin/activate 
pip install .