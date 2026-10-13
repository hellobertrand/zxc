# ZXC in the Windows kernel: `contrib/windows-kernel` design

Date: 2026-10-13. Status: approved in discussion, awaiting review of this text.

## Goal

Let a Windows kernel-mode driver (`.sys`) use ZXC's buffer API by linking a
kernel-mode static library, `zxc_km.lib`, built from the unmodified core
sources. The deliverable is the library and the recipe to consume it, not a
driver. A self-test driver ships alongside as the reference consumer, as the
Linux tree ships a self-test module.

This mirrors `contrib/linux-kernel`: a replacement dependency header, a build
recipe on the platform's own driver build system, a self-test, a build-only
CI job, and a README that states the platform's rules.

The default target is Windows 10 2004. Windows 7 is the floor, supported as
a documented alternative build, plan B, that one property selects and that
CI builds on every run.

## Evidence

A throwaway spike on 2026-10-13 compiled the seven core units under
`/kernel /std:c17 /W4 /WX`, archived them, and linked a self-test driver
against the archive, on x64 and ARM64, with two kits:

| Kit | Toolset | Target | x64 | ARM64 |
|---|---|---|---|---|
| 22621 (Windows 11 22H2 EWDK) | MSVC 14.31 | Windows 11 22H2 | pass | pass |
| 26100.6584 (Windows 11 25H2 EWDK, the line on GitHub `windows-2025` runners) | MSVC 14.44 | Windows 11 22H2 | pass | pass |
| 22621 | MSVC 14.31 | Windows 7 | pass | no such target |
| 20348 (Windows Server 2022 EWDK) | MSVC 14.28 (Visual Studio 2019 16.9) | Windows 7 | pass | no such target |

And for 32-bit x86, which the 22621 kit is the last to ship kernel
libraries for:

| Kit | Toolset | Target | x86 |
|---|---|---|---|
| 22621 | MSVC 14.31 | Windows 11 22H2 | pass |
| 22621 | MSVC 14.31 | Windows 7 | pass |

Those rows are compiler-level builds. Through the official project system
the picture is narrower: a probe driver project built with each kit's
MSBuild toolset gave

| Kit | x64, latest target | x64, Windows 7 | Win32, Windows 7 |
|---|---|---|---|
| 22000 (Windows 11 21H2 EWDK, MSVC 14.28) | builds | builds | builds |
| 22621 | builds | refused | refused |

The 22621 kit's `WindowsDriver.common.targets` refuses Win32 for kernel
drivers and any `TargetVersion` other than the latest whenever the kit is
newer than 22000; the 26100 kit inherits both rules. So plan B, Windows 7
and x86, is built with the 22000 kit, whose compiler is the same MSVC 14.28
the 20348 kit carries.

What the spike established, and what this design encodes:

- **Include order.** With the kernel toolset's own order, the kit's legacy
  `km\crt` CRT headers come first and `<stdint.h>` cannot be included at all:
  `km\crt\crtdefs.h` and the compiler's `vcruntime.h` redefine each other's
  macros (C4005, C4083), in either include position, in every C mode, on both
  kits. The public ZXC headers need `<stdint.h>`. The order compiler headers,
  UCRT, `km`, `shared`, with `km\crt` dropped, compiles everything clean.
- **C standard.** The kit headers accept `/std:c17`. The toolset's compiler
  rule silently drops the `LanguageStandard_C` project property, so the flag
  must travel through `AdditionalOptions`. On ARM64 the kit's `wdm.h` macros
  fail the conforming preprocessor that `/std:c17` enables (C5103), so
  `/Zc:preprocessor-` is required there.
- **Instruction sets.** The x64 library contains no AVX, POPCNT, LZCNT, BMI,
  SSSE3 or SSE4.2. It uses SSE2 freely, which Windows x64 kernel mode allows
  without saving state. At `/O2`, MSVC's auto-vectorizer emits eight SSE4.1
  instructions in two functions behind a runtime test of `__isa_available`;
  in an image without the C runtime the compiler's `/alternatename` directive
  pins that variable to `__isa_available_default`, which is zero, so the path
  is dead. The ARM64 library uses baseline NEON only: no LSE atomics, CRC32,
  dot product, crypto, PAC or BTI, and no scalar floating point.
