# Windows Kernel Library (`contrib/windows-kernel`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a Windows kernel-mode driver link ZXC's buffer API as a static library, `zxc_km.lib`, built from the unmodified core sources, with a self-test driver as the reference consumer and a build-only CI job.

**Architecture:** A `contrib/windows-kernel/` tree mirroring `contrib/linux-kernel`: a replacement `zxc_deps.h` that maps the libc seams onto the executive pool, two MSBuild property sheets that encode the rules a spike found (include order, C standard, defines, x86 flags, target version and kit), a static-library project that stages the seven core units and builds them on the WDK's `WindowsKernelModeDriver10.0` toolset, and a WDM driver project that links the result. The default target is Windows 10 2004; Windows 7 and 32-bit x86 are "plan B", one property away and built with the 22000 kit.

**Tech Stack:** MSBuild `.vcxproj`/`.props` on the `WindowsKernelModeDriver10.0` toolset; EWDK 26100 (Visual Studio 2022 17.14 build tools, MSVC 14.44) for the default target; EWDK 22000 (Visual Studio 2019 16.9 build tools, MSVC 14.28) for plan B; PowerShell 7 for checks; GitHub Actions `windows-2025`.

**Spec:** `docs/superpowers/specs/2026-10-13-windows-kernel-contrib-design.md`

## Global Constraints

- No change to `src/lib` or `include`. The vendored `zxc_deps.h` replacement is the whole contract.
- Include order, in every kernel compile: the stage's `include`, then `$(VC_IncludePath)`, `$(UniversalCRT_IncludePath)`, `$(KM_IncludePath)`, `$(KIT_SHARED_IncludePath)`, with the standard include path ignored (`/X`) and the kit's `km\crt` directory never on it.
- Compiler options on every unit: `/std:c17 /Zc:preprocessor-`, `/W4 /WX`, warnings 4244 and 4310 disabled, `/Z7`, `/Zl`; Release is `/O2` (`Optimization` MaxSpeed, `FavorSizeOrSpeed` Speed); Spectre mitigation left at the toolset default (on).
- Defines on every unit: `ZXC_STATIC_DEFINE;ZXC_ONLY_DEFAULT;ZXC_FUNCTION_SUFFIX=_default`.
- Default target: `TargetVersion` `Windows10` with `_NT_TARGET_VERSION` `0xA000008` (Windows 10 2004). Plan B: `TargetVersion=Windows7`, x64 or Win32 only, built with the 22000 kit (`WindowsTargetPlatformVersion` `10.0.22000.0`).
- x86 (Win32): `/arch:IA32` (`EnableEnhancedInstructionSet` NoExtensions), and `#pragma warning(disable : 4242)` in the dependency header on `_M_IX86`.
- Floor: the dependency header errors below `NTDDI_WIN7` and outside `/kernel` builds.
- Entry point `GsDriverEntry` (toolset default); signing off by default (`SignMode` Off).
- All outputs under `contrib/windows-kernel/build/` (`build/stage/` for sources, `build/<Platform>/<Configuration>/` for objects and outputs); `build/` is git-ignored.
- New C files follow `.clang-format` (the repo's `format-check` target covers `contrib/`); every new source file carries the `SPDX-License-Identifier: BSD-3-Clause` header used across the repo.
- GitHub Actions pinned by SHA, as in `.github/workflows/kernel.yml`; `permissions: contents: read`; the same concurrency block.
- Commits follow the repo's conventional-commit style (`feat(contrib): ...`, `docs(contrib): ...`, `ci: ...`).

## Review Focus

Inputs and failure modes the spec implies but no CI step can exercise end to end. Each line names the test or guard that pins it, in the owning task.

1. **A plan B consumer that never calls `ExInitializeDriverRuntime(DrvRtPoolNxOptIn)`** keeps working but allocates executable non-paged pool on Windows 8 and later. Pinned in Task 3: the plan B library must reference `ExDefaultNonPagedPoolType`, the default library must not, which proves the opt-in path compiled in; the self-test shows the call; the README states the rule.
2. **Compressing at level 6 or 7 on the 12 KB x86 kernel stack** overflows it. Pinned in Task 2: the self-test's dense level is 5 on `_M_IX86`, enforced by a `static_assert`, and the README's stack section says so.
3. **A zero-length allocation under Driver Verifier** is flagged as a pool misuse. Pinned in Task 1 by the header rounding a zero size up to one byte, and in Task 4 by the README's instruction to run the self-test under Verifier's standard settings, which checks it.
4. **Pool exhaustion** (any `ExAllocatePool*` returning NULL) must surface as an error status, never a bug check. Pinned in Task 2: the self-test returns `STATUS_INSUFFICIENT_RESOURCES` on any NULL and frees what it got, and the README names Verifier's low-resource simulation as the way to exercise it.
5. **Plan B asked for where it cannot be built**: `TargetVersion=Windows7` on ARM64, or on a machine without the 22000 kit. Pinned in Task 3 by two expected-failure builds that must stop with a readable message rather than a mis-targeted binary.

## Build environments on this machine

The EWDK ISOs live in `L:\iso\dev`. Mount one, note its drive letter, and run MSBuild from its environment. Drive letters vary between mounts, so every command below takes the letter from a variable.

```powershell
# 26100 EWDK: the default target. Mounts as some letter, here captured in $e.
$e = (Mount-DiskImage -ImagePath "L:\iso\dev\EWDK_ge_release_svc_prod1_26100_250904-1728.iso" -PassThru | Get-Volume).DriveLetter
# 22000 EWDK: plan B.
$h = (Mount-DiskImage -ImagePath "L:\iso\dev\EWDK_co_release_22000_210604-1628.iso" -PassThru | Get-Volume).DriveLetter
```

Run MSBuild through the EWDK's own environment script; `amd64` selects the x64-hosted tools, which cross-compile ARM64 and x86 too. Always from the repository root (`F:\github\zxc`):

```powershell
cmd /c "call ${e}:\BuildEnv\SetupBuildEnv.cmd amd64 >nul 2>&1 && msbuild <project> /nologo /m /p:Configuration=Release /p:Platform=x64"
```

`dumpbin` for the checks: `${e}:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe` on the 26100 EWDK, `${h}:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.28.29910\bin\Hostx64\x64\dumpbin.exe` on the 22000 one.

Dismount when done: `Dismount-DiskImage -ImagePath <iso>`.

`clang-format` is not installed here. Install the pinned major the CI uses with `pip install "clang-format>=22,<23"` (falls back to `winget install LLVM.LLVM` if pip is unavailable), then check with `clang-format --style=file --dry-run --Werror <files>` from the repository root, which is what `make format-check` runs.

Work on the existing branch `feat/windows-kernel`, which already holds the spec.

---

### Task 1: Dependency header, property sheets, library project

**Files:**
- Create: `contrib/windows-kernel/zxc_deps.h`
- Create: `contrib/windows-kernel/zxc_km_target.props`
- Create: `contrib/windows-kernel/zxc_km.props`
- Create: `contrib/windows-kernel/zxc_km.vcxproj`
- Create: `contrib/windows-kernel/.gitignore`

**Interfaces:**
- Consumes: `src/lib/*.c` (seven units), `src/lib/*.h`, `src/lib/vendors/*`, `include/*.h`, unchanged.
- Produces: `contrib/windows-kernel/build/<Platform>/<Configuration>/zxc_km.lib`; the staged tree `contrib/windows-kernel/build/stage/{include,src/lib,src/lib/vendors}`; MSBuild properties `ZxcRoot`, `ZxcStage`, `ZxcPlanB`; project GUID `{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}`; the library's public symbols are the ZXC buffer API from `include/` (stdcall on Win32, as the toolset's `/Gz` makes them).

