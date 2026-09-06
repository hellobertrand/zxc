# zxc-compress

Safe Rust bindings to [ZXC](https://github.com/hellobertrand/zxc), a lossless
compressor that trades compression speed for maximum decode throughput.

[![Crates.io](https://img.shields.io/crates/v/zxc-compress.svg)](https://crates.io/crates/zxc-compress)
[![Documentation](https://docs.rs/zxc-compress/badge.svg)](https://docs.rs/zxc-compress)
[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](https://github.com/hellobertrand/zxc/blob/main/LICENSE)

## Installation

```sh
cargo add zxc-compress
```

The package is `zxc-compress`, but the library it exports is named `zxc`:
import it with `use zxc::...`. The C library is vendored and built by
[`zxc-compress-sys`](https://crates.io/crates/zxc-compress-sys); a C compiler
is the only requirement.

## Quick Start

```rust
use zxc::{compress, decompress, Level};

fn main() -> Result<(), zxc::Error> {
    let data = b"Hello, ZXC! This is some data to compress.";

    // Compress (no checksum for maximum speed)
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
| `Level::Density` | ★☆☆☆☆ | ★★★★★ | High density (Huffman literals + optimal parser) |
| `Level::Ultra` | ★☆☆☆☆ | ★★★★★ | Maximum density (Huffman literals + tokens, deep parse) |

## Features

- **Fast decompression**: Optimized for read-heavy workloads
- **7 compression levels**: Trade off speed vs ratio
- **Optional checksums**: Disabled by default for maximum performance, enable for data integrity
- **File streaming**: Multi-threaded compression/decompression for large files
- **Zero-allocation API**: `compress_to` and `decompress_to` for buffer reuse
- **Pure Rust API**: Safe, idiomatic interface over the C library

Streaming (`CStream`/`DStream`), `std::io` adapters (`Encoder`/`Decoder`),
seekable archives, dictionaries and block-level contexts are covered in the
[API documentation](https://docs.rs/zxc-compress).

## Advanced Usage

### Pre-allocated Buffers

```rust
use zxc::{compress_bound, compress_to, decompress_to, CompressOptions, DecompressOptions};

fn main() -> Result<(), zxc::Error> {
    let data = b"Hello, world!";

    // Compression
    let mut output = vec![0u8; compress_bound(data.len()) as usize];
    let size = compress_to(data, &mut output, &CompressOptions::default())?;
    output.truncate(size);

    // Decompression
    let mut decompressed = vec![0u8; data.len()];
    decompress_to(&output, &mut decompressed, &DecompressOptions::default())?;
    assert_eq!(&decompressed[..], &data[..]);
    Ok(())
}
```

### Disable Checksum

```rust
use zxc::{compress_with_options, decompress_with_options, CompressOptions, DecompressOptions, Level};

fn main() -> Result<(), zxc::Error> {
    let data = b"Hello, world!";

    let opts = CompressOptions::with_level(Level::Fastest).without_checksum();
    let compressed = compress_with_options(data, &opts)?;

    let decompressed = decompress_with_options(&compressed, &DecompressOptions::skip_checksum())?;
    assert_eq!(&decompressed[..], &data[..]);
    Ok(())
}
```

### Query Decompressed Size

```rust
use zxc::{compress, decompress_to, decompressed_size, DecompressOptions, Level};

fn main() -> Result<(), zxc::Error> {
    let compressed = compress(b"Hello, world!", Level::Default, None)?;

    let size = decompressed_size(&compressed).expect("valid ZXC frame");
    let mut buffer = vec![0u8; size as usize];
    decompress_to(&compressed, &mut buffer, &DecompressOptions::default())?;
    Ok(())
}
```

## License

BSD-3-Clause - see [LICENSE](https://github.com/hellobertrand/zxc/blob/main/LICENSE) for details.