- **Link inputs.** Entry point `GsDriverEntry` from `BufferOverflowFastFailK.lib`
  seeds the `/GS` cookie and calls `DriverEntry`. The library's externals are
  `memcpy`, `memset`, `memcmp`, `ExAllocatePool2`, `ExFreePoolWithTag`,
  `__chkstk`, the `/GS` and CFG support symbols, and on ARM64
  `__security_push_cookie` / `__security_pop_cookie`, which come from
  `arm64rt.lib`. The kit keeps that file under `um\arm64`; the official
  toolset links it for kernel ARM64 targets automatically. The linked images
  import only `ntoskrnl.exe`.
- **Stack.** Largest prologue frames: about 2.9 KiB on x64 and 3.2 KiB on
  ARM64. Eleven functions exceed the WDK code-analysis default of 1 KiB.
- **Windows 7 target.** `ExAllocatePool2` exists only from Windows 10 2004.
  Below that, allocation goes through the kit's inline
  `ExAllocatePoolUninitialized` wrapper on `NonPagedPool`, with explicit
  zeroing. The raw `ExAllocatePoolWithTag` is deprecated under
  warnings-as-errors in the 22621 kit even for a Windows 7 target, which is
  why the wrapper is not optional. With `POOL_NX_OPTIN` defined, that pool
  becomes non-executable on Windows 8 and later once the driver's
  `DriverEntry` calls `ExInitializeDriverRuntime(DrvRtPoolNxOptIn)` before
  its first allocation. The image links `BufferOverflowK.lib` instead of the
  fast-fail variant, carries subsystem version 6.01, and imports
  `ExAllocatePoolWithTag`, `ExFreePoolWithTag`, `__chkstk`, `KeBugCheckEx`
  and `DbgPrintEx` from `ntoskrnl.exe`, nothing else.
- **x86.** Built with the toolset's x86 settings (`_X86_`, `i386`,
  `STD_CALL`, `/Gz`, SafeSEH, entry `GsDriverEntry@8`, the kit's
  `memcmp.lib`, all confirmed from the 22000 toolset's own command lines,
  which pass no `/arch` at all) plus `/arch:IA32`,
  the x86 library contains no SSE, MMX, x87, POPCNT or LZCNT instructions,
  only the baseline bit scans; 64-bit arithmetic goes through the
  `_allmul`, `_allshl` and `_aullshr` helpers that `ntoskrnl` exports. The
  x86 kernel exports `memcpy` and `memset` but not `memcmp`, which the kit
  supplies as `memcmp.lib`. Without `/arch:IA32` the same build carried
  about 3,900 XMM instructions. The kit's force-included `warning.h` sets
  C4242 to level 3 by pragma, which a `/wd4242` on the command line cannot
  undo, and on a 32-bit `size_t` the core's block-bounded `uint64_t`
  narrowing at call sites trips it. Largest frame about 2.9 KiB.
- **Toolchain floor.** The core needs `/std:c17`, which Visual Studio 2019
  16.8 (MSVC 14.28) introduced. The 20348 EWDK has it; the 18362 EWDK
  (MSVC 14.20) does not and cannot build the library. Microsoft's officially
  supported pairing for Windows 7 targets is the 19041 kit with Visual
  Studio 2019; the 20348 and 22621 kits build the target without complaint.

## Scope

In scope:

- `contrib/windows-kernel/`: dependency header, shared project settings, the
  library project, the self-test driver project and source, README.
- Three platforms: x64 and ARM64 on the current kit, and 32-bit x86 as the
  second axis of plan B. Plan B, both the Windows 7 target and x86, is
  built with the 22000 kit, the newest whose project system accepts
  either. The user-mode library already builds and is tested on 32-bit
  x86; this adds the kernel-mode build.
- `.github/workflows/windows-kernel.yml`: build both platforms, assert the
  image's imports.
- Pointers in `docs/INSTALL.md`, `docs/API.md` and
  `.github/workflows/README.md`.

Out of scope:

- Any change to `src/lib` or `include`. The core stays unaware of Windows
  kernel mode; the vendored `zxc_deps.h` replacement is the whole contract.
- Loading the driver in CI. Runtime proof is a manual step on a test-signed
  VM, as it is for the Linux module.
- ARM32. No kit in use supports it and nothing asks for it.
- Loading the x86 build in CI, for the same reason as the others.
- Shipping the library as a release artifact.
- Pageable code sections, lookaside lists, or any allocator beyond pool.

## Tree

