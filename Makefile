# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause

# Top-level convenience Makefile (wraps CMake)
#
# Usage:
#   make              Build the library + CLI (Release)
#   make test         Build and run tests
#   make format       Format source code with clang-format
#   make format-check Check formatting (CI mode)
#   make doc          Generate Doxygen documentation
#   make clean        Remove build directory
#
# Override build directory:  make BUILD=mybuild

BUILD    ?= build
CMAKE    ?= cmake
JOBS     ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: all test format format-check doc clean

# ── Build ────────────────────────────────────────────────────
all:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	@$(CMAKE) --build $(BUILD) -j$(JOBS)

# ── Test ─────────────────────────────────────────────────────
test:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug -DZXC_BUILD_TESTS=ON
	@$(CMAKE) --build $(BUILD) -j$(JOBS)
	@cd $(BUILD) && ctest --output-on-failure

# ── Formatting ───────────────────────────────────────────────
format:
	@$(CMAKE) -S . -B $(BUILD)
	@$(CMAKE) --build $(BUILD) --target format

format-check:
	@$(CMAKE) -S . -B $(BUILD)
	@$(CMAKE) --build $(BUILD) --target format-check

# ── Documentation ────────────────────────────────────────────
doc:
	@$(CMAKE) -S . -B $(BUILD)
	@$(CMAKE) --build $(BUILD) --target doc

# ── Clean ────────────────────────────────────────────────────
clean:
	@rm -rf $(BUILD)
