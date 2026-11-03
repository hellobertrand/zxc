// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

//go:build unix

package zxc

import (
	"os"
	"strconv"
	"strings"
)

// committedBytes returns the process's data segment (VmData). Resident memory
// would miss a leak: the library reserves buffers it may never touch. Outside
// Linux there is no portable source, so the memory test skips.
func committedBytes() (int64, bool) {
	b, err := os.ReadFile("/proc/self/status")
	if err != nil {
		return 0, false
	}
	for _, line := range strings.Split(string(b), "\n") {
		if f := strings.Fields(line); len(f) >= 2 && f[0] == "VmData:" {
			kb, err := strconv.ParseInt(f[1], 10, 64)
			return kb << 10, err == nil
		}
	}
	return 0, false
}

func openHandles() (int, bool) {
	for _, dir := range []string{"/proc/self/fd", "/dev/fd"} {
		if e, err := os.ReadDir(dir); err == nil {
			return len(e), true
		}
	}
	return 0, false
}