```
contrib/windows-kernel/
  README.md             the guide, section for section like linux-kernel/README.md
  zxc_deps.h            the kernel replacement for src/lib/zxc_deps.h
  zxc_km_target.props   target version and kit, imported before Microsoft.Cpp.Default.props
  zxc_km.props          the rules both projects import after Microsoft.Cpp.props
  zxc_km.vcxproj        static library: stages the sources, builds zxc_km.lib
  zxc_selftest.vcxproj  WDM driver: DriverEntry round trips, links zxc_km.lib
  selftest.c            the driver's init
  check-image.ps1       CI's modinfo: imports, machine, subsystem, no SSE on x86
  .gitignore            build/
```

As implemented, the property sheet is two files: the toolset reads the
target version and the kit when its own sheets are imported, during
`Microsoft.Cpp.props`, so those must exist before `Microsoft.Cpp.Default.props`,
while every compile rule must come after `Microsoft.Cpp.props`. The rules
sheet carries no output directories and no paths into the tree, so a
vendoring consumer can import it as is; each project sets its own
`OutDir`, `IntDir` and staged include directories before importing it.

Everything the build produces lands under `contrib/windows-kernel/build/`:
`build/stage/` for the staged sources, `build/<Platform>/<Configuration>/`
for objects and outputs.

## Components

### `zxc_deps.h`

Replaces `src/lib/zxc_deps.h` in the staged tree. It includes `<ntddk.h>`,
`<ntintsafe.h>`, `<limits.h>`, `<stddef.h>`, `<stdint.h>` and `<string.h>`.
It requires `NTDDI_VERSION` to be defined, as the toolset does from
`TargetVersion`: `<sdkddkver.h>` cannot supply a default ahead of
`<ntddk.h>` (it defines a deprecation macro in terms of one `ntdef.h`
defines later, and `wdm.h` then fails), so the header spells its two
thresholds out as literals for the tests that precede the include and
cross-checks them against the kit afterwards.
Unlike the Linux header it needs no shims: the compiler's `<stdint.h>` and
`<limits.h>` are freestanding, the UCRT `<string.h>` declares the memory
primitives without import decoration, and `ntoskrnl` exports them.

Guards, each a `#error` with a one-line reason:

- `_KERNEL_MODE` undefined: the header is only for `/kernel` builds.
- `NTDDI_VERSION` below `NTDDI_WIN7`: Windows 7 is the floor.

Before including `<ntddk.h>`, when the target is below Windows 10 2004, the
header defines `POOL_NX_OPTIN` to 1 unless already defined, so that
`NonPagedPool` resolves to the kit's runtime-selected default pool type.

Definitions:

- `static_assert(expr, msg)` maps to `_Static_assert` when `<assert.h>` has
  not defined it. `zxc_internal.h` checks its constants with the two-argument
  form.
- `ZXC_POOL_TAG`: a four-character tag that reads `zxc ` in pool dumps.
- On `_M_IX86`, `#pragma warning(disable : 4242)`: the kit's `warning.h`
  sets C4242 to level 3, and on a 32-bit `size_t` the core's block-bounded
  `uint64_t` narrowing at call sites trips it, the same class the CMake
  build leaves unchecked on 32-bit hosts.
- One internal allocation routine with two bodies, selected on
  `NTDDI_VERSION` at compile time:
  - Windows 10 2004 and later:
    `ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, size, tag)`,
    or without the uninitialized flag when zeroing is requested, since this
    allocator zeroes by default.
  - Windows 7 up to Windows 10 1909:
    `ExAllocatePoolUninitialized(NonPagedPool, size, tag)`, the kit's inline
    wrapper that suppresses the deprecation of `ExAllocatePoolWithTag`,
    followed by `RtlZeroMemory` when zeroing is requested. Zeroing is done
    here rather than through `ExAllocatePoolZero`, so correctness never
    depends on the driver's runtime-init call; only the NX property does.
  Both bodies round a zero size up to one byte because Driver Verifier
  flags zero-length allocations, and both allocate non-paged, because a
  context built at PASSIVE_LEVEL must stay usable at DISPATCH_LEVEL and a
  heap context's lazy allocations may happen there.
- `ZXC_MALLOC`: that routine, uninitialized.
- `ZXC_CALLOC`: the size product checked with `RtlSizeTMult`, then that
  routine, zeroed.
- `ZXC_FREE`: `ExFreePoolWithTag` behind a NULL check; the kernel routine
  bug-checks on NULL where libc's `free` is a no-op.
