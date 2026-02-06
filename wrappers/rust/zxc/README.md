# zxc

Safe Rust bindings to the **ZXC compression library** — a fast LZ77-based compressor optimized for high decompression speed.

[![Crates.io](https://img.shields.io/crates/v/zxc.svg)](https://crates.io/crates/zxc)
[![Documentation](https://docs.rs/zxc/badge.svg)](https://docs.rs/zxc)
[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)

## Quick Start

```rust
use zxc::{compress, decompress, Level};

fn main() -> Result<(), zxc::Error> {
    let data = b"Hello, ZXC! This is some data to compress.";
    
    // Compress (no checksum for max speed)
    let compressed = compress(data, Level::Default, None)?;
    println!("Compressed {} -> {} bytes", data.len(), compressed.len());
    
    // Decompress
    let decompressed = decompress(&compressed)?;
    assert_eq!(&decompressed[..], &data[..]);
    
    Ok(())
}
```

## Compression Levels

| Level | Speed | Ratio | Use Case |
|-------|-------|-------|----------|
| `Level::Fastest` | ★★★★★ | ★★☆☆☆ | Real-time, gaming |
| `Level::Fast` | ★★★★☆ | ★★★☆☆ | Network, streaming |
| `Level::Default` | ★★★☆☆ | ★★★★☆ | General purpose |
| `Level::Balanced` | ★★☆☆☆ | ★★★★☆ | Archives |
| `Level::Compact` | ★☆☆☆☆ | ★★★★★ | Storage, firmware |

## Features

- **Fast decompression**: Optimized for read-heavy workloads
- **5 compression levels**: Trade off speed vs ratio
- **Optional checksums**: Disabled by default for maximum performance, enable for data integrity
- **File streaming**: Multi-threaded compression/decompression for large files
- **Zero-allocation API**: `compress_to` and `decompress_to` for buffer reuse
- **Pure Rust API**: Safe, idiomatic interface over the C library

## Advanced Usage

### Pre-allocated Buffers

```rust
use zxc::{compress_to, decompress_to, compress_bound, CompressOptions, DecompressOptions};

let data = b"Hello, world!";

// Compression
let mut output = vec![0u8; compress_bound(data.len())];
let size = compress_to(data, &mut output, &CompressOptions::default())?;
output.truncate(size);

// Decompression
let mut decompressed = vec![0u8; data.len()];
decompress_to(&output, &mut decompressed, &DecompressOptions::default())?;
```

### Disable Checksum

```rust
use zxc::{compress_with_options, decompress_with_options, CompressOptions, DecompressOptions, Level};

let opts = CompressOptions::with_level(Level::Fastest).without_checksum();
let compressed = compress_with_options(data, &opts)?;

let decompressed = decompress_with_options(&compressed, &DecompressOptions::skip_checksum())?;
```

### Query Decompressed Size

```rust
use zxc::decompressed_size;

if let Some(size) = decompressed_size(&compressed) {
    let mut buffer = vec![0u8; size];
    // ...
}
```

## License

BSD-3-Clause — see [LICENSE](../../LICENSE) for details.
