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
zxc_km_target.props   target version and kit, read before the toolset's own sheets
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
`contrib\windows-kernel\build\x64\Release\`. Platforms: `x64`, `ARM64`, and
`Win32` for plan B. Configurations: `Debug`, `Release`. Build only the library
with `zxc_km.vcxproj`. The library carries its own debug information (`/Z7`),
so there is no companion PDB to ship, and its objects are plain COFF, not
whole-program-optimized, so any kernel-toolset compiler links them.

The default target is Windows 10 2004 (`_NT_TARGET_VERSION` `0xA000008`), the
first Windows with `ExAllocatePool2`. The build uses whichever kit the
environment offers; the 26100 kit is what CI uses. Add `/p:TargetVersion=Windows7`
for plan B, below. `build\<platform>\Release` holds whatever target was built
last.

## Consuming the library

Link `zxc_km.lib` and include `include\` from the ZXC tree (or the staged
copy under `build\stage\include`). Three settings in the consumer project
matter, and `zxc_selftest.vcxproj` is the worked example:

```xml
<ItemGroup>
  <ProjectReference Include="path\to\contrib\windows-kernel\zxc_km.vcxproj">
    <Project>{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}</Project>
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
library builds for its own default, Windows 10 2004, and a plan B driver
loads on Windows 7 with a missing `ExAllocatePool2` import. A
`/p:TargetVersion=Windows7` on the command line is global and reaches the
library either way. The official toolset supplies every other link input; a
hand-rolled link also needs
`ntoskrnl.lib`, the `/GS` support library (`BufferOverflowK.lib` for a target
below Windows 8, `BufferOverflowFastFailK.lib` otherwise), `arm64rt.lib` on
ARM64, and `memcmp.lib` on x86. The library's externals are `memcpy`,
`memset`, `memcmp`, the two pool routines, `__chkstk`, and the `/GS` and
CFG support symbols; a linked image imports `ntoskrnl.exe` only, which
`check-image.ps1` asserts.

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

## Plan B: Windows 7 and 32-bit x86

Windows 7 is the floor, as an opt-in build:

```
msbuild contrib\windows-kernel\zxc_selftest.vcxproj /p:Configuration=Release /p:Platform=x64 /p:TargetVersion=Windows7
msbuild contrib\windows-kernel\zxc_selftest.vcxproj /p:Configuration=Release /p:Platform=Win32 /p:TargetVersion=Windows7
```

One library built this way runs on every Windows from 7 up. The target
version must reach `zxc_km.vcxproj`: on the command line as above, or
forwarded through the project reference as shown under "Consuming the
library"; a value in the consumer's project file alone does not. What
changes:

- **The kit.** Kits newer than 22000 refuse both a Windows 7 target and the
  Win32 platform in their MSBuild targets, though their compilers still
  build the code. Plan B therefore builds with the 22000 kit
  (`WindowsTargetPlatformVersion` `10.0.22000.0`, set by
  `zxc_km_target.props`), whose compiler is MSVC 14.28 from Visual Studio
  2019 16.9, the first toolset with `/std:c17`. The 22000 EWDK is
  self-contained; or install WDK 10.0.22000 with its SDK next to a newer
  kit. On a machine with only a newer kit the build stops at evaluation
  with that toolset's own message, on 26100 `Unknown or unsupported property
  value '1.9' for KmdfVersion for target OS 'Windows7'`; with the 22000 kit
  present but its kernel headers missing, `zxc_km.props` names the missing
  `ntddk.h`. Neither produces a binary.
- **The allocator.** Below Windows 10 2004 the dependency header allocates
  through the kit's `ExAllocatePoolUninitialized` wrapper on non-paged pool
  and zeroes explicitly. The raw `ExAllocatePoolWithTag` is deprecated under
  warnings-as-errors in newer kits even for a Windows 7 target, which is why
  the wrapper is not optional.
- **The opt-in call.** Define `POOL_NX_OPTIN` to 1 before including
  `<ntddk.h>` in the driver and call `ExInitializeDriverRuntime(DrvRtPoolNxOptIn)`
  at the top of `DriverEntry`, before any allocation; `selftest.c` shows it.
  The library's pool is then non-executable on Windows 8 and later. Without
  the call the library still works, but its non-paged allocations are
  executable there, which Driver Verifier and certification tests report.
- **x86** is scalar: `zxc_km.props` sets `/arch:IA32` on Win32, because
  x86 kernel mode does not preserve XMM state for drivers and `/kernel`
  alone leaves the 32-bit compiler on SSE2 code generation. The toolset
  compiles x86 with `/Gz`, so the library's functions are stdcall; any
  kernel-toolset project matches. The 12 KB x86 kernel stack leaves room for
  decoding and for compression at levels 1 to 5 only, so the self-test's
  dense level is 5 there.
- ARM64 has no Windows 7 edition; plan B is x64 and Win32 only. Asking for
  it stops with the toolset's `The specified architecture 'ARM64' is not
  supported by the target OS 'Windows7'`.

## The dependency header

`zxc_deps.h` here replaces `src/lib/zxc_deps.h` (the `ZxcStage` target
copies it over). What it maps, and why:

- `ZXC_MALLOC` / `ZXC_CALLOC` / `ZXC_FREE`: `ExAllocatePool2` on non-paged
  pool with the tag `zxc `, or the plan B wrapper below Windows 10 2004.
  Non-paged, because a context built at PASSIVE_LEVEL must stay usable at
  DISPATCH_LEVEL and a heap context's lazy allocations may happen there. A
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
  breaks `wdm.h` when included ahead of `ntddk.h`, so it spells the two
  thresholds out and checks them against the kit afterwards.
- On x86, C4242 disabled by pragma: the kit's force-included `warning.h`
  raises it past any command-line `/wd`, and the core's block-bounded
  `uint64_t` narrowing at call sites trips it on a 32-bit `size_t`.

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

Kernel stacks are 12 KB on x86 and 24 KB on x64 and ARM64, shared with
whatever the I/O stack above already used. On Linux with GCC, decoding peaks
at 4.5 KiB whatever the level and compression at 12.3 KiB at levels 6-7
(5.9 KiB up to level 5), over the whole call path; the largest single MSVC
frames measured here are about 2.9 KiB on x64 and x86 and 3.2 KiB on ARM64.
Decoding fits everywhere. Compressing at levels 6 and 7 wants
`KeExpandKernelStackAndCalloutEx` on x64 and ARM64 and does not fit x86 at
all. The WDK's code analysis flags any frame over 1 KiB; eleven of the
library's functions pass that, which only matters if analysis runs over the
library sources rather than the consumer linking the archive.

## SIMD

The library uses SSE2 on x64 and NEON on ARM64, both allowed in kernel mode
without saving state, and never AVX: `/kernel` refuses `/arch:AVX*`, and the
build defines `ZXC_ONLY_DEFAULT`, which keeps the baseline variant and
compiles the CPUID probes out. The x64 library contains no POPCNT, LZCNT,
BMI, SSSE3 or SSE4.2; the ARM64 one no LSE atomics, CRC32, dot product,
crypto, PAC or BTI. At `/O2` MSVC's auto-vectorizer emits a few SSE4.1
instructions behind a runtime test of `__isa_available`, which an image
without the C runtime pins to zero through the compiler's `/alternatename`,
so that path never runs. x86 is scalar (see plan B).

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
bug-checking. On Windows 7, sign with a SHA-1 test certificate or install the
SHA-2 code-signing support update first; a SHA-256 signature is otherwise
rejected at load.

## CI

`.github/workflows/windows-kernel.yml` builds x64 and ARM64 at the default
target with the runner's kit on every change to this tree or the library,
then runs `check-image.ps1`. Plan B needs the 22000 kit, which the runner
lacks, so a second job installs it and builds x64 and Win32 at the Windows 7
target on the weekly schedule and on manual dispatch only.

## Licensing

BSD-3-Clause, like the rest of ZXC. Kernel-mode Windows code links no GPL
component, so no licence interaction arises.