The spec describes one property sheet. It is two files here because the toolset reads `TargetVersion`, `_NT_TARGET_VERSION` and `WindowsTargetPlatformVersion` when its own sheets are imported, during `Microsoft.Cpp.props`, so those must exist before `Microsoft.Cpp.Default.props`, while every compile setting must come after `Microsoft.Cpp.props` or the toolset's own item definitions override it.

- [ ] **Step 1: Write the dependency header**

Create `contrib/windows-kernel/zxc_deps.h`:

```c
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file zxc_deps.h
 * @brief Windows kernel-mode replacement for src/lib/zxc_deps.h.
 *
 * Maps the libc seams onto the executive pool. Everything else the core
 * reaches for - <stdint.h>, <limits.h>, <stddef.h>, <string.h> - is
 * freestanding in a WDK build, and ntoskrnl exports the memory primitives,
 * so unlike contrib/linux-kernel no header shim is needed.
 *
 * One allocation routine with two bodies, selected on the target version:
 * ExAllocatePool2 from Windows 10 2004, the kit's ExAllocatePoolUninitialized
 * wrapper below that (plan B), where POOL_NX_OPTIN and the driver's
 * ExInitializeDriverRuntime(DrvRtPoolNxOptIn) call make the pool
 * non-executable on Windows 8 and later.
 */

#ifndef ZXC_DEPS_H
#define ZXC_DEPS_H

#ifndef _KERNEL_MODE
#error "contrib/windows-kernel/zxc_deps.h: build with /kernel (the WindowsKernelModeDriver10.0 toolset)"
#endif

// sdkddkver.h gives NTDDI_VERSION its default when the build left it unset,
// so the version tests below never see an undefined macro.
#include <sdkddkver.h>

#if NTDDI_VERSION < NTDDI_WIN7
#error "contrib/windows-kernel: Windows 7 is the floor (NTDDI_VERSION is below NTDDI_WIN7)"
#endif

// Plan B: below Windows 10 2004, NonPagedPool resolves to the kit's run-time
// selected default pool type, which the driver's DriverEntry sets with
// ExInitializeDriverRuntime(DrvRtPoolNxOptIn).
#if NTDDI_VERSION < NTDDI_WIN10_VB
#ifndef POOL_NX_OPTIN
#define POOL_NX_OPTIN 1
#endif
#endif

#include <ntddk.h>
#include <ntintsafe.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// <assert.h> is the hosted spelling of the keyword; zxc_internal.h checks its
// constants with the two-argument form.
#ifndef static_assert
#define static_assert(expr, msg) _Static_assert(expr, msg)
#endif

// The kit's force-included warning.h raises C4242 to level 3 by pragma, past
// any /wd on the command line. On a 32-bit size_t the core's block-bounded
// uint64 -> size_t narrowing at call sites trips it; the CMake build leaves
// the same class unchecked on 32-bit hosts for the same reason.
#if defined(_M_IX86)
#pragma warning(disable : 4242)
#endif

/** @brief Pool tag; pool dumps display tags reversed, so this reads "zxc ". */
#define ZXC_POOL_TAG ((ULONG)' cxz')

// Non-paged: a context built at PASSIVE_LEVEL must stay usable at
// DISPATCH_LEVEL, and the lazy allocations of a heap context may happen there.
// A zero size is rounded up to one byte: Driver Verifier flags zero-length
// allocations.
#if NTDDI_VERSION >= NTDDI_WIN10_VB
static inline void* zxc_km_alloc(const size_t size, const int zero) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED | (zero ? 0 : POOL_FLAG_UNINITIALIZED),
                           size ? size : 1, ZXC_POOL_TAG);
}
#else
// The kit's inline wrapper over ExAllocatePoolWithTag, which the kit otherwise
// deprecates. Zeroing is done here rather than through ExAllocatePoolZero, so
// correctness never depends on the driver's runtime-init call; only the NX
// property of the pool does.
static inline void* zxc_km_alloc(const size_t size, const int zero) {
    const size_t n = size ? size : 1;
    void* p = ExAllocatePoolUninitialized(NonPagedPool, n, ZXC_POOL_TAG);
    if (p && zero) RtlZeroMemory(p, n);
    return p;
}
#endif

static inline void* zxc_km_calloc(const size_t nmemb, const size_t size) {
    size_t total;
    if (!NT_SUCCESS(RtlSizeTMult(nmemb, size, &total))) return NULL;
    return zxc_km_alloc(total, 1);
}

// ExFreePoolWithTag(NULL) bug-checks; libc free(NULL) is a no-op.
static inline void zxc_km_free(void* ptr) {
    if (ptr) ExFreePoolWithTag(ptr, ZXC_POOL_TAG);
}

#define ZXC_MALLOC(size) zxc_km_alloc((size), 0)
#define ZXC_CALLOC(nmemb, size) zxc_km_calloc((nmemb), (size))
#define ZXC_FREE(ptr) zxc_km_free(ptr)

// No ZXC_REALLOC: pool has no realloc, and these units never reallocate; a
// future use fails to build.

// Pool aligns to 16 bytes below a page and to a page above: align by hand for
// the cache-line workspaces, as the Linux header does over kvmalloc.
static inline void* zxc_km_aligned_alloc(const size_t size, size_t alignment) {
    void* mem;
    void* ptr;
    size_t total;

    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (!NT_SUCCESS(RtlSizeTAdd(size, alignment + sizeof(void*), &total))) return NULL;
    mem = zxc_km_alloc(total, 0);
    if (!mem) return NULL;
    ptr = (void*)(((uintptr_t)mem + sizeof(void*) + (alignment - 1)) & ~(uintptr_t)(alignment - 1));
    ((void**)ptr)[-1] = mem;
    return ptr;
}

static inline void zxc_km_aligned_free(void* ptr) {
    if (ptr) ExFreePoolWithTag(((void**)ptr)[-1], ZXC_POOL_TAG);
}

#define ZXC_ALIGNED_MALLOC(size, alignment) zxc_km_aligned_alloc((size), (alignment))
#define ZXC_ALIGNED_FREE(ptr) zxc_km_aligned_free(ptr)

// SWAR popcount: __popcnt emits POPCNT, which faults on CPUs without it, and
// nothing in a kernel build may assume that instruction.
static inline int zxc_km_popcount32(const uint32_t v) {
    uint32_t x = v - ((v >> 1) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2) & 0x33333333U);
    x = (x + (x >> 4)) & 0x0F0F0F0FU;
    return (int)((x * 0x01010101U) >> 24);
}
static inline int zxc_km_popcount64(const uint64_t v) {
    return zxc_km_popcount32((uint32_t)v) + zxc_km_popcount32((uint32_t)(v >> 32));
}
#define ZXC_POPCOUNT32(v) zxc_km_popcount32(v)
#define ZXC_POPCOUNT64(v) zxc_km_popcount64(v)

#endif /* ZXC_DEPS_H */
```

- [ ] **Step 2: Write the early property sheet**

Create `contrib/windows-kernel/zxc_km_target.props`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- ZXC - High-performance lossless compression -->
<!-- Copyright (c) Bertrand Lebonnois and contributors. -->
<!--
  Imported by both projects before Microsoft.Cpp.Default.props. The kernel
  toolset reads TargetVersion, _NT_TARGET_VERSION and
  WindowsTargetPlatformVersion when its own sheets are imported, so they must
  exist by then. Every compile setting lives in zxc_km.props, imported after
  Microsoft.Cpp.props so the toolset's own item definitions do not override it.