- No `ZXC_REALLOC`: pool has none, the seven units never reallocate, and a
  future use must fail to build, as on Linux.
- `ZXC_ALIGNED_MALLOC` / `ZXC_ALIGNED_FREE`: align by hand over pool, storing
  the raw pointer one slot below the aligned one, with the size sum checked
  by `RtlSizeTAdd`. Pool guarantees 16 bytes below a page and a page above;
  the workspace layout assumes a cache line.
- `ZXC_POPCOUNT32` / `ZXC_POPCOUNT64`: the SWAR fallback. The `__popcnt`
  intrinsics emit POPCNT, which faults on CPUs without it, and nothing in a
  kernel build may assume that instruction.

### `zxc_km.props`

One property sheet, imported by both projects after `Microsoft.Cpp.props`,
so that the two never disagree on the rules the spike found. Its contents:

- `TargetVersion` `Windows10` with `_NT_TARGET_VERSION` at Windows 10 2004
  (`0xA000008`) on both platforms, unless the caller set them. The toolset
  derives `NTDDI_VERSION`, `_WIN32_WINNT`, the subsystem version and the
  `/GS` support library from them. Plan B: building with
  `TargetVersion=Windows7` on x64 or Win32 selects the Windows 7 body of
  the allocation routine and yields one library that runs on every Windows
  from 7 up. Plan B requires the 22000 kit: the sheet sets
  `WindowsTargetPlatformVersion` to `10.0.22000.0` for it, and fails early
  with a plain message when that kit is not installed, because the 22621
  and newer project systems refuse both the target and the platform.
- `IgnoreStandardIncludePath` true (`/X`), and `AdditionalIncludeDirectories`
  set to, in order: `$(VC_IncludePath)`, `$(UniversalCRT_IncludePath)`,
  `$(KM_IncludePath)`, `$(KIT_SHARED_IncludePath)`. This is the include-order
  rule. The toolset's `km\crt` directory is deliberately absent.
- `AdditionalOptions`: `/std:c17 /Zc:preprocessor-` on both platforms, so
  that x64 and ARM64 compile the same way.
- `PreprocessorDefinitions`: `ZXC_STATIC_DEFINE`, `ZXC_ONLY_DEFAULT`,
  `ZXC_FUNCTION_SUFFIX=_default`. `ZXC_ONLY_DEFAULT` rather than
  `ZXC_DISABLE_SIMD`: baseline SSE2 and NEON are legal in kernel mode, and
  the dispatcher's CPUID probes compile out.
- `WarningLevel` 4, `TreatWarningAsError` true, `DisableSpecificWarnings`
  4244 and 4310, the two suppressions the CMake build applies. A third
  suppression, C4242 on 32-bit, cannot live here: the kit's force-included
  `warning.h` raises that warning by pragma after the command line, so the
  dependency header disables it by pragma on `_M_IX86` instead.
- `DebugInformationFormat` OldStyle (`/Z7`): the objects carry their debug
  info, so the library needs no companion PDB.
- `OmitDefaultLibName` true (`/Zl`): no default-library directive in the
  objects.
- Release: `Optimization` MaxSpeed and `FavorSizeOrSpeed` Speed (`/O2`),
  instead of the toolset's size-first default, because this is a codec.
- Spectre mitigation is left at the toolset default, which is on for
  drivers. A consumer who wants it off passes the toolset's own
  `Driver_SpectreMitigation=false`.
- `OutDir` and `IntDir` under `build/`.

### `zxc_km.vcxproj`

`ConfigurationType` StaticLibrary, `PlatformToolset`
WindowsKernelModeDriver10.0, `TargetVersion` and kit from the property
sheet, platforms x64, ARM64 and Win32, configurations Debug and Release.
Win32 is plan B only and builds with the 22000 kit, which the property
sheet checks for and reports.

A target `ZxcStage`, run before `ClCompile`, copies into `build/stage/zxc/`:
`include/*.h`, the seven units `zxc_common.c`, `zxc_pivco_tables.c`,
`zxc_dispatch.c`, `zxc_dict.c`, `zxc_compress.c`, `zxc_decompress.c`,
`zxc_huffman.c`, every `src/lib/*.h`, and `src/lib/vendors/*`. It then copies
`contrib/windows-kernel/zxc_deps.h` over the staged stock header. The copy
is the point: `zxc_internal.h` includes the dependency header with quotes,
so the file next to it always wins, and a replacement has to sit there.
The target declares inputs and outputs so an unchanged tree does not restage.

