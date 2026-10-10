# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.

# Top-level convenience Makefile (wraps CMake)
#
# Usage:
#   make              Build the library + CLI (Release)
#   make test         Build and run tests (parallel)
#   make conformance  Build and run only the decoder conformance suite
#   make format       Format source code with clang-format
#   make format-check Check formatting (CI mode)
#   make lint         Scan source files for non-ASCII characters (CI mirror)
#   make man          Regenerate src/cli/zxc.1 from src/cli/zxc.1.md (pandoc)
#   make man-check    Check that src/cli/zxc.1 is up to date (CI mode)
#   make doc          Generate Doxygen documentation
#   make clean        Remove build directory
#
# Override build directory:  make BUILD=mybuild
# Pass extra CMake flags:   make CMAKE_EXTRA="-DZXC_NATIVE_ARCH=OFF"

BUILD       ?= build
CMAKE       ?= cmake
CMAKE_EXTRA ?=
# Older corpus are frozen, so `vectors` must not touch
# their manifests (conformance/README.md).
FORMAT_VERSION := $(shell sed -n 's/^\#define ZXC_FILE_FORMAT_VERSION \([0-9]*\).*/\1/p' src/lib/zxc_internal.h)
JOBS        ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: all test conformance golden vectors format format-check lint man man-check doc clean

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
# Decoder behaviour: the published vectors under conformance/v<N>/
# (valid/ must decode byte-for-byte, invalid/ must be rejected with
# the declared error).
conformance:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DZXC_BUILD_TESTS=ON $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) -j$(JOBS) --target zxc_conformance_test
	@cd $(BUILD) && ctest --output-on-failure -R '^conformance$$'

# ── Golden ───────────────────────────────────────────────────
# Encoder output: every field of the frozen archives under
# tests/format/golden/, their annotated dumps, and the recipes.
golden:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DZXC_BUILD_TESTS=ON $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) -j$(JOBS) --target zxc_format_golden_test
	@cd $(BUILD) && ctest --output-on-failure -R '^format_golden$$'

# ── Vectors (maintainer) ─────────────────────────────────────
# Rewrites committed test data. Only after a deliberate format or
# encoder change; read the dump diff before committing.
SHA256 := $(shell command -v sha256sum 2>/dev/null || echo "shasum -a 256")
vectors:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DZXC_BUILD_TESTS=ON $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) -j$(JOBS) --target zxc_golden_gen zxc_valid_gen \
	    zxc_invalid_gen zxc_format_golden_test
	@./$(BUILD)/zxc_golden_gen tests/format/golden
	@./$(BUILD)/zxc_valid_gen
	@./$(BUILD)/zxc_invalid_gen
	@$(SHA256) tests/format/golden/*.zxc | sort -k2 > tests/format/golden.sha256
	@d=conformance/v$(FORMAT_VERSION); \
	    $(SHA256) $$d/valid/*.zxc $$d/valid/*.expected $$d/valid/*.zxd $$d/invalid/*.zxc \
	      | sort -k2 > $$d/vectors.sha256
	@./$(BUILD)/zxc_format_golden_test --dump tests/format/golden
	@echo "Vectors regenerated. Review the .zxc.txt dump diff before committing."

# ── Formatting ───────────────────────────────────────────────
format:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) --target format

format-check:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) --target format-check

# ── Lint (mirrors .github/workflows/quality.yml) ─────────────
# Scans every .c/.h in the repository, as quality.yml does.
lint:
	@echo "Scanning for non-ASCII characters in .c and .h files..."
	@files=$$(find . -type f \( -name '*.c' -o -name '*.h' \) \
	    -not -path './$(BUILD)/*' -not -path './.git/*' -not -path '*/node_modules/*' \
	    -not -path '*/target/*' -not -path '*/.venv/*' 2>/dev/null); \
	if [ -z "$$files" ]; then echo "No source files found."; exit 0; fi; \
	LC_ALL=C perl -ne \
	  'if (/[^[:ascii:]]/) { print "$$ARGV:$$.:$$_"; $$bad=1 } \
	   END { exit($$bad ? 1 : 0) }' \
	  $$files \
	&& echo "OK: No non-ASCII characters found." \
	|| { echo "ERROR: Non-ASCII characters found in source files."; exit 1; }

# ── Documentation ────────────────────────────────────────────
# ── Man page ─────────────────────────────────────────────────
# src/cli/zxc.1 is committed, so building never needs pandoc.
# CI pins the pandoc version: another one may render differently.
# -smart keeps "--option" as two hyphens instead of an en dash.
PANDOC    ?= pandoc
MAN_FLAGS := --standalone --from markdown-smart --to man --shift-heading-level-by=-1 \
             -M header="User Commands" -M footer=zxc
man:
	@$(PANDOC) $(MAN_FLAGS) src/cli/zxc.1.md -o src/cli/zxc.1

man-check:
	@$(PANDOC) $(MAN_FLAGS) src/cli/zxc.1.md | diff -u src/cli/zxc.1 - \
	    || { echo "src/cli/zxc.1 is stale: run 'make man'"; exit 1; }

doc:
	@$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release $(CMAKE_EXTRA)
	@$(CMAKE) --build $(BUILD) --target doc

# ── Clean ────────────────────────────────────────────────────
clean:
	@rm -rf $(BUILD)