-->
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <!-- Default target: Windows 10 2004 (NTDDI_WIN10_VB), the first with ExAllocatePool2. -->
    <TargetVersion Condition="'$(TargetVersion)' == ''">Windows10</TargetVersion>
    <_NT_TARGET_VERSION Condition="'$(_NT_TARGET_VERSION)' == '' and '$(TargetVersion)' == 'Windows10'">0xA000008</_NT_TARGET_VERSION>
    <!-- Plan B: a Windows 7 target or the Win32 platform. Kits newer than 22000
         refuse both in their MSBuild targets, so plan B builds with the 22000 kit. -->
    <ZxcPlanB Condition="'$(TargetVersion)' == 'Windows7' or '$(Platform)' == 'Win32'">true</ZxcPlanB>
    <WindowsTargetPlatformVersion Condition="'$(WindowsTargetPlatformVersion)' == '' and '$(ZxcPlanB)' == 'true'">10.0.22000.0</WindowsTargetPlatformVersion>
    <DriverTargetPlatform Condition="'$(DriverTargetPlatform)' == ''">Desktop</DriverTargetPlatform>
  </PropertyGroup>
</Project>
```

- [ ] **Step 3: Write the main property sheet**

Create `contrib/windows-kernel/zxc_km.props`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- ZXC - High-performance lossless compression -->
<!-- Copyright (c) Bertrand Lebonnois and contributors. -->
<!--
  Imported by both projects after Microsoft.Cpp.props. What a kernel-mode
  build of zxc needs beyond the toolset's defaults, and why:

  - Include order. The toolset puts the kit's legacy km\crt CRT headers first;
    with them, <stdint.h> cannot be included at all (crtdefs.h and the
    compiler's vcruntime.h redefine each other's macros, C4005/C4083). The
    public zxc headers need <stdint.h>. So the standard path is ignored and
    the compiler's headers, the UCRT, km and shared are named in that order.
  - /std:c17: the core's C standard. The toolset's CL rule drops the
    LanguageStandard_C property, hence AdditionalOptions. /Zc:preprocessor-:
    the kit's wdm.h ARM64 macros fail the conforming preprocessor (C5103).
  - ZXC_ONLY_DEFAULT, not ZXC_DISABLE_SIMD: baseline SSE2 (x64) and NEON
    (ARM64) are legal in kernel mode; the CPUID probes compile out.
  - Win32: /arch:IA32. x86 kernel mode does not preserve XMM state for
    drivers, and /kernel alone leaves the 32-bit compiler on SSE2.
  - /Z7 so the library carries its own debug info, /Zl so no default-library
    directive leaks into the objects, /O2 because this is a codec.
-->
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <ZxcRoot>$(MSBuildThisFileDirectory)..\..\</ZxcRoot>
    <ZxcStage>$(MSBuildThisFileDirectory)build\stage\</ZxcStage>
    <OutDir>$(MSBuildThisFileDirectory)build\$(Platform)\$(Configuration)\</OutDir>
    <IntDir>$(MSBuildThisFileDirectory)build\$(Platform)\$(Configuration)\$(ProjectName)\</IntDir>
  </PropertyGroup>

  <ItemDefinitionGroup>
    <ClCompile>
      <IgnoreStandardIncludePath>true</IgnoreStandardIncludePath>
      <AdditionalIncludeDirectories>$(ZxcStage)include;$(VC_IncludePath);$(UniversalCRT_IncludePath);$(KM_IncludePath);$(KIT_SHARED_IncludePath);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <AdditionalOptions>/std:c17 /Zc:preprocessor- %(AdditionalOptions)</AdditionalOptions>
      <PreprocessorDefinitions>ZXC_STATIC_DEFINE;ZXC_ONLY_DEFAULT;ZXC_FUNCTION_SUFFIX=_default;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <DisableSpecificWarnings>4244;4310;%(DisableSpecificWarnings)</DisableSpecificWarnings>
      <DebugInformationFormat>OldStyle</DebugInformationFormat>
      <OmitDefaultLibName>true</OmitDefaultLibName>
    </ClCompile>
  </ItemDefinitionGroup>

  <ItemDefinitionGroup Condition="'$(Configuration)' == 'Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <FavorSizeOrSpeed>Speed</FavorSizeOrSpeed>
    </ClCompile>
  </ItemDefinitionGroup>

  <ItemDefinitionGroup Condition="'$(Platform)' == 'Win32'">
    <ClCompile>
      <EnableEnhancedInstructionSet>NoExtensions</EnableEnhancedInstructionSet>
    </ClCompile>
  </ItemDefinitionGroup>

  <!-- Plan B needs the 22000 kit's kernel headers; say so instead of failing on
       a missing include. -->
  <Target Name="ZxcCheckKit" BeforeTargets="ClCompile" Condition="'$(ZxcPlanB)' == 'true'">
    <Error Condition="!Exists('$(KM_IncludePath)\ntddk.h')"
           Text="zxc_km plan B (TargetVersion=Windows7 or Platform=Win32) needs the 22000 kit: '$(KM_IncludePath)\ntddk.h' not found. Install WDK 10.0.22000 with its SDK, or build from the 22000 EWDK." />
  </Target>
</Project>
```

- [ ] **Step 4: Write the library project**

Create `contrib/windows-kernel/zxc_km.vcxproj`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- ZXC - High-performance lossless compression -->
<!-- Copyright (c) Bertrand Lebonnois and contributors. -->
<!--
  zxc_km.lib: the seven core units compiled in kernel mode. The ZxcStage
  target copies them, the internal headers, the vendored hash and the public
  headers into build\stage\, with contrib/windows-kernel/zxc_deps.h in place
  of src/lib/zxc_deps.h. The copy is the point: zxc_internal.h includes the
  dependency header with quotes, so the file next to it always wins.
-->
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Debug|ARM64"><Configuration>Debug</Configuration><Platform>ARM64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|ARM64"><Configuration>Release</Configuration><Platform>ARM64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Debug|Win32"><Configuration>Debug</Configuration><Platform>Win32</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|Win32"><Configuration>Release</Configuration><Platform>Win32</Platform></ProjectConfiguration>
  </ItemGroup>

  <PropertyGroup Label="Globals">
    <ProjectGuid>{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}</ProjectGuid>
    <RootNamespace>zxc_km</RootNamespace>
    <Configuration Condition="'$(Configuration)' == ''">Release</Configuration>
    <Platform Condition="'$(Platform)' == ''">x64</Platform>
  </PropertyGroup>

  <Import Project="zxc_km_target.props" />
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <PropertyGroup Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <UseDebugLibraries Condition="'$(Configuration)' == 'Debug'">true</UseDebugLibraries>
    <UseDebugLibraries Condition="'$(Configuration)' == 'Release'">false</UseDebugLibraries>
    <TargetName>zxc_km</TargetName>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <Import Project="zxc_km.props" />

  <ItemDefinitionGroup>
    <ClCompile>
      <AdditionalIncludeDirectories>$(ZxcStage)src\lib;$(ZxcStage)src\lib\vendors;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>

  <!-- What ZxcStage copies. The stock dependency header is excluded; ours goes
       in its place. -->
  <ItemGroup>
    <ZxcPublicHeader Include="$(ZxcRoot)include\*.h" />
    <ZxcLibHeader Include="$(ZxcRoot)src\lib\*.h" Exclude="$(ZxcRoot)src\lib\zxc_deps.h" />
    <ZxcVendor Include="$(ZxcRoot)src\lib\vendors\*" />
    <ZxcUnit Include="$(ZxcRoot)src\lib\zxc_common.c;$(ZxcRoot)src\lib\zxc_pivco_tables.c;$(ZxcRoot)src\lib\zxc_dispatch.c;$(ZxcRoot)src\lib\zxc_dict.c;$(ZxcRoot)src\lib\zxc_compress.c;$(ZxcRoot)src\lib\zxc_decompress.c;$(ZxcRoot)src\lib\zxc_huffman.c" />
    <ZxcKernelDeps Include="$(MSBuildThisFileDirectory)zxc_deps.h" />
  </ItemGroup>

  <Target Name="ZxcStage" BeforeTargets="ClCompile">
    <Copy SourceFiles="@(ZxcPublicHeader)" DestinationFolder="$(ZxcStage)include" SkipUnchangedFiles="true" />
    <Copy SourceFiles="@(ZxcLibHeader);@(ZxcUnit);@(ZxcKernelDeps)" DestinationFolder="$(ZxcStage)src\lib" SkipUnchangedFiles="true" />
    <Copy SourceFiles="@(ZxcVendor)" DestinationFolder="$(ZxcStage)src\lib\vendors" SkipUnchangedFiles="true" />
  </Target>

  <ItemGroup>
    <ClCompile Include="$(ZxcStage)src\lib\zxc_common.c" />
    <ClCompile Include="$(ZxcStage)src\lib\zxc_pivco_tables.c" />
    <ClCompile Include="$(ZxcStage)src\lib\zxc_dispatch.c" />
    <ClCompile Include="$(ZxcStage)src\lib\zxc_dict.c" />
    <ClCompile Include="$(ZxcStage)src\lib\zxc_compress.c" />
    <ClCompile Include="$(ZxcStage)src\lib\zxc_decompress.c" />
    <ClCompile Include="$(ZxcStage)src\lib\zxc_huffman.c" />
  </ItemGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
