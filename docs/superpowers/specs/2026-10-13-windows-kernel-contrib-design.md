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

## Evidence

A throwaway spike on 2026-10-13 compiled the seven core units under
`/kernel /std:c17 /W4 /WX`, archived them, and linked a self-test driver
against the archive, on x64 and ARM64, with two kits:

| Kit | Toolset | x64 | ARM64 |
|---|---|---|---|
| 22621 (Windows 11 22H2 EWDK) | MSVC 14.31 | pass | pass |
| 26100.6584 (Windows 11 25H2 EWDK, the line on GitHub `windows-2025` runners) | MSVC 14.44 | pass | pass |

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

## Scope

In scope:

- `contrib/windows-kernel/`: dependency header, shared project settings, the
  library project, the self-test driver project and source, README.
- `.github/workflows/windows-kernel.yml`: build both platforms, assert the
  image's imports.
- Pointers in `docs/INSTALL.md`, `docs/API.md` and
  `.github/workflows/README.md`.

Out of scope:

- Any change to `src/lib` or `include`. The core stays unaware of Windows
  kernel mode; the vendored `zxc_deps.h` replacement is the whole contract.
- Loading the driver in CI. Runtime proof is a manual step on a test-signed
  VM, as it is for the Linux module.
- 32-bit x86 and ARM32. The 26100 kit does not support them, and the spike
  did not try them.
- Shipping the library as a release artifact.
- Pageable code sections, lookaside lists, or any allocator beyond pool.

## Tree

```
contrib/windows-kernel/
  README.md             the guide, section for section like linux-kernel/README.md
  zxc_deps.h            the kernel replacement for src/lib/zxc_deps.h
  zxc_km.props          settings both projects import: include order, flags, defines
  zxc_km.vcxproj        static library: stages the sources, builds zxc_km.lib
  zxc_selftest.vcxproj  WDM driver: DriverEntry round trips, links zxc_km.lib
  selftest.c            the driver's init
  .gitignore            build/
```

Everything the build produces lands under `contrib/windows-kernel/build/`:
`build/stage/` for the staged sources, `build/<Platform>/<Configuration>/`
for objects and outputs.

## Components

### `zxc_deps.h`

Replaces `src/lib/zxc_deps.h` in the staged tree. It includes `<ntddk.h>`,
`<ntintsafe.h>`, `<limits.h>`, `<stddef.h>`, `<stdint.h>` and `<string.h>`.
Unlike the Linux header it needs no shims: the compiler's `<stdint.h>` and
`<limits.h>` are freestanding, the UCRT `<string.h>` declares the memory
primitives without import decoration, and `ntoskrnl` exports them.

Guards, each a `#error` with a one-line reason:

- `_KERNEL_MODE` undefined: the header is only for `/kernel` builds.
- `NTDDI_VERSION` below `NTDDI_WIN10_VB`: `ExAllocatePool2` needs a
  Windows 10 2004 target or later. There is no fallback to the older pool
  routines.

Definitions:

- `static_assert(expr, msg)` maps to `_Static_assert` when `<assert.h>` has
  not defined it. `zxc_internal.h` checks its constants with the two-argument
  form.
- `ZXC_POOL_TAG`: a four-character tag that reads `zxc ` in pool dumps.
- `ZXC_MALLOC`: `ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, size, tag)`,
  with a zero size rounded up to one byte because Driver Verifier flags
  zero-length allocations. Non-paged, because a context built at
  PASSIVE_LEVEL must stay usable at DISPATCH_LEVEL and a heap context's lazy
  allocations may happen there.
- `ZXC_CALLOC`: the size product checked with `RtlSizeTMult`, then the same
  call without the uninitialized flag, since the allocator zeroes by default.
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
  4244 and 4310, the two suppressions the CMake build applies.
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
WindowsKernelModeDriver10.0, `TargetVersion` Windows10, platforms x64 and
ARM64, configurations Debug and Release.

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
driver whose `DriverEntry` sets an unload routine, allocates a static
decode workspace from non-paged pool sized for `PAGE_SIZE` blocks, and at
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

- **Link** `zxc_km.lib` for the matching platform and configuration. The
  official toolset supplies everything else; a hand-rolled link also needs
  `ntoskrnl.lib`, `BufferOverflowFastFailK.lib`, and `arm64rt.lib` on ARM64.
- **Include** `include/` from the ZXC tree (or the staged copy), and adopt
  the include order above in the consumer project, because `zxc.h` pulls
  `<stdint.h>`. The README carries the three-line project snippet.
- **Alternative:** vendor the seven units into the driver project with the
  same property sheet settings, which is what the Linux tree does. The
  staging rule applies unchanged.
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
  with `/arch:AVX*`; `/kernel` refuses that anyway.

## CI: `windows-kernel.yml`

Runner `windows-2025`, whose image ships Visual Studio 2022 17.14 with the
Windows Driver Kit component. Triggers mirror `kernel.yml`: push to `main`
and pull requests to `main` and `candidate` on changes under
`contrib/windows-kernel/`, `include/`, `src/lib/` or the workflow itself;
weekly on a different hour; manual dispatch. Same permissions and
concurrency block, actions pinned by SHA as elsewhere in the repo.

One job, matrix over `x64` and `ARM64`:

1. Checkout.
2. Locate the toolchain in PowerShell: MSBuild through `vswhere`, the
   kernel headers at `Windows Kits\10\Include\<version>\km\ntddk.h`, and
   `dumpbin` under the VC tools. Fail with a plain message naming what is
   missing, so a runner-image change reads as such and not as a code error.
3. Build `zxc_selftest.vcxproj` for Release and the matrix platform with
   `SignMode=Off`. Compiler and linker warnings are errors through the
   property sheet; MSBuild's own warnings are not.
4. Assert with `dumpbin /IMPORTS` that the image imports `ntoskrnl.exe` and
   nothing else, and with `dumpbin /HEADERS` that the subsystem is Native
   and the machine matches the platform. This is the job's `modinfo`.

No artifacts are uploaded; the Linux job uploads none either.

## Documentation

- `contrib/windows-kernel/README.md`, in the Linux README's order: what the
  tree contains; what to build and what you get; the consumer recipe, both
  linking and vendoring; the include-order rule and the clash behind it;
  the C-standard flags; contexts and IRQL, with the workspace table reused
  from the Linux README; the stack budget with the measured frames; the SIMD
  rules, including the inert auto-vectorizer dispatch; loading the self-test
  on a test-signed VM (`bcdedit /set testsigning on`, `signtool` with a test
  certificate, `sc create ... type= kernel`, `sc start`, the DebugView or
  debugger line to expect, `sc stop`, `sc delete`) and running it under
  Driver Verifier; licensing (BSD-3-Clause, no GPL interaction on Windows).
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
- **Windows 10 2004 floor.** The oldest Windows 10 the current kits target
  with `ExAllocatePool2`; supporting older pool routines would add a second
  allocator path nobody has asked for.
- **x64 and ARM64 only.**
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
