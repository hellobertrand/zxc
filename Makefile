# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause

# Top-level convenience Makefile (wraps CMake)
#
# Usage:
#   make              Build the library + CLI (Release)
#   make test         Build and run tests (parallel)
#   make conformance  Build and run only the decoder conformance suite
#   make format       Format source code with clang-format
#   make format-check Check formatting (CI mode)
#   make lint         Scan source files for non-ASCII characters (CI mirror)
#   make doc          Generate Doxygen documentation
#   make clean        Remove build directory
#
# Override build directory:  make BUILD=mybuild
# Pass extra CMake flags:   make CMAKE_EXTRA="-DZXC_NATIVE_ARCH=OFF"

BUILD       ?= build
CMAKE       ?= cmake
CMAKE_EXTRA ?=
JOBS        ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: all test conformance format format-check lint doc clean

# ── Build ────────────────────────────────────────────────────
all:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) -j$(JOBS)

# ── Test ─────────────────────────────────────────────────────
test:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DZXC_BUILD_TESTS=ON $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) -j$(JOBS)
	@cd $(BUILD) && ctest --output-on-failure -j$(JOBS)

# ── Conformance ──────────────────────────────────────────────
# Runs only the decoder conformance suite: the reference test
# vectors under conformance/ (valid/ must decode byte-for-byte,
# invalid/ must be rejected). Same test as `make test`, filtered.
conformance:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DZXC_BUILD_TESTS=ON $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) -j$(JOBS) --target zxc_conformance_test
	@cd $(BUILD) && ctest --output-on-failure -R '^conformance$$'

# ── Formatting ───────────────────────────────────────────────
format:
	@$(CMAKE) -S . -B $(BUILD)
	@$(CMAKE) --build $(BUILD) --target format

format-check:
	@$(CMAKE) -S . -B $(BUILD)
	@$(CMAKE) --build $(BUILD) --target format-check

# ── Lint (mirrors .github/workflows/quality.yml) ─────────────
# Scans .c and .h files under src/, include/, tests/ for non-ASCII bytes.
# Uses Perl for portability.
lint:
	@echo "Scanning for non-ASCII characters in .c and .h files..."
	@files=$$(find src include tests -type f \( -name '*.c' -o -name '*.h' \) 2>/dev/null); \
	if [ -z "$$files" ]; then echo "No source files found."; exit 0; fi; \
	LC_ALL=C perl -ne \
	  'if (/[^[:ascii:]]/) { print "$$ARGV:$$.:$$_"; $$bad=1 } \
	   END { exit($$bad ? 1 : 0) }' \
	  $$files \
	&& echo "OK: No non-ASCII characters found." \
	|| { echo "ERROR: Non-ASCII characters found in source files."; exit 1; }

# ── Documentation ────────────────────────────────────────────
doc:
	@$(CMAKE) -S . -B $(BUILD)
	@$(CMAKE) --build $(BUILD) --target doc

# ── Clean ────────────────────────────────────────────────────
clean:
	@rm -rf $(BUILD)