```

- [ ] **Step 5: Write the ignore file**

Create `contrib/windows-kernel/.gitignore`:

```
# SPDX-License-Identifier: BSD-3-Clause
build/
```

- [ ] **Step 6: Build the library for x64 Release on the 26100 EWDK, expecting success**

```powershell
$e = (Mount-DiskImage -ImagePath "L:\iso\dev\EWDK_ge_release_svc_prod1_26100_250904-1728.iso" -PassThru | Get-Volume).DriveLetter
cmd /c "call ${e}:\BuildEnv\SetupBuildEnv.cmd amd64 >nul 2>&1 && msbuild contrib\windows-kernel\zxc_km.vcxproj /nologo /m /p:Configuration=Release /p:Platform=x64"
```

Expected: `Build succeeded.`, no warnings from `cl` or `lib`, and `contrib\windows-kernel\build\x64\Release\zxc_km.lib` exists. If MSBuild rejects `StaticLibrary` under the kernel toolset (an error naming `ConfigurationType`), apply the spec's fallback: set `ConfigurationType` to `Utility`, and add after the `ClCompile` item group a target `ZxcLib` with `AfterTargets="ClCompile"` that runs `<Lib Sources="@(ClCompile->'$(IntDir)%(Filename).obj')" OutputFile="$(OutDir)zxc_km.lib" />`; then rebuild.

- [ ] **Step 7: Check the library's externals and instruction set**

```powershell
$dumpbin = "${e}:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
$lib = "contrib\windows-kernel\build\x64\Release\zxc_km.lib"
$undef = & $dumpbin /nologo /SYMBOLS $lib | Select-String ' UNDEF ' |
    ForEach-Object { (($_.Line -split '\|')[1].Trim() -split '\s')[0] } |
    Where-Object { $_ -notmatch '^zxc_' } | Sort-Object -Unique
$undef
$ymm = (& $dumpbin /nologo /DISASM:NOBYTES $lib | Select-String '\b[yz]mm\d' | Measure-Object).Count
"AVX registers: $ymm"
```

Expected externals, exactly: `__chkstk`, `__GSHandlerCheck`, `__guard_dispatch_icall_fptr`, `__ImageBase`, `__imp_ExAllocatePool2`, `__imp_ExFreePoolWithTag`, `__isa_available`, `__security_check_cookie`, `__security_cookie`, `memcmp`, `memcpy`, `memset`. Expected `AVX registers: 0`. Anything else in the list means a libc or CRT reference leaked through the dependency header.

- [ ] **Step 8: Check formatting of the header**

```powershell
clang-format --style=file --dry-run --Werror contrib\windows-kernel\zxc_deps.h
```

Expected: no output, exit code 0. If it reports a diff, run `clang-format --style=file -i contrib\windows-kernel\zxc_deps.h` and re-run the check.

- [ ] **Step 9: Commit**

```powershell
git add contrib/windows-kernel/zxc_deps.h contrib/windows-kernel/zxc_km_target.props contrib/windows-kernel/zxc_km.props contrib/windows-kernel/zxc_km.vcxproj contrib/windows-kernel/.gitignore
git commit -m "feat(contrib): kernel-mode static library project for Windows (zxc_km.lib)"
```

---

### Task 2: Self-test driver and the image check script

**Files:**
- Create: `contrib/windows-kernel/selftest.c`
- Create: `contrib/windows-kernel/zxc_selftest.vcxproj`
- Create: `contrib/windows-kernel/check-image.ps1`

**Interfaces:**
- Consumes: `zxc_km.vcxproj` (GUID `{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}`), `zxc_km_target.props`, `zxc_km.props`, the staged `build\stage\include`.
- Produces: `contrib/windows-kernel/build/<Platform>/<Configuration>/zxc_selftest.sys` and `.pdb`; `check-image.ps1 -Image <path> -Machine x64|ARM64|x86 -SubsystemVersion <"10.00"|"6.01"> -Dumpbin <path>`, exit 0 when the image imports only `ntoskrnl.exe`, is Native with the given machine and subsystem version, and (x86) uses no XMM register; CI and Task 3 call it.

- [ ] **Step 1: Write the self-test source**

Create `contrib/windows-kernel/selftest.c`:

```c
// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/*
 * The reference consumer of zxc_km.lib: a legacy software driver whose
 * DriverEntry round-trips one block, one frame and one in-place frame on a
 * static context, at a fast and a dense level, like the Linux self-test
 * module. A mismatch fails the load.
 *
 * Load it on a test-signed machine: sc create zxc_selftest type= kernel
 * binPath= <path>\zxc_selftest.sys, then sc start zxc_selftest. One line per
 * level reaches the kernel debugger or DebugView.
 */

// sdkddkver.h gives NTDDI_VERSION its default when the build left it unset.
#include <sdkddkver.h>

// Plan B (below Windows 10 2004): opt into NX non-paged pool at run time; the
// kernel honours it from Windows 8. The library's allocations follow this
// setting too, through the kit's shared ExDefaultNonPagedPoolType.
#if NTDDI_VERSION < NTDDI_WIN10_VB
#define POOL_NX_OPTIN 1
#endif

#include <ntddk.h>
#include <string.h>

#include "zxc.h"

#define ZXC_SELFTEST_TAG ((ULONG)'tsxz')

// The dense level. Compressing at levels 6 and 7 peaks past 12 KiB of stack,
// which is the whole x86 kernel stack; levels 1 to 5 stay under 6 KiB.
#if defined(_M_IX86)
#define ZXC_SELFTEST_DENSE 5
#else
#define ZXC_SELFTEST_DENSE 7
#endif
static_assert(ZXC_SELFTEST_DENSE <= 5 || sizeof(void*) == 8,
              "levels 6 and 7 do not fit the 12 KB x86 kernel stack");

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD zxc_selftest_unload;

static void* st_alloc(const size_t n) {
#if NTDDI_VERSION >= NTDDI_WIN10_VB
    return ExAllocatePool2(POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED, n, ZXC_SELFTEST_TAG);
#else
    return ExAllocatePoolUninitialized(NonPagedPool, n, ZXC_SELFTEST_TAG);
#endif
}

static void st_free(void* p) {
    if (p) ExFreePoolWithTag(p, ZXC_SELFTEST_TAG);
}

_Use_decl_annotations_ void zxc_selftest_unload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
}

_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    static const char msg[] = "zxc kernel smoke test, zxc kernel smoke test, zxc kernel";
    static const int levels[] = {3, ZXC_SELFTEST_DENSE};
    size_t cap, ws_sz;
    uint8_t *comp, *back;
    void* ws;
    zxc_dctx* sdctx;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;
    int i;

    UNREFERENCED_PARAMETER(RegistryPath);
