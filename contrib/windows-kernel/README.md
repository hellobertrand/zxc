# ZXC in the Windows kernel

Everything a kernel-mode driver needs to link ZXC: the dependency header, two
property sheets, the library project, a self-test driver and this guide.
`msbuild zxc_selftest.vcxproj` here stages the library sources next to the
public headers, builds `build\<platform>\<configuration>\zxc_km.lib`, and
links `zxc_selftest.sys` against it. Loading the driver round-trips a block
and a frame, also in place, at a fast and a dense level and refuses to load on
a mismatch.

```
zxc_deps.h            the kernel replacement for src/lib/zxc_deps.h
zxc_km_target.props   target version, read before the toolset's own sheets
zxc_km.props          include order, flags, defines; both projects import it
zxc_km.vcxproj        the library: stages the sources, builds zxc_km.lib
zxc_selftest.vcxproj  the reference consumer: a WDM driver linking the library
selftest.c            the driver's init: the round trips
check-image.ps1       what CI asserts about a built driver image
```

The deliverable is the library, not a driver. ZXC compresses slowly and
decompresses fast: compress in user mode, once, and decompress in the driver,
a file-system filter serving a compressed image, a storage driver, a boot-time
payload. A driver compressing on every write would pay the slow side on its
hot path.

The core uses no floating point, no VLA and no `alloca`, and reaches the C
library only through `src/lib/zxc_deps.h`, which this tree replaces. Unlike
the Linux tree it needs no header shims: the compiler's `<stdint.h>` and
`<limits.h>` are freestanding, the UCRT's `<string.h>` declares the memory
primitives without import decoration, and `ntoskrnl` exports them.

## What to build

From the EWDK's build environment (`LaunchBuildEnv.cmd`) or a Visual Studio
developer prompt with the Windows Driver Kit component installed:

```
msbuild contrib\windows-kernel\zxc_selftest.vcxproj /p:Configuration=Release /p:Platform=x64
```

builds `zxc_km.lib` and then `zxc_selftest.sys` into
`contrib\windows-kernel\build\x64\Release\`. Platforms: `x64`, `ARM64`.
Configurations: `Debug`, `Release`. Build only the library with
`zxc_km.vcxproj`. The library carries its own debug information (`/Z7`),
so there is no companion PDB to ship, and its objects are plain COFF, not
whole-program-optimized, so any kernel-toolset compiler links them.

The target is Windows 10 2004 (`_NT_TARGET_VERSION` `0xA000008`), the first
Windows with `ExAllocatePool2`, and the floor: `zxc_deps.h` refuses an older
`NTDDI_VERSION`. Windows 7 and 32-bit x86 are not supported; both are out of
Microsoft support and CI cannot build them. The build uses whichever kit the
environment offers; the 28000 kit is what CI uses.

## Consuming the library

Link `zxc_km.lib` and include `include\` from the ZXC tree (or the staged
copy under `build\stage\include`). Three settings in the consumer project
matter, and `zxc_selftest.vcxproj` is the worked example:

```xml
<ItemGroup>
  <ProjectReference Include="path\to\contrib\windows-kernel\zxc_km.vcxproj">
    <Project>{B3A184D6-C740-4C29-B8A5-FF8D49A6217F}</Project>
    <AdditionalProperties Condition="'$(TargetVersion)' != ''">TargetVersion=$(TargetVersion)</AdditionalProperties>
  </ProjectReference>
</ItemGroup>
<ItemDefinitionGroup>
  <ClCompile>
    <IgnoreStandardIncludePath>true</IgnoreStandardIncludePath>
    <AdditionalIncludeDirectories>path\to\contrib\windows-kernel\build\stage\include;$(VC_IncludePath);$(UniversalCRT_IncludePath);$(KM_IncludePath);$(KIT_SHARED_IncludePath);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
  </ClCompile>
