# ZXC Node.js Bindings

High-performance Node.js bindings for the **ZXC** asymmetric compressor, optimized for **fast decompression**.  
Designed for *Write Once, Read Many* workloads like ML datasets, game assets, and caches.

## Features

- **Blazing fast decompression** — ZXC is specifically optimized for read-heavy workloads.
- **Native N-API addon** — compiled C code via cmake-js for maximum performance.
- **Buffer support** — works directly with Node.js `Buffer` objects.
- **TypeScript declarations** — full type definitions included.

## Installation (from source)

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc/wrappers/nodejs
npm install
```

### Prerequisites

- Node.js >= 16.0.0
- CMake >= 3.14
- A C17/C++17 compiler (GCC, Clang, or MSVC)

## Usage

```javascript
const zxc = require('zxc-compress');

// Compress
const data = Buffer.from('Hello, World!'.repeat(1_000));
const compressed = zxc.compress(data, { level: zxc.LEVEL_DEFAULT });

// Decompress (auto-detects size)
const decompressed = zxc.decompress(compressed);

console.log(`Original: ${data.length} bytes`);
console.log(`Compressed: ${compressed.length} bytes`);
console.log(`Ratio: ${(compressed.length / data.length * 100).toFixed(1)}%`);
```

## API

### `compress(data, options?)`

Compress a Buffer.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `data` | `Buffer` | — | Input data |
| `options.level` | `number` | `LEVEL_DEFAULT` | Compression level (1–5) |
| `options.checksum` | `boolean` | `false` | Enable checksum |

Returns: `Buffer` — compressed data.

### `decompress(data, options?)`

Decompress a ZXC compressed Buffer.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `data` | `Buffer` | — | Compressed data |
| `options.size` | `number` | auto | Expected decompressed size |
| `options.checksum` | `boolean` | `false` | Verify checksum |

Returns: `Buffer` — decompressed data.

### `compressBound(inputSize)`

Returns the maximum compressed size for a given input size.

### `getDecompressedSize(data)`

Returns the original size from a ZXC compressed buffer (reads footer only).

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `LEVEL_FASTEST` | 1 | Fastest compression |
| `LEVEL_FAST` | 2 | Fast compression |
| `LEVEL_DEFAULT` | 3 | Recommended balance |
| `LEVEL_BALANCED` | 4 | Good ratio, good speed |
| `LEVEL_COMPACT` | 5 | Highest density |

## Testing

```bash
npm test
```

## License

BSD-3-Clause
