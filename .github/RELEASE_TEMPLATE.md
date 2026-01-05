# ZXC vX.X.X - [Release Title]

## [Release-specific changes]

---

## Download Guide

### Build Selection

| CPU Generation | Linux | Windows | macOS |
|----------------|-------|---------|-------|
| **x86-64** (2006+) | `zxc-linux-x86_64.tar.gz` | `zxc-windows-x64.exe.zip` | - |
| **AVX2** (2013+, Haswell) | `zxc-linux-x86_64-avx2.tar.gz` | `zxc-windows-x64-avx2.exe.zip` | - |
| **AVX512** (2017+, Skylake-X) | `zxc-linux-x86_64-avx512.tar.gz` | `zxc-windows-x64-avx512.exe.zip` | - |
| **ARM64** | `zxc-linux-aarch64.tar.gz` | - | `zxc-macos-arm64.tar.gz` |

**Unsure?** Use the generic x86-64 build for universal compatibility.

### CPU Feature Detection (x86)

```bash
# Linux
grep -E 'avx512|avx2' /proc/cpuinfo | head -1

# Windows (PowerShell)
Get-WmiObject Win32_Processor | Select-Object Name
```

### Performance

- **x86-64**: Baseline (SSE2)
- **AVX2**: ~20-30% faster than baseline
- **AVX512**: ~40-60% faster than baseline (tested with Intel SDE)

---

## Build from Source

For optimal CPU-specific performance:

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DZXC_NATIVE_ARCH=ON ..
cmake --build . --parallel
```

Enables `-march=native` for maximum SIMD utilization.

---

**Full Changelog**: https://github.com/hellobertrand/zxc/compare/vX.X.X...vX.X.X