#if POOL_NX_OPTIN
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);  // before the first allocation
#endif
    DriverObject->DriverUnload = zxc_selftest_unload;

    cap = (size_t)zxc_compress_bound(sizeof(msg));
    ws_sz = zxc_static_dctx_workspace_size(PAGE_SIZE);
    comp = st_alloc(cap);
    back = st_alloc(sizeof(msg));
    ws = st_alloc(ws_sz);
    sdctx = ws ? zxc_init_static_dctx(ws, ws_sz, PAGE_SIZE) : NULL;

    if (!comp || !back || !sdctx) goto out;
    for (i = 0; i < 2; i++) {
        zxc_compress_opts_t opts = {.level = levels[i]};
        zxc_cctx* cctx = zxc_create_cctx(&opts);
        zxc_dctx* dctx = zxc_create_dctx();
        int64_t n = -1, m = -1, fn = -1, fm = -1, pn = -1, pm = -1;

        status = STATUS_INSUFFICIENT_RESOURCES;
        if (cctx && dctx) {
            n = zxc_compress_block(cctx, msg, sizeof(msg), comp, cap, &opts);
            if (n > 0) m = zxc_decompress_block_safe(dctx, comp, (size_t)n, back, sizeof(msg), NULL);
            status = (m == sizeof(msg) && !memcmp(back, msg, sizeof(msg))) ? STATUS_SUCCESS
                                                                            : STATUS_DATA_ERROR;
            memset(back, 0, sizeof(msg));
            fn = zxc_compress(msg, sizeof(msg), comp, cap, &opts);
            if (fn > 0) fm = zxc_decompress(comp, (size_t)fn, back, sizeof(msg), NULL);
            if (NT_SUCCESS(status) && (fm != sizeof(msg) || memcmp(back, msg, sizeof(msg))))
                status = STATUS_DATA_ERROR;

            // In place: the archive flush-right in one buffer, no allocation inside.
            zxc_compress_opts_t popts = {.level = levels[i], .block_size = PAGE_SIZE};
            uint8_t* buf = NULL;
            size_t need = 0;
            pn = zxc_compress(msg, sizeof(msg), comp, cap, &popts);
            if (pn > 0) need = zxc_decompress_inplace_bound(comp, (size_t)pn);
            if (need) buf = st_alloc(need);
            if (buf) {
                memcpy(buf + need - (size_t)pn, comp, (size_t)pn);
                pm = zxc_decompress_inplace_dctx(sdctx, buf, need, (size_t)pn, NULL);
            }
            if (NT_SUCCESS(status) && (pm != sizeof(msg) || !buf || memcmp(buf, msg, sizeof(msg))))
                status = STATUS_DATA_ERROR;
            st_free(buf);
        }
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "zxc_selftest: level %d block %I64d -> %I64d, frame %I64d -> %I64d, "
                   "in place %I64d -> %I64d: %s\n",
                   levels[i], n, m, fn, fm, pn, pm, NT_SUCCESS(status) ? "ok" : "FAIL");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        if (!NT_SUCCESS(status)) break;
    }
out:
    st_free(comp);
    st_free(back);
    st_free(ws);
    return status;
}
```

- [ ] **Step 2: Write the driver project**

Create `contrib/windows-kernel/zxc_selftest.vcxproj`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- SPDX-License-Identifier: BSD-3-Clause -->
<!-- ZXC - High-performance lossless compression -->
<!-- Copyright (c) Bertrand Lebonnois and contributors. -->
<!--
  zxc_selftest.sys: a WDM driver that links zxc_km.lib and round-trips data in
  DriverEntry. No INF: it loads as a legacy software driver (sc create
  type= kernel). Signing is off; pass /p:SignMode=TestSign for a loadable
  binary on a test-signed machine.
-->
<Project DefaultTargets="Build" ToolsVersion="17.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Debug|ARM64"><Configuration>Debug</Configuration><Platform>ARM64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|ARM64"><Configuration>Release</Configuration><Platform>ARM64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Debug|Win32"><Configuration>Debug</Configuration><Platform>Win32</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|Win32"><Configuration>Release</Configuration><Platform>Win32</Platform></ProjectConfiguration>
  </ItemGroup>

  <PropertyGroup Label="Globals">
    <ProjectGuid>{A1B2C3D4-E5F6-4A7B-8C9D-0E1F2A3B4C5D}</ProjectGuid>
    <RootNamespace>zxc_selftest</RootNamespace>
    <DriverType>WDM</DriverType>
    <Configuration Condition="'$(Configuration)' == ''">Release</Configuration>
    <Platform Condition="'$(Platform)' == ''">x64</Platform>
  </PropertyGroup>

  <Import Project="zxc_km_target.props" />
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />

  <PropertyGroup Label="Configuration">
    <ConfigurationType>Driver</ConfigurationType>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <UseDebugLibraries Condition="'$(Configuration)' == 'Debug'">true</UseDebugLibraries>
    <UseDebugLibraries Condition="'$(Configuration)' == 'Release'">false</UseDebugLibraries>
    <TargetName>zxc_selftest</TargetName>
    <SignMode Condition="'$(SignMode)' == ''">Off</SignMode>
    <RunCodeAnalysis>false</RunCodeAnalysis>
  </PropertyGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <Import Project="zxc_km.props" />

  <ItemGroup>
    <ClCompile Include="selftest.c" />
  </ItemGroup>

  <ItemGroup>
    <ProjectReference Include="zxc_km.vcxproj">
      <Project>{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}</Project>
    </ProjectReference>
  </ItemGroup>

  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
```

- [ ] **Step 3: Write the image check script**

Create `contrib/windows-kernel/check-image.ps1`:

```powershell
# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.

<#
.SYNOPSIS
What a built zxc_selftest.sys must look like; CI's equivalent of modinfo.

.DESCRIPTION
Asserts that the image imports ntoskrnl.exe and nothing else, is a
Native-subsystem image of the expected machine and subsystem version, and,
for x86, carries no SSE instruction (x86 kernel mode does not preserve XMM
state for drivers). Exit code 0 when every assertion holds.

.EXAMPLE
.\check-image.ps1 -Image build\x64\Release\zxc_selftest.sys -Machine x64 -SubsystemVersion 10.00
#>
param(
    [Parameter(Mandatory)][string]$Image,
    [Parameter(Mandatory)][ValidateSet("x64", "ARM64", "x86")][string]$Machine,
    [Parameter(Mandatory)][string]$SubsystemVersion,
    [string]$Dumpbin = "dumpbin.exe"
)
$ErrorActionPreference = "Stop"
$fail = @()

$imports = & $Dumpbin /nologo /IMPORTS $Image |
    Select-String -Pattern '^\s{4}(\S+\.(exe|dll|sys))\s*$' |
    ForEach-Object { $_.Matches[0].Groups[1].Value }
if (($imports -join ',') -ne 'ntoskrnl.exe') {
    $fail += "imports '$($imports -join ', ')', expected ntoskrnl.exe only"
}

$headers = & $Dumpbin /nologo /HEADERS $Image
$machineLine = @{ x64 = '8664 machine (x64)'; ARM64 = 'AA64 machine (ARM64)'; x86 = '14C machine (x86)' }[$Machine]
if (-not ($headers -match [regex]::Escape($machineLine))) { $fail += "machine is not $Machine" }
if (-not ($headers -match "^\s*$([regex]::Escape($SubsystemVersion)) subsystem version")) {
    $fail += "subsystem version is not $SubsystemVersion"
}
if (-not ($headers -match '^\s*1 subsystem \(Native\)')) { $fail += "subsystem is not Native" }

if ($Machine -eq "x86") {
    $xmm = (& $Dumpbin /nologo /DISASM:NOBYTES $Image | Select-String -Pattern '\bxmm\d' | Measure-Object).Count
    if ($xmm) { $fail += "x86 image uses XMM registers in $xmm instructions; build with /arch:IA32" }
}

if ($fail) {
    $fail | ForEach-Object { Write-Error -ErrorAction Continue "check-image: $_" }
    exit 1
}
"check-image: ok, $Image imports ntoskrnl.exe only, $Machine, Native $SubsystemVersion"
```