The `ClCompile` items are the seven staged units. The library's include
directories are the staged `include/`, `src/lib/` and `src/lib/vendors/`,
ahead of the kernel directories from the property sheet. The output is
`build/<Platform>/<Configuration>/zxc_km.lib`.

### `zxc_selftest.vcxproj` and `selftest.c`

`ConfigurationType` Driver, `DriverType` WDM, `DriverTargetPlatform`
Desktop, a `ProjectReference` to the library project so the archive is
linked and built first, `SignMode` Off by default, no INF. It compiles
`selftest.c` with the staged public headers on the path.

`selftest.c` is the Linux module's init, translated: a legacy software
driver whose `DriverEntry` calls `ExInitializeDriverRuntime(DrvRtPoolNxOptIn)`
first when the target is below Windows 10 2004, sets an unload routine,
allocates a static decode workspace from non-paged pool sized for
`PAGE_SIZE` blocks through the same two-body allocation the dependency
header uses, and at
levels 3 and 7 round-trips one block through `zxc_compress_block` and
`zxc_decompress_block_safe`, one frame through `zxc_compress` and
`zxc_decompress`, and one in-place frame through
`zxc_decompress_inplace_dctx` on the static context. Each level logs one
line through `DbgPrintEx` at error level, so it shows without a filter mask,
and the entry returns `STATUS_DATA_ERROR` on a mismatch or
`STATUS_INSUFFICIENT_RESOURCES` on allocation failure, so a bad build
refuses to load. All pool allocations carry the driver's own tag.

### `.gitignore`

`build/`.

## Consumer contract

The README states it; the self-test project demonstrates it.

- **Link** `zxc_km.lib` for the matching platform, configuration and target
  version. The official toolset supplies everything else; a hand-rolled
  link also needs `ntoskrnl.lib`, the `/GS` support library
  (`BufferOverflowK.lib` for a target below Windows 8,
  `BufferOverflowFastFailK.lib` otherwise), and `arm64rt.lib` on ARM64.
- **Target below Windows 10 2004** (plan B, built with
  `TargetVersion=Windows7`): define
  `POOL_NX_OPTIN` to 1 in the driver and call
  `ExInitializeDriverRuntime(DrvRtPoolNxOptIn)` at the top of `DriverEntry`,
  before any allocation. The library's pool is then non-executable on
  Windows 8 and later. Without the call the library still works, but its
  non-paged allocations are executable there, which Driver Verifier and
  certification tests report. The self-test shows the call.
- **Include** `include/` from the ZXC tree (or the staged copy), and adopt
  the include order above in the consumer project, because `zxc.h` pulls
  `<stdint.h>`. The README carries the three-line project snippet.
- **Alternative:** vendor the seven units into the driver project with the
  same rules, importing the rules sheet, which is what the Linux tree does.
  The staging rule applies unchanged.
- **Target version through a project reference.** A `TargetVersion` in the
  consumer's project file does not cross a `ProjectReference`; the library
  would build for its own default. The reference forwards it with
  `AdditionalProperties`, as the self-test project does, or the build passes
  it globally with `/p:`.
- **IRQL.** Build contexts at PASSIVE_LEVEL from non-paged pool at init,
  one per CPU, then decode at up to DISPATCH_LEVEL with no allocation. A
  heap context allocates lazily on the first decode and again on the first
  entropy-coded block; the header makes those non-paged, so they are legal
  at DISPATCH_LEVEL but can still fail under pressure. Nothing in the
  library is marked pageable, so its code stays resident.
- **Stack.** On Linux with GCC, decode peaks at 4.5 KiB and compression at
  12.3 KiB for levels 6 and 7 over the whole call path. The MSVC frames the
  spike measured are of the same order, but the README states the Windows
  whole-path figures only once they are measured on Windows, with a painted
  stack in the self-test driver. Kernel stacks are 12 KB on x86 and 24 KB on
  x64 and ARM64, shared with whatever the I/O stack above already used.
  Decoding fits; compressing at levels 6 and 7 wants
  `KeExpandKernelStackAndCalloutEx` or levels 1 to 5.
