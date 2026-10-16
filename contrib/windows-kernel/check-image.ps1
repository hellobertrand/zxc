# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Jean Gautier and contributors.

<#
.SYNOPSIS
What a built zxc_selftest.sys must look like; CI's equivalent of modinfo.

.DESCRIPTION
Asserts that the image imports ntoskrnl.exe and nothing else, is a
Native-subsystem image of the expected machine and subsystem version, and
carries no AVX instruction (no ymm or zmm register in its disassembly):
kernel mode allows the SSE2 and NEON baselines without saving state, never
AVX. Exit code 0 when every assertion holds; a dumpbin failure fails the
check.

.EXAMPLE
.\check-image.ps1 -Image build\x64\Release\zxc_selftest.sys -Machine x64 -SubsystemVersion 10.00
#>
param(
    [Parameter(Mandatory)][string]$Image,
    [Parameter(Mandatory)][ValidateSet("x64", "ARM64")][string]$Machine,
    [Parameter(Mandatory)][string]$SubsystemVersion,
    [string]$Dumpbin = "dumpbin.exe"
)
$ErrorActionPreference = "Stop"
$fail = @()

# A native command's exit code never throws, whatever $ErrorActionPreference
# says: a dumpbin that fails must not read as an empty, passing listing.
function Invoke-Dumpbin([string]$Option) {
    $out = & $Dumpbin /nologo $Option $Image
    if ($LASTEXITCODE) {
        Write-Error -ErrorAction Continue "check-image: dumpbin $Option exited $LASTEXITCODE"
        exit 1
    }
    $out
}

$imports = Invoke-Dumpbin /IMPORTS |
    Select-String -Pattern '^\s{4}(\S+\.(exe|dll|sys))\s*$' |
    ForEach-Object { $_.Matches[0].Groups[1].Value }
if (($imports -join ',') -ne 'ntoskrnl.exe') {
    $fail += "imports '$($imports -join ', ')', expected ntoskrnl.exe only"
}

$headers = Invoke-Dumpbin /HEADERS
$machineLine = @{ x64 = '8664 machine (x64)'; ARM64 = 'AA64 machine (ARM64)' }[$Machine]
if (-not ($headers -match [regex]::Escape($machineLine))) { $fail += "machine is not $Machine" }
if (-not ($headers -match "^\s*$([regex]::Escape($SubsystemVersion)) subsystem version")) {
    $fail += "subsystem version is not $SubsystemVersion"
}
if (-not ($headers -match '^\s*1 subsystem \(Native\)')) { $fail += "subsystem is not Native" }

$avx = (Invoke-Dumpbin /DISASM:NOBYTES | Select-String -Pattern '\b[yz]mm\d' | Measure-Object).Count
if ($avx) { $fail += "image uses ymm/zmm registers (AVX) in $avx instructions" }

if ($fail) {
    $fail | ForEach-Object { Write-Error -ErrorAction Continue "check-image: $_" }
    exit 1
}
"check-image: ok, $Image imports ntoskrnl.exe only, $Machine, Native $SubsystemVersion, no AVX"
