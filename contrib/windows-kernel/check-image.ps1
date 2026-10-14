# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Jean Gautier and contributors.

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