- **SIMD.** The library uses SSE2 on x64 and NEON on ARM64, both allowed in
  kernel mode without save and restore. It never uses AVX. Do not build it
  with `/arch:AVX*`; `/kernel` refuses that anyway. On x86 the library is
  scalar, by `/arch:IA32`, because x86 kernel mode does not preserve XMM
  state for drivers.
- **x86.** Plan B only, built with the 22000 kit: newer project systems
  refuse the platform, and the 26100 kit ships no x86 kernel libraries at
  all. The property sheet sets `/arch:IA32` on Win32: `/kernel` alone
  leaves the 32-bit compiler on SSE2 code generation, with `_M_IX86_FP` at
  2, which both lets the compiler use XMM registers and switches the
  core's explicit SSE2 paths on; IA32 takes both out, and `/kernel` rejects
  `/arch:SSE2` outright. The kernel toolset compiles x86 with `/Gz`, so the library's
  public functions are stdcall and the consumer must be built the same
  way, which any kernel-toolset project is. The x86 kernel does not export
  `memcmp`; the kit's `memcmp.lib` provides it and the toolset links it
  automatically, a hand-rolled link names it. The 64-bit arithmetic helpers
  the compiler emits come from `ntoskrnl`. The 12 KB x86 kernel stack
  leaves room for decoding and for compression at levels 1 to 5 only.

## CI: `windows-kernel.yml`

Runner `windows-2025`, whose image ships Visual Studio 2022 17.14 with the
Windows Driver Kit component. Triggers mirror `kernel.yml`: push to `main`
and pull requests to `main` and `candidate` on changes under
`contrib/windows-kernel/`, `include/`, `src/lib/` or the workflow itself;
weekly on a different hour; manual dispatch. Same permissions and
concurrency block, actions pinned by SHA as elsewhere in the repo.

Two jobs.

The first, on the pull-request path, is a matrix over `x64` and `ARM64` at
the default Windows 10 2004 target. The first run showed the runner image
carries the WDK's Visual Studio extension but not the kit content, so the
job installs the 26100 WDK with Microsoft's installer before it builds, and
takes MSBuild from the Visual Studio instance that has the extension rather
than the newest instance.

The second job for plan B was designed to install the 22000 kit and build
`x64` and `Win32` at the Windows 7 target on the schedule and on dispatch.
It was dropped after the first runs: the 22000 kit ships its MSBuild task
assemblies for Visual Studio 2019 only, selected by `$(VisualStudioVersion)`,
and the runner image carries Visual Studio 2022 and 2026, so the kit's
targets cannot load there. Plan B is therefore verified locally against the
22000 EWDK, the fallback this spec names below, and the README says so.

1. Checkout.
2. Locate the toolchain in PowerShell: MSBuild through `vswhere`, the
   kernel headers at `Windows Kits\10\Include\<version>\km\ntddk.h`, and
   `dumpbin` under the VC tools. Fail with a plain message naming what is
   missing, so a runner-image change reads as such and not as a code error.
3. Build `zxc_selftest.vcxproj` for Release, the matrix platform and target
   version, with `SignMode=Off`. Compiler and linker warnings are errors
   through the property sheet; MSBuild's own warnings are not.
4. Assert with `dumpbin /IMPORTS` that the image imports `ntoskrnl.exe` and
   nothing else, and with `dumpbin /HEADERS` that the subsystem is Native,
   the subsystem version matches the leg's target (6.01 or 10.00) and the
   machine matches the platform. This is the job's `modinfo`.

No artifacts are uploaded; the Linux job uploads none either.

## Documentation

- `contrib/windows-kernel/README.md`, in the Linux README's order: what the
  tree contains; what to build and what you get; the consumer recipe, both
  linking and vendoring; the include-order rule and the clash behind it;
  the C-standard flags; contexts and IRQL, with the workspace table reused
  from the Linux README; the stack budget with the measured frames; the SIMD
  rules, including the inert auto-vectorizer dispatch; the default target,
  the Windows 7 plan B build with its runtime opt-in call, and which kits
  build which target; loading
  the self-test on a test-signed VM (`bcdedit /set testsigning on`,
  `signtool` with a test certificate, `sc create ... type= kernel`,
  `sc start`, the DebugView or debugger line to expect, `sc stop`,
  `sc delete`) and running it under Driver Verifier, with the Windows 7
  note that a SHA-256 signature needs the SHA-2 support update or a SHA-1
  test certificate; licensing (BSD-3-Clause, no GPL interaction on Windows).
- `docs/INSTALL.md` and `docs/API.md`: add the Windows tree next to the
  Linux pointer.
