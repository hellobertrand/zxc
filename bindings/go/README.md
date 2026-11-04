# ZXC Go Bindings

High-performance Go bindings for the **ZXC** asymmetric compressor, optimised for **fast decompression**.  
Designed for *Write Once, Read Many* workloads like ML datasets, game assets, and caches.

## Features

- **Blazing fast decompression** - ZXC is specifically optimised for read-heavy workloads.
- **Buffer API** - compress/decompress `[]byte` slices in a single call.
- **Streaming API** - multi-threaded file compression/decompression.
- **Functional options** - clean, composable configuration (`WithLevel`, `WithChecksum`, `WithThreads`).
- **Typed errors** - sentinel error values for every ZXC error code.

## Installation

```bash
go get github.com/hellobertrand/zxc/bindings/go
```

The module carries the C sources, so all it needs is cgo and a C compiler. On
Windows and 32-bit ARM it compiles the baseline code only, without AVX2 or NEON
paths chosen at run time.

## Versioning

Releases are tagged `bindings/go/vX.Y.Z`: `go get` picks the latest, `@vX.Y.Z`
pins one.

Only these tags carry the C core, which the repository keeps above the module
directory. A branch or a commit (`@main`, a pseudo-version) does not build.

Versions up to v0.15.0 are published as `github.com/hellobertrand/zxc/wrappers/go`.

## Quick Start

```go
package main

import (
    "fmt"
    "log"

    zxc "github.com/hellobertrand/zxc/bindings/go"
)

func main() {
    data := []byte("Hello, ZXC from Go!")

    // Compress
    compressed, err := zxc.Compress(data, zxc.WithLevel(zxc.LevelDefault))
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Compressed %d => %d bytes\n", len(data), len(compressed))

    // Decompress
    original, err := zxc.Decompress(compressed)
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Decompressed: %s\n", original)
}
```

`CompressTo` writes into a caller-supplied buffer (size it with `CompressBound`).
Both `Compress` and `CompressTo` accept empty input and emit a minimal valid frame.
Pass `zxc.WithSeekable(true)` to append a seek table for random access via `zxc.OpenBytes`.
`Decompress` allocates the size the archive's footer declares: on untrusted input, cap it with `zxc.WithMaxOutputSize(n)`.

Compression writes no checksums unless `zxc.WithChecksum(true)`. Decompression verifies those an archive carries, on every path; `zxc.WithChecksum(false)` opts out (`SetChecksum(false)` on a `Seekable`). The block API is the exception: a bare block does not say whether it has a checksum, so it is off by default and `CompressBlock` and `DecompressBlock` must agree.

## Streaming Files

```go
// Multi-threaded file compression
n, err := zxc.CompressFile("input.bin", "output.zxc",
    zxc.WithLevel(zxc.LevelCompact),
    zxc.WithThreads(4),
    zxc.WithChecksum(true),
)

// Decompress, verifying the checksums
n, err = zxc.DecompressFile("output.zxc", "restored.bin")
```

## Testing

```bash
cd zxc/bindings/go
go test -v -count=1 ./...
```

## Benchmarks

```bash
go test -bench=. -benchmem ./...
```

## License

BSD-3-Clause - see [LICENSE](../../LICENSE).