</ItemDefinitionGroup>
```

The include order is the rule that matters (see below). The
`AdditionalProperties` line is the other one: a `TargetVersion` set in the
driver's project file does not cross a project reference, so without it the
library builds for its own default, Windows 10 2004, whatever the driver
targets. A `TargetVersion` on the command line is global and reaches the
library either way. The official toolset supplies every other link input; a
hand-rolled link also needs `ntoskrnl.lib`, the `/GS` support library
(`BufferOverflowFastFailK.lib`), and `arm64rt.lib` on ARM64. The library's
externals are `memcpy`, `memset`, `memcmp`, the two pool routines,
`__chkstk`, and the `/GS` and CFG support symbols; a linked image imports
`ntoskrnl.exe` only, which `check-image.ps1` asserts.

Vendoring instead: add the seven units named in `zxc_km.vcxproj` to the
driver project, import `zxc_km.props` after `Microsoft.Cpp.props`, and keep
the staging rule: `zxc_internal.h` includes the dependency header with
quotes, so this tree's `zxc_deps.h` must sit next to the units in place of
the stock one. The sheet carries the rules only, no output directories and
no paths into this tree, so the project's own `OutDir`, `IntDir` and include
directories for the staged sources stay as they are. The sheet also passes
`/kernel`, which the toolset gives driver configurations only; the header
refuses to build without it.

### Include order

With the toolset's own include order, the kit's legacy `km\crt` CRT headers
come first and `<stdint.h>` cannot be included at all: `km\crt\crtdefs.h`
and the compiler's `vcruntime.h` redefine each other's macros (C4005,
C4083), in either include position, in every C mode. The public ZXC headers
need `<stdint.h>`. The order compiler headers, UCRT, `km`, `shared`, with
`km\crt` dropped, compiles clean; `zxc_km.props` encodes it with the
standard include path ignored.

`/std:c17` is the core's standard; the toolset drops the C-standard project
property, so it travels through `AdditionalOptions`, together with
`/Zc:preprocessor-`, because the kit's `wdm.h` ARM64 macros fail the
conforming preprocessor.

## The dependency header

`zxc_deps.h` here replaces `src/lib/zxc_deps.h` (the `ZxcStage` target
copies it over). What it maps, and why:

- `ZXC_MALLOC` / `ZXC_CALLOC` / `ZXC_FREE`: `ExAllocatePool2` on non-paged
  pool with the tag `zxc `. Non-paged, because a context built at
  PASSIVE_LEVEL must stay usable at DISPATCH_LEVEL and a heap context's lazy
  allocations may happen there. A
  zero size is rounded up to one byte, which Driver Verifier would otherwise
  flag; free of NULL is a no-op, which the kernel routine is not.
- No `ZXC_REALLOC`: pool has none, and these units never reallocate; a
  future use fails to build.
- `ZXC_ALIGNED_MALLOC` / `ZXC_ALIGNED_FREE`: pool aligns to 16 bytes below a
  page and to a page above, short of the cache line the workspace layout
  assumes, so the header aligns by hand.
- `ZXC_POPCOUNT32` / `ZXC_POPCOUNT64`: a SWAR count. The `__popcnt`
  intrinsics emit POPCNT, which faults on CPUs without it.
- `static_assert`: the keyword, since `<assert.h>` is not included.
- `NTDDI_VERSION` must be defined; the toolset derives it from
  `TargetVersion`. The header cannot default it through `sdkddkver.h`, which
  breaks `wdm.h` when included ahead of `ntddk.h`, so it spells the Windows
  10 2004 threshold out, refuses anything older, and checks the value against
  the kit afterwards. It also refuses any machine but x64 and ARM64.

## Contexts and IRQL

Static contexts: the driver allocates one workspace per CPU at init, from
non-paged pool at PASSIVE_LEVEL, the library carves everything out of it,
and decoding then allocates nothing and may run at DISPATCH_LEVEL.

```c
size_t ws_sz = zxc_static_dctx_workspace_size(PAGE_SIZE);
void*  ws    = ExAllocatePool2(POOL_FLAG_NON_PAGED, ws_sz, 'xyzT');   /* once, at init, per CPU */
zxc_dctx* dctx = zxc_init_static_dctx(ws, ws_sz, PAGE_SIZE);
```

Workspace sizes; levels 6-7 add the optimal-parser scratch:

| blocks  | dctx      | cctx (levels 1-5) | cctx (levels 6-7) |
|---------|-----------|-------------------|-------------------|
| 4 KiB   | 15 872 B  | 314 496 B         | 532 736 B         |
| 64 KiB  | 212 480 B | 500 480 B         | 1 033 216 B       |
| 512 KiB | 1.60 MiB  | 1.79 MiB          | 5.85 MiB          |

- A heap context (`zxc_create_dctx`) allocates on the first decode, and
  again on the first entropy-coded block. The header makes those non-paged,
  so they are legal at DISPATCH_LEVEL, but they can still fail under
  pressure; a static context cannot.
- Nothing in the library is marked pageable, so its code stays resident.
- Never put a workspace on the stack.
- `zxc_decompress_block()` wants `ZXC_DECOMPRESS_TAIL_PAD` (2112 bytes) of
  slack past the output for its wild copies, which a page-sized buffer does
  not have. `zxc_decompress_block_safe()` takes a destination sized to the
  exact output.

## Stack

Kernel stacks are 24 KB on x64 and ARM64, shared with
whatever the I/O stack above already used. On Linux with GCC, decoding peaks
at 4.5 KiB whatever the level and compression at 12.3 KiB at levels 6-7
(5.9 KiB up to level 5), over the whole call path; the largest single MSVC
frames measured here are about 2.9 KiB on x64 and 3.2 KiB on ARM64.
Decoding fits everywhere. Compressing at levels 6 and 7 from a deep I/O
stack wants `KeExpandKernelStackAndCalloutEx`. The WDK's code analysis flags
any frame over 1 KiB; eleven of the library's functions pass that, which only
matters if analysis runs over the library sources rather than the consumer
linking the archive.

## SIMD

The library uses SSE2 on x64 and NEON on ARM64, both allowed in kernel mode
without saving state, and never AVX: `/kernel` refuses `/arch:AVX*`, and the
build defines `ZXC_ONLY_DEFAULT`, which keeps the baseline variant and
compiles the CPUID probes out. The x64 library contains no POPCNT, LZCNT,
BMI, SSSE3 or SSE4.2; the ARM64 one no LSE atomics, CRC32, dot product,
crypto, PAC or BTI. At `/O2` MSVC's auto-vectorizer emits a few SSE4.1
instructions behind a runtime test of `__isa_available`, which an image
without the C runtime pins to zero through the compiler's `/alternatename`,
so that path never runs. CI checks every image for `ymm` and `zmm`
registers, so an AVX instruction fails the build.

## Loading the self-test

On a test machine or VM, as administrator:

```
bcdedit /set testsigning on          (then reboot)
msbuild contrib\windows-kernel\zxc_selftest.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SignMode=TestSign
sc create zxc_selftest type= kernel binPath= C:\path\to\zxc_selftest.sys
sc start zxc_selftest
```

`SignMode=TestSign` lets the toolset sign the image with its test certificate.
`sc start` returns an error when `DriverEntry` fails, which is what a
mismatch does. Each level logs one line, at error level so no filter mask is
needed, visible in DebugView (with kernel capture on) or in the kernel
debugger:

```
zxc_selftest: level 3 block <n> -> 57, frame <n> -> 57, in place <n> -> 57: ok
zxc_selftest: level 7 block <n> -> 57, frame <n> -> 57, in place <n> -> 57: ok
```

`<n>` is the compressed size, which varies by level and version; what
matters is `-> 57`, the message's length, for every decode, and `ok`. Then
`sc stop zxc_selftest` and `sc delete zxc_selftest`. Run it under Driver
Verifier at least once:

```
verifier /standard /driver zxc_selftest.sys     (then reboot, load, and verifier /reset)
```

The standard settings check IRQL, pool tags, zero-length allocations and
leaks; add low-resource simulation (`verifier /flags 0x4`) to see the
allocation-failure paths return `STATUS_INSUFFICIENT_RESOURCES` instead of
bug-checking.

## CI

`.github/workflows/kernel-windows.yml` builds x64 and ARM64 at the default
target on every change to this tree or the library, then runs
`check-image.ps1`. The runner image carries Visual Studio 2026 with the
WDK's Visual Studio extension but no kit, so the job installs the 28000
SDK and WDK, the kit line built for Visual Studio 2026, with Microsoft's
installers first, each pinned to a versioned download and checked against
its SHA-256 and Microsoft Authenticode signature before it runs. A kit's
MSBuild task assemblies exist for one Visual Studio version: 26100 for 2022,
28000 for 2026.

## Licensing

BSD-3-Clause, like the rest of ZXC. Kernel-mode Windows code links no GPL
component, so no licence interaction arises.