- `.github/workflows/README.md`: an entry after `kernel.yml`.

## Testing

- CI proves compile and link on the current kit for both platforms and
  pins the import set.
- The self-test driver, loaded by hand on a test-signed VM, proves the round
  trips; Driver Verifier's standard settings catch IRQL, pool and tag
  mistakes. The README documents the expected log line per level.
- Formatting: `make format-check` already covers `contrib/`, so the new C
  files follow `.clang-format`.
- No new tests under `tests/`: the core is unchanged and covered there.

## Decisions and alternatives

- **Build system: MSBuild on the kernel toolset**, not a script and not
  CMake. The toolset owns the kernel flag set, the link inputs and signing,
  consumers can reference the project, and the CI image has it. A script
  would re-implement and drift; a CMake toolchain module would be a
  third-party dependency and would mix kernel targets into a user-mode
  build.
- **A named property sheet, not `Directory.Build.props`.** MSBuild imports
  the directory-wide file before the toolset's own property sheets, which
  set the include path unconditionally; a sheet imported after
  `Microsoft.Cpp.props` is evaluated where the override has to be.
- **Staging copy, not a force-include.** Force-including the kernel header
  would work only because the stock header's include guard then skips the
  stock file, a coupling to a guard name. The copy mirrors Linux and is
  explicit.
- **Release at `/O2`.** The toolset's `/Ox /Os` emits no SSE4.1 dispatch at
  all, but a codec should favour speed; the dispatch is inert anyway.
- **Spectre on by default.** Matches the toolset and Microsoft's guidance
  for drivers; the lfence cost is the consumer's call through the toolset's
  own switch.
- **Windows 10 2004 by default, Windows 7 as plan B.** The default build
  uses the current allocator, is non-executable by construction, and asks
  nothing of the consumer's `DriverEntry`. Windows 7 is the floor the user
  asked for, accepted as not ideal: one property selects it, at the cost of
  a deprecated-but-present pool routine behind the kit's wrapper and a
  runtime opt-in call in the consumer's `DriverEntry`, which is Microsoft's
  own mechanism for drivers that span Windows 7 and later. CI builds plan B
  on every run, so it cannot rot.
- **x86 as part of plan B.** The user asked about it twice; the 22000 kit
  builds it through the project system, and the only costs are one pragma
  and one `/arch` flag, a Win32 platform in the projects, and the scheduled
  CI job plan B already needs. It is not on the pull-request path because
  that kit is not on the runner.
- **Plan B on the 22000 kit, not the latest.** Not a choice: kits newer
  than 22000 refuse down-level targets and x86 in their MSBuild targets,
  while their compilers still build the code. The 22000 kit carries the
  same MSVC 14.28 as the 20348 kit, so plan B's compiler floor is Visual
  Studio 2019 16.8 either way.
- **Signing off by default.** A loadable binary needs a test certificate in
  the developer's store, a side effect a library repo should not cause on a
  plain build.

## Items to verify during implementation

Each has a fallback; none changes the design.

- A StaticLibrary configuration under the kernel toolset. Fallback: build
  the archive with the `Lib` task from a Utility project.
- A Driver configuration without an INF runs through the toolset's full
  pipeline (the INF stamping and catalog steps should skip). Fallback: an
  explicit property disabling them.
- `IgnoreStandardIncludePath` together with the toolset's force-included
  `warning.h`, which must resolve through the explicit `shared` directory.
- The `windows-2025` image carries the kernel headers and the ARM64 build
  tools, not only the WDK Visual Studio extension. Fallback: a guarded
  `winget install` of the WDK, or the WDK NuGet package, at a cost of
  minutes per run.
- Property-sheet metadata surviving the toolset's own item definitions.
  Fallback: move the affected settings into each project after the toolset
  imports.
- The toolset refusing `TargetVersion=Windows7` on ARM64 with a readable
  error, so the README can say "x64 and x86 only" and leave it there.
  Fallback: a guard in the property sheet that fails the build with its own
  message.
- `winget` offering the 22000 WDK, installing it on the runner within a
  tolerable time, and the 22000 kit coexisting with the runner's 26100 kit
  under one `Windows Kits\10` root with `WindowsTargetPlatformVersion`
  selecting it. Fallback: the plan B job installs the 22000 kit from
  Microsoft's download link instead, or plan B stays a documented local
  build against the EWDK.