- [ ] **Step 4: Build the driver for x64 and ARM64 on the 26100 EWDK, expecting success**

```powershell
foreach ($p in "x64", "ARM64") {
  cmd /c "call ${e}:\BuildEnv\SetupBuildEnv.cmd amd64 >nul 2>&1 && msbuild contrib\windows-kernel\zxc_selftest.vcxproj /nologo /m /p:Configuration=Release /p:Platform=$p"
}
```

Expected: `Build succeeded.` twice, the library project built first through the reference, and `contrib\windows-kernel\build\{x64,ARM64}\Release\zxc_selftest.sys` present. The 22000 toolset built an INF-less WDM project through link in the spike, and 26100 is expected to as well. If this toolset nevertheless demands an INF (an error from `stampinf` or `Inf2Cat` naming a missing `.inf`), set `<DisableInfVerif>true</DisableInfVerif>` and `<ApiValidator_Enable>false</ApiValidator_Enable>` in the Configuration group and rebuild.

- [ ] **Step 5: Run the image check on both images, expecting success**

```powershell
.\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\x64\Release\zxc_selftest.sys -Machine x64 -SubsystemVersion 10.00 -Dumpbin $dumpbin
.\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\ARM64\Release\zxc_selftest.sys -Machine ARM64 -SubsystemVersion 10.00 -Dumpbin $dumpbin
```

Expected: two `check-image: ok` lines. Then prove the script fails on a wrong expectation, so a green CI means something:

```powershell
.\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\x64\Release\zxc_selftest.sys -Machine ARM64 -SubsystemVersion 10.00 -Dumpbin $dumpbin; "exit $LASTEXITCODE"
```

Expected: `check-image: machine is not ARM64` and `exit 1`.

- [ ] **Step 6: Check formatting**

```powershell
clang-format --style=file --dry-run --Werror contrib\windows-kernel\selftest.c
```

Expected: no output, exit code 0. Otherwise `clang-format --style=file -i` the file and re-check.

- [ ] **Step 7: Commit**

```powershell
git add contrib/windows-kernel/selftest.c contrib/windows-kernel/zxc_selftest.vcxproj contrib/windows-kernel/check-image.ps1
git commit -m "feat(contrib): self-test driver linking zxc_km.lib, and the image check"
```

---

### Task 3: Plan B on the 22000 kit: Windows 7 target and 32-bit x86

**Files:**
- Modify (only if a step below says so): `contrib/windows-kernel/zxc_km_target.props`, `contrib/windows-kernel/zxc_km.props`
- Test: builds with the 22000 EWDK; `check-image.ps1`

**Interfaces:**
- Consumes: everything from Tasks 1 and 2; property `ZxcPlanB`; `check-image.ps1`.
- Produces: `contrib/windows-kernel/build/{x64,Win32}/Release/zxc_selftest.sys` and `zxc_km.lib` for the Windows 7 target; the two expected-failure behaviours documented in the README (Task 4).

- [ ] **Step 1: Build x64 and Win32 at the Windows 7 target on the 22000 EWDK, expecting success**

```powershell
$h = (Mount-DiskImage -ImagePath "L:\iso\dev\EWDK_co_release_22000_210604-1628.iso" -PassThru | Get-Volume).DriveLetter
foreach ($p in "x64", "Win32") {
  cmd /c "call ${h}:\BuildEnv\SetupBuildEnv.cmd amd64 >nul 2>&1 && msbuild contrib\windows-kernel\zxc_selftest.vcxproj /nologo /m /p:Configuration=Release /p:Platform=$p /p:TargetVersion=Windows7"
}
```

Expected: `Build succeeded.` twice. The x64 output overwrites Task 2's x64 output, which is fine: `build\x64\Release` holds whatever target was built last, and the README says so. If `cl` reports C4242 on Win32, the pragma in `zxc_deps.h` is not reaching the units: check that `ZxcStage` copied the header (compare `build\stage\src\lib\zxc_deps.h` with `contrib\windows-kernel\zxc_deps.h`). If `cl` reports C4996 on `ExAllocatePoolWithTag`, a direct call slipped in somewhere: both the header and the self-test must call `ExAllocatePoolUninitialized` below Windows 10 2004.

- [ ] **Step 2: Check both images**

```powershell
$dumpbin7 = "${h}:\Program Files\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.28.29910\bin\Hostx64\x64\dumpbin.exe"
.\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\x64\Release\zxc_selftest.sys -Machine x64 -SubsystemVersion 6.01 -Dumpbin $dumpbin7
.\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\Win32\Release\zxc_selftest.sys -Machine x86 -SubsystemVersion 6.01 -Dumpbin $dumpbin7
```

Expected: two `check-image: ok` lines; the x86 one implies zero XMM instructions in the whole image.

- [ ] **Step 3: Check the plan B allocation path compiled in (Review Focus 1)**

```powershell
$lib7 = "contrib\windows-kernel\build\x64\Release\zxc_km.lib"
$syms = & $dumpbin7 /nologo /SYMBOLS $lib7 | Select-String 'ExDefaultNonPagedPoolType|__imp_ExAllocatePoolWithTag|__imp_ExAllocatePool2' | ForEach-Object { (($_.Line -split '\|')[1].Trim() -split '\s')[0] } | Sort-Object -Unique
$syms
```

