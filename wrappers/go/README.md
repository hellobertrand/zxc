# ZXC Go Bindings

High-performance Go bindings for the **ZXC** asymmetric compressor, optimised for **fast decompression**.  
Designed for *Write Once, Read Many* workloads like ML datasets, game assets, and caches.

## Features

- **Blazing fast decompression** — ZXC is specifically optimised for read-heavy workloads.
- **Buffer API** — compress/decompress `[]byte` slices in a single call.
- **Streaming API** — multi-threaded file compression/decompression.
- **Functional options** — clean, composable configuration (`WithLevel`, `WithChecksum`, `WithThreads`).
- **Typed errors** — sentinel error values for every ZXC error code.

## Prerequisites

Build the ZXC core library as a static library:

```bash
cd /path/to/zxc
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
      -DZXC_BUILD_CLI=OFF -DZXC_BUILD_TESTS=OFF
cmake --build build
```

## Installation (from source)

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc/wrappers/go
CGO_ENABLED=1 CGO_LDFLAGS="-L../../build -lzxc -lpthread" go build ./...
```

## Quick Start

```go
package main

import (
    "fmt"
    "log"

    zxc "github.com/hellobertrand/zxc/wrappers/go"
)

func main() {
    data := []byte("Hello, ZXC from Go!")

    // Compress
    compressed, err := zxc.Compress(data, zxc.WithLevel(zxc.LevelDefault))
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Compressed %d → %d bytes\n", len(data), len(compressed))

    // Decompress
    original, err := zxc.Decompress(compressed)
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Decompressed: %s\n", original)
}
```

## Streaming Files

```go
// Multi-threaded file compression
n, err := zxc.CompressFile("input.bin", "output.zxc",
    zxc.WithLevel(zxc.LevelCompact),
    zxc.WithThreads(4),
    zxc.WithChecksum(true),
)

// Decompress
n, err = zxc.DecompressFile("output.zxc", "restored.bin")
```

## Testing

```bash
cd zxc/wrappers/go
CGO_ENABLED=1 CGO_LDFLAGS="-L../../build -lzxc -lpthread" go test -v -count=1 ./...
```

## Benchmarks

```bash
CGO_ENABLED=1 CGO_LDFLAGS="-L../../build -lzxc -lpthread" go test -bench=. -benchmem ./...
```

## License

BSD-3-Clause — see [LICENSE](../../LICENSE).
