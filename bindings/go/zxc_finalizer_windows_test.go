// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

import (
	"syscall"
	"unsafe"
)

var (
	modPsapi                  = syscall.NewLazyDLL("psapi.dll")
	modKernel32               = syscall.NewLazyDLL("kernel32.dll")
	procGetProcessMemoryInfo  = modPsapi.NewProc("GetProcessMemoryInfo")
	procGetProcessHandleCount = modKernel32.NewProc("GetProcessHandleCount")
)

// processMemoryCountersEx mirrors PROCESS_MEMORY_COUNTERS_EX.
type processMemoryCountersEx struct {
	cb                         uint32
	pageFaultCount             uint32
	peakWorkingSetSize         uintptr
	workingSetSize             uintptr
	quotaPeakPagedPoolUsage    uintptr
	quotaPagedPoolUsage        uintptr
	quotaPeakNonPagedPoolUsage uintptr
	quotaNonPagedPoolUsage     uintptr
	pagefileUsage              uintptr
	peakPagefileUsage          uintptr
	privateUsage               uintptr
}

// committedBytes returns the process's private commit charge.
func committedBytes() (int64, bool) {
	var c processMemoryCountersEx
	c.cb = uint32(unsafe.Sizeof(c))
	h, _ := syscall.GetCurrentProcess()
	r, _, _ := procGetProcessMemoryInfo.Call(uintptr(h), uintptr(unsafe.Pointer(&c)), uintptr(c.cb))
	return int64(c.privateUsage), r != 0
}

func openHandles() (int, bool) {
	var n uint32
	h, _ := syscall.GetCurrentProcess()
	r, _, _ := procGetProcessHandleCount.Call(uintptr(h), uintptr(unsafe.Pointer(&n)))
	return int(n), r != 0
}
