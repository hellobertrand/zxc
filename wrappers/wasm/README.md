# ZXC WebAssembly

High-performance lossless compression for the browser and Node.js via WebAssembly.

## Features

- **Buffer API**: Compress and decompress `Uint8Array` buffers
- **Reusable Contexts**: Amortise allocation overhead across multiple operations
- **All Levels**: Compression levels 1–5
- **Checksum Support**: Optional integrity verification
- **Tiny Footprint**: ~60 KB `.wasm` file (scalar build, no SIMD)

## Quick Start

### Browser (ES Module)

```js
import createZXC from './zxc_wasm.js';

const zxc = await createZXC();

// Compress
const input = new TextEncoder().encode('Hello, World!');
const compressed = zxc.compress(input, { level: 3 });

// Decompress
const output = zxc.decompress(compressed);
console.log(new TextDecoder().decode(output)); // "Hello, World!"
```

### Node.js

```js
import createZXC from 'zxc-wasm';
import { readFileSync } from 'fs';

const zxc = await createZXC();
const data = new Uint8Array(readFileSync('input.bin'));

const compressed = zxc.compress(data, { level: 5, checksum: true });
const decompressed = zxc.decompress(compressed, { checksum: true });
```

## API Reference

### `createZXC(moduleOverrides?) → Promise<ZXC>`

Initialise the WASM module. Returns a frozen API object.

### `zxc.compress(data, opts?) → Uint8Array`

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `level` | `number` | `3` | Compression level (1–5) |
| `checksum` | `boolean` | `false` | Enable integrity checksums |

### `zxc.decompress(data, opts?) → Uint8Array`

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `checksum` | `boolean` | `false` | Verify integrity checksums |

### `zxc.compressBound(inputSize) → number`

Maximum compressed output size for a given input size.

### `zxc.getDecompressedSize(data) → number`

Read the original size from a compressed buffer without decompressing.

### `zxc.createCompressContext(opts?) → CompressContext`

Create a reusable compression context (avoids per-call allocation).

```js
const ctx = zxc.createCompressContext({ level: 3 });
const c1 = ctx.compress(data1);
const c2 = ctx.compress(data2);
ctx.free(); // Release WASM memory
```

### `zxc.createDecompressContext() → DecompressContext`

Create a reusable decompression context.

```js
const ctx = zxc.createDecompressContext();
const d1 = ctx.decompress(compressed1);
const d2 = ctx.decompress(compressed2);
ctx.free();
```

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `version` | `string` | Library version (e.g. `"0.10.1"`) |
| `minLevel` | `number` | Minimum compression level (`1`) |
| `maxLevel` | `number` | Maximum compression level (`5`) |
| `defaultLevel` | `number` | Default compression level (`3`) |

## Building from Source

Requires [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```bash
# Configure
emcmake cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build-wasm

# Test
BUILD_DIR=build-wasm node wrappers/wasm/test.mjs
```

The build produces `build-wasm/zxc.js` and `build-wasm/zxc.wasm`.

## License

BSD 3-Clause. See [LICENSE](../../LICENSE).