Expected: `ExDefaultNonPagedPoolType` and `__imp_ExAllocatePoolWithTag` present, `__imp_ExAllocatePool2` absent. The first is the run-time selected pool type the opt-in call sets; its presence proves the library's allocations follow the consumer's `ExInitializeDriverRuntime(DrvRtPoolNxOptIn)`. Then rebuild the default x64 target on the 26100 EWDK (Task 2 Step 4's first command) and repeat the symbol check on it: expected `__imp_ExAllocatePool2` present and the other two absent.

- [ ] **Step 4: Expected failure: plan B on ARM64 (Review Focus 5)**

```powershell
cmd /c "call ${h}:\BuildEnv\SetupBuildEnv.cmd amd64 >nul 2>&1 && msbuild contrib\windows-kernel\zxc_selftest.vcxproj /nologo /p:Configuration=Release /p:Platform=ARM64 /p:TargetVersion=Windows7"; "exit $LASTEXITCODE"
```

Expected: a non-zero exit with an error that names the target version or the platform (the toolset's own message, or a kit lookup failure). If instead the build succeeds, add to `zxc_km_target.props`, inside the property group:

```xml
<ZxcPlanBUnsupported Condition="'$(TargetVersion)' == 'Windows7' and '$(Platform)' == 'ARM64'">true</ZxcPlanBUnsupported>
```

and to `zxc_km.props`, inside the `ZxcCheckKit` target, a second error:

```xml
<Error Condition="'$(ZxcPlanBUnsupported)' == 'true'" Text="zxc_km plan B: Windows 7 has no ARM64 edition; build ARM64 at the default target." />
```

changing that target's `Condition` to `'$(ZxcPlanB)' == 'true' or '$(ZxcPlanBUnsupported)' == 'true'`, then re-run and expect that message.

- [ ] **Step 5: Expected failure: plan B without the 22000 kit (Review Focus 5)**

On the 26100 EWDK, which has no 22000 kit:

```powershell
cmd /c "call ${e}:\BuildEnv\SetupBuildEnv.cmd amd64 >nul 2>&1 && msbuild contrib\windows-kernel\zxc_selftest.vcxproj /nologo /p:Configuration=Release /p:Platform=x64 /p:TargetVersion=Windows7"; "exit $LASTEXITCODE"
```

Expected: a non-zero exit, and the first error is one of: the `ZxcCheckKit` message naming the missing `ntddk.h`, the toolset's `'Windows7' is not a supported OS Version`, or MSBuild's `MSB8036` about Windows SDK version 10.0.22000.0 not found. All three say what is wrong; none produces a binary. Record which one appears in the README's plan B section (Task 4).

- [ ] **Step 6: Commit any props change, dismount**

```powershell
git add contrib/windows-kernel/zxc_km_target.props contrib/windows-kernel/zxc_km.props
git commit -m "feat(contrib): guard plan B builds of the Windows kernel library"   # only if Step 4 changed a file
Dismount-DiskImage -ImagePath "L:\iso\dev\EWDK_co_release_22000_210604-1628.iso"
```

---

### Task 4: README and documentation pointers

**Files:**
- Create: `contrib/windows-kernel/README.md`
- Modify: `docs/INSTALL.md:6`
- Modify: `docs/API.md:78`

**Interfaces:**
- Consumes: the behaviours established in Tasks 1 to 3, the workspace table from `contrib/linux-kernel/README.md`.
- Produces: the consumer contract in prose; the two doc pointers.

- [ ] **Step 1: Write the README**

Create `contrib/windows-kernel/README.md`:

````markdown
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
so there is no companion PDB to ship.

The default target is Windows 10 2004 (`_NT_TARGET_VERSION` `0xA000008`), the
first Windows with `ExAllocatePool2`. The build uses whichever kit the
environment offers; the 26100 kit is what CI uses. Add `/p:TargetVersion=Windows7`
for plan B, below.

## Consuming the library

Link `zxc_km.lib` and include `include\` from the ZXC tree (or the staged
copy under `build\stage\include`). Three settings in the consumer project
matter, and `zxc_selftest.vcxproj` is the worked example:

```xml
<ItemGroup>
  <ProjectReference Include="path\to\contrib\windows-kernel\zxc_km.vcxproj">
    <Project>{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}</Project>
  </ProjectReference>
</ItemGroup>
<ItemDefinitionGroup>
  <ClCompile>
    <IgnoreStandardIncludePath>true</IgnoreStandardIncludePath>
    <AdditionalIncludeDirectories>path\to\contrib\windows-kernel\build\stage\include;$(VC_IncludePath);$(UniversalCRT_IncludePath);$(KM_IncludePath);$(KIT_SHARED_IncludePath);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
  </ClCompile>
</ItemDefinitionGroup>
```

The include order is the rule that matters (see below). The official toolset
supplies every other link input; a hand-rolled link also needs
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
the stock one.

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

One library built this way runs on every Windows from 7 up. What changes:

- **The kit.** Kits newer than 22000 refuse both a Windows 7 target and the
  Win32 platform in their MSBuild targets, though their compilers still
  build the code. Plan B therefore builds with the 22000 kit (`WindowsTargetPlatformVersion`
  `10.0.22000.0`, set by `zxc_km_target.props`), whose compiler is MSVC 14.28
  from Visual Studio 2019 16.9, the first toolset with `/std:c17`. The
  22000 EWDK is self-contained; or install WDK 10.0.22000 with its SDK next
  to a newer kit. Without it the build stops with a message naming the
  missing kit.
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
- ARM64 has no Windows 7 edition; plan B is x64 and Win32 only.

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
`sc stop zxc_selftest` and `sc delete
zxc_selftest`. Run it under Driver Verifier at least once:

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
````

- [ ] **Step 2: Point the installation and API docs at the tree**

In `docs/INSTALL.md`, replace line 6:

```markdown
Vendoring into a libc-less target (a kernel module, a firmware) has its own tree: [contrib/linux-kernel](../contrib/linux-kernel/README.md).
```

with:

```markdown
Vendoring into a libc-less target (a kernel module, a firmware) has its own tree: [contrib/linux-kernel](../contrib/linux-kernel/README.md). A Windows driver links the kernel-mode library from [contrib/windows-kernel](../contrib/windows-kernel/README.md).
```

In `docs/API.md`, replace line 78:

```markdown
Porting to a libc-less target: [contrib/linux-kernel](../contrib/linux-kernel/README.md).
```

with:

```markdown
Porting to a libc-less target: [contrib/linux-kernel](../contrib/linux-kernel/README.md); Windows drivers: [contrib/windows-kernel](../contrib/windows-kernel/README.md).
```

- [ ] **Step 3: Check the README against what was built**

Open the README and verify each claim against Task 3's results: the exact plan B failure message observed in Task 3 Step 5 goes into the plan B section's "Without it the build stops with a message naming the missing kit" sentence if the observed message differed (for example, name `MSB8036` when that was the first error). Verify every relative link resolves: `../contrib/linux-kernel/README.md` from `docs/`, `../contrib/windows-kernel/README.md` from `docs/`.

- [ ] **Step 4: Commit**

```powershell
git add contrib/windows-kernel/README.md docs/INSTALL.md docs/API.md
git commit -m "docs(contrib): guide for the Windows kernel-mode library"
```

---

### Task 5: CI workflow

**Files:**
- Create: `.github/workflows/windows-kernel.yml`
- Modify: `.github/workflows/README.md` (after the `kernel.yml` entry, lines 70-73)

**Interfaces:**
- Consumes: `contrib/windows-kernel/zxc_selftest.vcxproj`, `check-image.ps1`, the runner image's Visual Studio 2022 and WDK component.
- Produces: the `Windows Kernel Library` workflow with jobs `library` (pull-request path) and `plan-b` (schedule and dispatch).

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/windows-kernel.yml`:

```yaml
name: Windows Kernel Library

# Builds contrib/windows-kernel with the runner's WDK: the kernel-mode static
# library and the self-test driver that links it, then asserts the image's
# imports, machine and subsystem. Like kernel.yml, off the PR path unless the
# tree or the library change. Plan B (the Windows 7 target and x86) needs the
# 22000 kit, which the runner lacks, so a second job installs it and runs on
# the schedule and on dispatch only.

on:
  workflow_dispatch:
  schedule:
    - cron: '0 5 * * 2'
  push:
    branches: [ main ]
    paths:
      - 'contrib/windows-kernel/**'
      - 'include/**'
      - 'src/lib/**'
      - '.github/workflows/windows-kernel.yml'
  pull_request:
    branches: [ main, candidate ]
    paths:
      - 'contrib/windows-kernel/**'
      - 'include/**'
      - 'src/lib/**'
      - '.github/workflows/windows-kernel.yml'

permissions:
  contents: read

concurrency:
  cancel-in-progress: true
  group: ${{ github.workflow }}-${{ github.event.pull_request.number || github.ref }}

jobs:
  library:
    name: "Kernel library (${{ matrix.platform }}, Windows 10 2004)"
    runs-on: windows-2025
    timeout-minutes: 20
    strategy:
      fail-fast: false
      matrix:
        platform: [x64, ARM64]

    steps:
      - name: Checkout Repository
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1

      - name: Locate the toolchain
        id: tools
        shell: pwsh
        run: |
          $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
          $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
          $dumpbin = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
          $km = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\Include\*\km\ntddk.h" -ErrorAction SilentlyContinue | Select-Object -Last 1
          foreach ($pair in @(@("MSBuild", $msbuild), @("dumpbin", $dumpbin), @("kernel headers (km\ntddk.h)", $km))) {
            if (-not $pair[1]) { Write-Host "::error::$($pair[0]) not found on this runner image"; exit 1 }
            "$($pair[0]): $($pair[1])"
          }
          "msbuild=$msbuild" >> $env:GITHUB_OUTPUT
          "dumpbin=$dumpbin" >> $env:GITHUB_OUTPUT

      - name: Build the library and the self-test driver
        shell: pwsh
        run: |
          & "${{ steps.tools.outputs.msbuild }}" contrib\windows-kernel\zxc_selftest.vcxproj /nologo /m /p:Configuration=Release /p:Platform=${{ matrix.platform }} /p:SignMode=Off
          exit $LASTEXITCODE

      - name: Check the image
        shell: pwsh
        run: |
          .\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\${{ matrix.platform }}\Release\zxc_selftest.sys -Machine ${{ matrix.platform }} -SubsystemVersion 10.00 -Dumpbin "${{ steps.tools.outputs.dumpbin }}"

  plan-b:
    name: "Kernel library plan B (${{ matrix.platform }}, Windows 7, kit 22000)"
    if: github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'
    runs-on: windows-2025
    timeout-minutes: 45
    strategy:
      fail-fast: false
      matrix:
        include:
          - platform: x64
            machine: x64
          - platform: Win32
            machine: x86

    steps:
      - name: Checkout Repository
        uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1

      - name: Install the 22000 SDK and WDK
        shell: pwsh
        run: |
          foreach ($id in "Microsoft.WindowsSDK.10.0.22000", "Microsoft.WindowsWDK.10.0.22000") {
            winget install --id $id --exact --silent --disable-interactivity --accept-package-agreements --accept-source-agreements
            if ($LASTEXITCODE) { Write-Host "::error::winget could not install $id (exit $LASTEXITCODE)"; exit 1 }
          }
          if (-not (Test-Path "${env:ProgramFiles(x86)}\Windows Kits\10\Include\10.0.22000.0\km\ntddk.h")) {
            Write-Host "::error::kit 22000 kernel headers missing after the install"; exit 1
          }

      - name: Locate the toolchain
        id: tools
        shell: pwsh
        run: |
          $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
          $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
          $dumpbin = & $vswhere -latest -products * -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe' | Select-Object -First 1
          foreach ($pair in @(@("MSBuild", $msbuild), @("dumpbin", $dumpbin))) {
            if (-not $pair[1]) { Write-Host "::error::$($pair[0]) not found on this runner image"; exit 1 }
            "$($pair[0]): $($pair[1])"
          }
          "msbuild=$msbuild" >> $env:GITHUB_OUTPUT
          "dumpbin=$dumpbin" >> $env:GITHUB_OUTPUT

      - name: Build for the Windows 7 target
        shell: pwsh
        run: |
          & "${{ steps.tools.outputs.msbuild }}" contrib\windows-kernel\zxc_selftest.vcxproj /nologo /m /p:Configuration=Release /p:Platform=${{ matrix.platform }} /p:TargetVersion=Windows7 /p:SignMode=Off
          exit $LASTEXITCODE

      - name: Check the image
        shell: pwsh
        run: |
          .\contrib\windows-kernel\check-image.ps1 -Image contrib\windows-kernel\build\${{ matrix.platform }}\Release\zxc_selftest.sys -Machine ${{ matrix.machine }} -SubsystemVersion 6.01 -Dumpbin "${{ steps.tools.outputs.dumpbin }}"
```

- [ ] **Step 2: Add the workflow to the workflows README**

In `.github/workflows/README.md`, after the `kernel.yml - Kernel Module` entry (the paragraph ending "Off the PR path on purpose."), insert:

```markdown
### windows-kernel.yml - Windows Kernel Library
**Triggers:** Changes to `contrib/windows-kernel/`, `include/` or `src/lib/`; weekly; manual dispatch

Builds [`contrib/windows-kernel`](../../contrib/windows-kernel/README.md) - the dependency header, the property sheets, the kernel-mode static library and the self-test driver that links it - with the runner's WDK for x64 and ARM64 at the Windows 10 2004 target, then asserts with `check-image.ps1` that the image imports `ntoskrnl.exe` only and is a Native image of the right machine and subsystem version. A second job, on the schedule and on dispatch only, installs the 22000 kit and builds plan B: x64 and Win32 at the Windows 7 target. Nothing is loaded; that is a manual step on a test-signed VM, as for the Linux module.
```

- [ ] **Step 3: Validate the YAML parses**

```powershell
python -c "import yaml, sys; yaml.safe_load(open('.github/workflows/windows-kernel.yml')); print('yaml ok')"
```

If `yaml` is missing: `pip install pyyaml`, then re-run. Expected: `yaml ok`.

- [ ] **Step 4: Commit and push, then run the workflow**

```powershell
git add .github/workflows/windows-kernel.yml .github/workflows/README.md
git commit -m "ci: build the Windows kernel-mode library on x64, ARM64 and, on schedule, plan B"
git push -u origin feat/windows-kernel
```

Then open a pull request from `feat/windows-kernel` to `main` (the `pull_request` trigger runs the `library` job), and trigger the workflow by hand on the branch to run `plan-b`:

```powershell
gh workflow run windows-kernel.yml --ref feat/windows-kernel
gh run watch
```

Expected: the `library` job green for both platforms. For `plan-b`, three outcomes, each with a documented next move:

1. Green: done.
2. The install step fails because `winget` is absent or the package IDs do not exist: replace that step with the kit installers from Microsoft's download pages. Look up the Windows 11 21H2 (22000) row on `https://learn.microsoft.com/windows-hardware/drivers/legacy-wdk-downloads`, download its SDK and WDK installers with `Invoke-WebRequest`, and run each with `/features + /quiet /norestart`, keeping the same `ntddk.h` existence check; commit as `ci: install the 22000 kit from the installers for plan B`.
3. The build fails because the 26100 toolset does not pick up a side-by-side 22000 kit (an error naming `WindowsTargetPlatformVersion` or `MSB8036`): plan B stays a documented local build against the 22000 EWDK. Remove the `plan-b` job, say so in the README's CI section and in the workflows README, and commit as `ci: plan B of the Windows kernel library stays a local build`. The spec names this fallback.

---

## Self-review

**Spec coverage.** Tree and all seven files: Tasks 1, 2, 4 (plus `check-image.ps1`, which the spec's CI section implies as "the job's modinfo"). Dependency header, every mapping and guard: Task 1 Step 1. Property sheet contents: Task 1 Steps 2 and 3, split in two with the reason stated. Library project and staging: Task 1 Step 4. Self-test driver and project, runtime opt-in, levels: Task 2. Plan B on the 22000 kit, x86 flags, the two expected failures: Task 3. Consumer contract, IRQL, stack, SIMD, loading, Windows 7 signing note, licensing: Task 4's README. Doc pointers: Task 4 Step 2. CI with two jobs, assertions, triggers, pinned action: Task 5. Items to verify from the spec: StaticLibrary under the toolset (Task 1 Step 6 with fallback), INF-less driver (Task 2 Step 4 with fallback), `/X` with the force-included `warning.h` (exercised by every build), runner kit and ARM64 tools (Task 5's locate step fails loudly), property-sheet metadata survival (every build; the sheet is imported after the toolset's definitions), ARM64 plan B refusal (Task 3 Step 4), `winget` and the side-by-side kit (Task 5 Step 4's outcomes).

**Placeholders.** None: every file is written out; the only "look up" is a URL to a Microsoft page in a fallback branch, with the exact command shape to run afterwards.

**Type consistency.** `check-image.ps1` takes `-Machine x64|ARM64|x86`; CI's plan B matrix maps `Win32` to `x86` through `matrix.machine`; Task 3 passes `x86` by hand. The library GUID is `{6E3C7B1A-2D4F-4C8E-9A0B-5F1D2E3C4B5A}` in the project, the reference and the README. `ZxcStage`, `ZxcRoot`, `ZxcPlanB` are defined once and used by name everywhere else. Output paths are `build\<Platform>\<Configuration>\` in the sheet, the tasks and the workflow.

**Review Focus.** Lines 1, 2, 4 and 5 have the tests named in Tasks 3, 2, 2 and 3. Line 3 (zero-length allocation) is pinned by code in Task 1 and by the README's Verifier instruction; no CI step loads a driver, so no CI test exists for it, by the spec's own scope.
