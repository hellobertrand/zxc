#!/bin/bash
# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# Test script for ZXC CLI
# Usage: ./tests/test_cli.sh [path_to_zxc_binary]

set -e

# Default binary path
ZXC_BIN=${1:-"../build/zxc"}

# Test Directory (to isolate test files)
TEST_DIR="./test_tmp"
mkdir -p "$TEST_DIR"

# Test Files
TEST_FILE="$TEST_DIR/testdata"
TEST_FILE_XC="${TEST_FILE}.zxc"
TEST_FILE_DEC="${TEST_FILE}.dec"
PIPE_XC="$TEST_DIR/test_pipe.zxc"
PIPE_DEC="$TEST_DIR/test_pipe.dec"

# Variables for checking file existence
TEST_FILE_XC_BASH="$TEST_FILE_XC"
TEST_FILE_DEC_BASH="${TEST_FILE}.dec"

# Arguments passed to ZXC (relative to test_tmp)
TEST_FILE_ARG="$TEST_FILE"
TEST_FILE_XC_ARG="$TEST_FILE_XC" 

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

log_pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    exit 1
}

# cleanup on exit
cleanup() {
    echo "Cleaning up..."
    rm -rf "$TEST_DIR"
}

trap cleanup EXIT

# 0. Check binary
if [[ ! -f "$ZXC_BIN" ]]; then
    log_fail "Binary not found at $ZXC_BIN. Please build the project first."
fi
echo "Using binary: $ZXC_BIN"

# 1. Generate Test File (Lorem Ipsum)
echo "Generating test file..."
cat > "$TEST_FILE" <<EOF
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.
Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo. Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit qui in ea voluptate velit esse quam nihil molestiae consequatur, vel illum qui dolorem eum fugiat quo voluptas nulla pariatur?
EOF
# Duplicate content
for i in {1..9}; do
    cat "$TEST_FILE" "$TEST_FILE" > "${TEST_FILE}.tmp"
    mv "${TEST_FILE}.tmp" "$TEST_FILE"
done
# Pristine copy: sections that consume or delete TEST_FILE restore it from here
cp "$TEST_FILE" "${TEST_FILE}.orig"

FILE_SIZE=$(wc -c < "$TEST_FILE" | tr -d ' ')
echo "Test file generated: $TEST_FILE ($FILE_SIZE bytes)"

# Helper: Wait for file to be ready and readable
wait_for_file() {
    local file="$1"
    local retries=10
    local count=0
    # On Windows, file locking can cause race conditions immediately after creation.
    while [[ $count -lt $retries ]]; do
        if [[ -f "$file" ]]; then
             # Try to read a byte to ensure it's not exclusively locked
             if head -c 1 "$file" >/dev/null 2>&1; then
                 return 0
             fi
        fi
        sleep 1
        count=$((count + 1))
    done
    echo "Timeout waiting for file '$file' to be readable."
    ls -l "$file" 2>/dev/null || echo "File not found in ls."
    return 1
}

# 2. Basic Round-Trip
echo "Testing Basic Round-Trip..."

if ! "$ZXC_BIN" -z -k "$TEST_FILE_ARG"; then
    log_fail "Compression command failed (exit code $?)"
fi

# Wait for output
if ! wait_for_file "$TEST_FILE_XC_BASH"; then
    log_fail "Compression succeeded but output file '$TEST_FILE_XC_BASH' is not accessible."
fi

# Decompress to stdout
echo "Attempting decompression of: $TEST_FILE_XC_ARG"

if ! "$ZXC_BIN" -d -c "$TEST_FILE_XC_ARG" > "$TEST_FILE_DEC_BASH"; then
     echo "Decompress to stdout failed. Retrying once..."
     sleep 1
     if ! "$ZXC_BIN" -d -c "$TEST_FILE_XC_ARG" > "$TEST_FILE_DEC_BASH"; then
        log_fail "Decompression to stdout failed."
     fi
fi

# Decompress to file
rm -f "$TEST_FILE"
sleep 1
if ! "$ZXC_BIN" -d -k "$TEST_FILE_XC_ARG"; then
     echo "Decompress to file failed. Retrying once..."
     sleep 1
     if ! "$ZXC_BIN" -d -k "$TEST_FILE_XC_ARG"; then
        log_fail "Decompression to file failed."
     fi
fi

if ! wait_for_file "$TEST_FILE"; then
   log_fail "Decompression failed to recreate original file."
fi

mv "$TEST_FILE" "$TEST_FILE_DEC_BASH"

# Restore source from the pristine copy for a valid comparison
cp "${TEST_FILE}.orig" "$TEST_FILE"

if cmp -s "$TEST_FILE" "$TEST_FILE_DEC_BASH"; then
    log_pass "Basic Round-Trip"
else
    log_fail "Basic Round-Trip content mismatch"
fi

# 3. Piping
echo "Testing Piping..."
rm -f "$PIPE_XC" "$PIPE_DEC"
cat "$TEST_FILE" | "$ZXC_BIN" > "$PIPE_XC"
if [[ ! -s "$PIPE_XC" ]]; then
    log_fail "Piping compression failed (empty output)"
fi

cat "$PIPE_XC" | "$ZXC_BIN" -d > "$PIPE_DEC"
if [[ ! -s "$PIPE_DEC" ]]; then
    log_fail "Piping decompression failed (empty output)"
fi

if cmp -s "$TEST_FILE" "$PIPE_DEC"; then
    log_pass "Piping"
else
    log_fail "Piping content mismatch"
fi

# 4. Flags
echo "Testing Flags..."
# Level
"$ZXC_BIN" -1 -k -f "$TEST_FILE_ARG"
if [[ ! -f "$TEST_FILE_XC_BASH" ]]; then log_fail "Level 1 flag failed"; fi
log_pass "Flag -1"

# Force Overwrite (-f)
touch "$TEST_DIR/out.zxc"
touch "${TEST_FILE_XC_BASH}"
set +e
"$ZXC_BIN" -z -k "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -eq 0 ]]; then
     log_fail "Should have failed to overwrite without -f"
else
     log_pass "Overwrite protection verified"
fi

"$ZXC_BIN" -z -k -f "$TEST_FILE_ARG"
if [[ $? -eq 0 ]]; then
   log_pass "Force overwrite (-f)"
else
   log_fail "Force overwrite failed"
fi

# 5. Benchmark
echo "Testing Benchmark..."
"$ZXC_BIN" -b1 "$TEST_FILE_ARG" > /dev/null
if [[ $? -eq 0 ]]; then
    log_pass "Benchmark mode"
else
    log_fail "Benchmark mode failed"
fi

# 6. Error Handling
echo "Testing Error Handling..."
set +e
"$ZXC_BIN" "nonexistent_file" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Missing file error handled"
else
    log_fail "Missing file should return error"
fi

# 7. Version
echo "Testing Version..."
OUT_VER=$("$ZXC_BIN" -V)
if [[ "$OUT_VER" == *"ZXC CLI"* && "$OUT_VER" =~ v[0-9]+\.[0-9]+\.[0-9]+ ]]; then
    log_pass "Version flag"
else
    log_fail "Version flag failed"
fi

# 8. Checksum
echo "Testing Checksum..."
"$ZXC_BIN" -C -k -f "$TEST_FILE_ARG"
if [[ ! -f "$TEST_FILE_XC_BASH" ]]; then log_fail "Checksum compression failed"; fi
rm -f "$TEST_FILE"
"$ZXC_BIN" -d "$TEST_FILE_XC_ARG"
if [[ ! -f "$TEST_FILE" ]]; then log_fail "Checksum decompression failed"; fi
log_pass "Checksum enabled (-C)"

"$ZXC_BIN" -N -k -f "$TEST_FILE_ARG"
if [[ ! -f "$TEST_FILE_XC_BASH" ]]; then log_fail "No-Checksum compression failed"; fi
rm -f "$TEST_FILE"
"$ZXC_BIN" -d "$TEST_FILE_XC_ARG"
if [[ ! -f "$TEST_FILE" ]]; then log_fail "No-Checksum decompression failed"; fi
log_pass "Checksum disabled (-N)"

# 9. Integrity Test (-t)
echo "Testing Integrity Check (-t)..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"

# Valid file should pass and show "OK"
OUT=$("$ZXC_BIN" -t "$TEST_FILE_XC_ARG")
if [[ "$OUT" == *": OK"* ]]; then
    log_pass "Integrity check passed on valid file"
else
    log_fail "Integrity check failed on valid file (expected OK output)"
fi

# Verbose test mode
OUT=$("$ZXC_BIN" -t -v "$TEST_FILE_XC_ARG")
if [[ "$OUT" == *": OK"* ]] && [[ "$OUT" == *"Checksum:"* ]]; then
    log_pass "Integrity check verbose mode"
else
    log_fail "Integrity check verbose mode failed"
fi

# Corrupt file should fail and show "FAILED"
# Corrupt a byte in the middle of the file (after header)
printf '\xff' | dd of="$TEST_FILE_XC_ARG" bs=1 seek=100 count=1 conv=notrunc 2>/dev/null

set +e
OUT=$("$ZXC_BIN" -t "$TEST_FILE_XC_ARG" 2>&1)
RET=$?
set -e
if [[ $RET -ne 0 ]] && [[ "$OUT" == *": FAILED"* ]]; then
    log_pass "Integrity check correctly failed on corrupt file"
else
    log_fail "Integrity check PASSED on corrupt file (False Negative)"
fi

# 10. Global Checksum Integrity
echo "Testing Global Checksum Integrity..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"

# Corrupt the last byte (part of Global Checksum)
FILE_SZ=$(wc -c < "$TEST_FILE_XC_ARG" | tr -d ' ')
LAST_BYTE_OFFSET=$((FILE_SZ - 1))
printf '\x00' | dd of="$TEST_FILE_XC_ARG" bs=1 seek=$LAST_BYTE_OFFSET count=1 conv=notrunc 2>/dev/null

set +e
OUT=$("$ZXC_BIN" -t "$TEST_FILE_XC_ARG" 2>&1)
RET=$?
set -e
if [[ $RET -ne 0 ]] && [[ "$OUT" == *": FAILED"* ]]; then
    log_pass "Integrity check correctly failed on corrupt Global Checksum"
else
    log_fail "Integrity check PASSED on corrupt Global Checksum (False Negative)"
fi

# Ensure no output file is created
if [[ -f "${TEST_FILE}.zxc.zxc" ]] || [[ -f "${TEST_FILE}.zxc.dec" ]]; then
    log_fail "Integrity check created output file unexpectedly"
fi

# 11. List Command (-l)
echo "Testing List Command (-l)..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"

# Normal list mode
OUT=$("$ZXC_BIN" -l "$TEST_FILE_XC_ARG")
if [[ "$OUT" == *"Compressed"* ]] && [[ "$OUT" == *"Uncompressed"* ]] && [[ "$OUT" == *"Ratio"* ]]; then
    log_pass "List command basic output"
else
    log_fail "List command basic output failed"
fi

# Verbose list mode
OUT=$("$ZXC_BIN" -l -v "$TEST_FILE_XC_ARG")
if [[ "$OUT" == *"Block Format:"* ]] && [[ "$OUT" == *"Block Size:"* ]] && [[ "$OUT" == *"Checksum Method:"* ]]; then
    log_pass "List command verbose output"
else
    log_fail "List command verbose output failed"
fi

# List with invalid file should fail
set +e
"$ZXC_BIN" -l "nonexistent_file" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "List command error handling"
else
    log_fail "List command should fail on nonexistent file"
fi

# 12. Compression Levels (All)
echo "Testing All Compression Levels..."
for LEVEL in 1 2 3 4 5 6 7; do
    "$ZXC_BIN" -$LEVEL -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_lvl${LEVEL}.zxc"
    if [[ ! -f "$TEST_DIR/test_lvl${LEVEL}.zxc" ]]; then
        log_fail "Compression level $LEVEL failed"
    fi
    
    # Decompress and verify
    "$ZXC_BIN" -d -c "$TEST_DIR/test_lvl${LEVEL}.zxc" > "$TEST_DIR/test_lvl${LEVEL}.dec"
    if ! cmp -s "$TEST_FILE" "$TEST_DIR/test_lvl${LEVEL}.dec"; then
        log_fail "Level $LEVEL decompression mismatch"
    fi
    
    SIZE=$(wc -c < "$TEST_DIR/test_lvl${LEVEL}.zxc" | tr -d ' ')
    log_pass "Level $LEVEL (Size: $SIZE bytes)"
done

# 13. Data Type Tests
echo "Testing Different Data Types..."

# 13.1 Highly Repetitive Text (Best Compression)
echo "  Testing repetitive data..."
yes "Lorem ipsum dolor sit amet" | head -n 1000 > "$TEST_DIR/test_repetitive.txt"
"$ZXC_BIN" -3 -k "$TEST_DIR/test_repetitive.txt"
if [[ ! -f "$TEST_DIR/test_repetitive.txt.zxc" ]]; then
    log_fail "Repetitive data compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_repetitive.txt.zxc" > "$TEST_DIR/test_repetitive.dec"
if cmp -s "$TEST_DIR/test_repetitive.txt" "$TEST_DIR/test_repetitive.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_repetitive.txt" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_repetitive.txt.zxc" | tr -d ' ')
    RATIO=$((SIZE_ORIG / SIZE_COMP))
    log_pass "Repetitive text (Ratio: ${RATIO}:1)"
else
    log_fail "Repetitive data round-trip failed"
fi

# 13.2 Binary Zeros (Highly Compressible)
echo "  Testing binary zeros..."
dd if=/dev/zero bs=1024 count=100 of="$TEST_DIR/test_zeros.bin" 2>/dev/null
"$ZXC_BIN" -3 -k "$TEST_DIR/test_zeros.bin"
if [[ ! -f "$TEST_DIR/test_zeros.bin.zxc" ]]; then
    log_fail "Binary zeros compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_zeros.bin.zxc" > "$TEST_DIR/test_zeros.dec"
if cmp -s "$TEST_DIR/test_zeros.bin" "$TEST_DIR/test_zeros.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_zeros.bin" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_zeros.bin.zxc" | tr -d ' ')
    RATIO=$((SIZE_ORIG / SIZE_COMP))
    log_pass "Binary zeros (Ratio: ${RATIO}:1)"
else
    log_fail "Binary zeros round-trip failed"
fi

# 13.3 Random Data (Incompressible - Should use RAW blocks)
echo "  Testing random data (incompressible)..."
dd if=/dev/urandom bs=1024 count=100 of="$TEST_DIR/test_random.bin" 2>/dev/null
"$ZXC_BIN" -3 -k "$TEST_DIR/test_random.bin"
if [[ ! -f "$TEST_DIR/test_random.bin.zxc" ]]; then
    log_fail "Random data compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_random.bin.zxc" > "$TEST_DIR/test_random.dec"
if cmp -s "$TEST_DIR/test_random.bin" "$TEST_DIR/test_random.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_random.bin" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_random.bin.zxc" | tr -d ' ')
    # Random data should expand slightly (RAW blocks + headers)
    if [[ $SIZE_COMP -le $((SIZE_ORIG + 200)) ]]; then
        log_pass "Random data (RAW blocks, minimal expansion)"
    else
        log_fail "Random data expanded too much"
    fi
else
    log_fail "Random data round-trip failed"
fi

# 14. Large File Test (Performance Validation)
echo "Testing Large Files..."
dd if=/dev/zero bs=1M count=10 of="$TEST_DIR/test_large.bin" 2>/dev/null
if ! "$ZXC_BIN" -3 -k "$TEST_DIR/test_large.bin"; then
    log_fail "Large file compression failed"
fi
if ! "$ZXC_BIN" -d -c "$TEST_DIR/test_large.bin.zxc" > "$TEST_DIR/test_large.dec"; then
    log_fail "Large file decompression failed"
fi
if cmp -s "$TEST_DIR/test_large.bin" "$TEST_DIR/test_large.dec"; then
    log_pass "Large file (10 MB) round-trip"
else
    log_fail "Large file content mismatch"
fi

# 15. One-Pass Pipe Round-Trip (Critical for Streaming)
echo "Testing One-Pass Pipe Round-Trip..."
cat "$TEST_FILE" | "$ZXC_BIN" -c | "$ZXC_BIN" -dc > "$TEST_DIR/test_pipe_onepass.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_pipe_onepass.dec"; then
    log_pass "One-pass pipe round-trip"
else
    log_fail "One-pass pipe round-trip content mismatch"
fi

# 16. Empty File Edge Case
echo "Testing Empty File..."
touch "$TEST_DIR/test_empty.txt"
"$ZXC_BIN" -3 -k "$TEST_DIR/test_empty.txt"
if [[ ! -f "$TEST_DIR/test_empty.txt.zxc" ]]; then
    log_fail "Empty file compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_empty.txt.zxc" > "$TEST_DIR/test_empty.dec"
if [[ ! -s "$TEST_DIR/test_empty.dec" ]]; then
    log_pass "Empty file round-trip"
else
    log_fail "Empty file produced non-empty output"
fi

# 17. Stdin Detection (No -c flag needed for compression)
echo "Testing Stdin Auto-Detection..."
cat "$TEST_FILE" | "$ZXC_BIN" > "$TEST_DIR/test_stdin_auto.zxc" 2>/dev/null
if [[ -s "$TEST_DIR/test_stdin_auto.zxc" ]]; then
    "$ZXC_BIN" -d -c "$TEST_DIR/test_stdin_auto.zxc" > "$TEST_DIR/test_stdin_auto.dec"
    if cmp -s "$TEST_FILE" "$TEST_DIR/test_stdin_auto.dec"; then
        log_pass "Stdin auto-detection (compression)"
    else
        log_fail "Stdin auto-detection content mismatch"
    fi
else
    log_fail "Stdin auto-detection failed (empty output)"
fi

# 18. Keep Source File (-k)
echo "Testing Keep Source (-k)..."
cp "$TEST_FILE" "$TEST_DIR/test_keep.txt"
"$ZXC_BIN" -k "$TEST_DIR/test_keep.txt"
if [[ -f "$TEST_DIR/test_keep.txt" ]] && [[ -f "$TEST_DIR/test_keep.txt.zxc" ]]; then
    log_pass "Keep source file (-k)"
else
    log_fail "Keep source file failed (source was deleted)"
fi

# 19. Multi-Threading Tests (-T)
echo "Testing Multi-Threading..."

# 19.1 Single Thread (baseline)
echo "  Testing single thread (-T1)..."
"$ZXC_BIN" -3 -T1 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T1.zxc"
if [[ ! -f "$TEST_DIR/test_T1.zxc" ]]; then
    log_fail "Single thread compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_T1.zxc" > "$TEST_DIR/test_T1.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T1.dec"; then
    SIZE_T1=$(wc -c < "$TEST_DIR/test_T1.zxc" | tr -d ' ')
    log_pass "Single thread (-T1, Size: $SIZE_T1)"
else
    log_fail "Single thread round-trip failed"
fi

# 19.2 Multi-Thread (2 threads)
echo "  Testing 2 threads (-T2)..."
"$ZXC_BIN" -3 -T2 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T2.zxc"
if [[ ! -f "$TEST_DIR/test_T2.zxc" ]]; then
    log_fail "2-thread compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_T2.zxc" > "$TEST_DIR/test_T2.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T2.dec"; then
    SIZE_T2=$(wc -c < "$TEST_DIR/test_T2.zxc" | tr -d ' ')
    log_pass "2 threads (-T2, Size: $SIZE_T2)"
else
    log_fail "2-thread round-trip failed"
fi

# 19.3 Multi-Thread (all threads)
echo "  Testing all threads (-T0)..."
"$ZXC_BIN" -3 -T0 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T0.zxc"
if [[ ! -f "$TEST_DIR/test_T0.zxc" ]]; then
    log_fail "all-thread compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_T0.zxc" > "$TEST_DIR/test_T0.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T0.dec"; then
    SIZE_T0=$(wc -c < "$TEST_DIR/test_T0.zxc" | tr -d ' ')
    log_pass "all threads (-T0, Size: $SIZE_T0)"
else
    log_fail "all-thread round-trip failed"
fi

# 19.4 Verify determinism: All outputs should decompress to same content
echo "  Verifying thread-count independence..."
if cmp -s "$TEST_DIR/test_T1.dec" "$TEST_DIR/test_T2.dec" && cmp -s "$TEST_DIR/test_T2.dec" "$TEST_DIR/test_T0.dec"; then
    log_pass "Deterministic output (all thread counts produce valid results)"
else
    log_fail "Thread outputs differ (non-deterministic)"
fi

# 19.5 Cross-compatibility: File compressed with -T2 should decompress without -T flag
echo "  Testing cross-compatibility..."
"$ZXC_BIN" -d -c "$TEST_DIR/test_T2.zxc" > "$TEST_DIR/test_T2_compat.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T2_compat.dec"; then
    log_pass "Cross-compatible decompression"
else
    log_fail "Multi-threaded file not compatible with standard decompression"
fi

# 19.6 Large file with threads (performance validation)
echo "  Testing large file with threads..."
dd if=/dev/zero bs=1M count=5 of="$TEST_DIR/test_large_mt.bin" 2>/dev/null
"$ZXC_BIN" -3 -T4 -c -k "$TEST_DIR/test_large_mt.bin" > "$TEST_DIR/test_large_mt.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/test_large_mt.zxc" > "$TEST_DIR/test_large_mt.dec"
if cmp -s "$TEST_DIR/test_large_mt.bin" "$TEST_DIR/test_large_mt.dec"; then
    log_pass "Large file multi-threading"
else
    log_fail "Large file multi-threading round-trip failed"
fi

# 20. JSON Output Tests
echo "Testing JSON Output (-j, --json)..."

# 20.1 List mode with JSON (single file)
echo "  Testing list mode with JSON (single file)..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_FILE_XC_ARG")
if [[ "$JSON_OUT" == *'"filename"'* ]] && \
   [[ "$JSON_OUT" == *'"compressed_size_bytes"'* ]] && \
   [[ "$JSON_OUT" == *'"uncompressed_size_bytes"'* ]] && \
   [[ "$JSON_OUT" == *'"compression_ratio"'* ]] && \
   [[ "$JSON_OUT" == *'"format_version"'* ]] && \
   [[ "$JSON_OUT" == *'"checksum_method"'* ]]; then
    log_pass "List mode JSON output (single file)"
else
    log_fail "List mode JSON output missing expected fields"
fi

# 20.2 List mode with JSON (multiple files)
echo "  Testing list mode with JSON (multiple files)..."
cp "$TEST_FILE_XC_ARG" "$TEST_DIR/test2.zxc"
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_FILE_XC_ARG" "$TEST_DIR/test2.zxc")
if [[ "$JSON_OUT" == "["* ]] && \
   [[ "$JSON_OUT" == *"]" ]] && \
   [[ "$JSON_OUT" == *'"filename"'* ]] && \
   [[ "$JSON_OUT" == *","* ]]; then
    log_pass "List mode JSON output (multiple files - array)"
else
    log_fail "List mode JSON output should produce array for multiple files"
fi

# 20.3 Benchmark mode with JSON
echo "  Testing benchmark mode with JSON..."
JSON_OUT=$("$ZXC_BIN" -b1 -j "$TEST_FILE_ARG" 2>/dev/null)
if [[ "$JSON_OUT" == *'"input_file"'* ]] && \
   [[ "$JSON_OUT" == *'"input_size_bytes"'* ]] && \
   [[ "$JSON_OUT" == *'"compressed_size_bytes"'* ]] && \
   [[ "$JSON_OUT" == *'"compression_ratio"'* ]] && \
   [[ "$JSON_OUT" == *'"duration_seconds"'* ]] && \
   [[ "$JSON_OUT" == *'"compress_iterations"'* ]] && \
   [[ "$JSON_OUT" == *'"decompress_iterations"'* ]] && \
   [[ "$JSON_OUT" == *'"threads"'* ]] && \
   [[ "$JSON_OUT" == *'"level"'* ]] && \
   [[ "$JSON_OUT" == *'"checksum_enabled"'* ]] && \
   [[ "$JSON_OUT" == *'"compress_speed_mbps"'* ]] && \
   [[ "$JSON_OUT" == *'"decompress_speed_mbps"'* ]]; then
    log_pass "Benchmark mode JSON output"
else
    log_fail "Benchmark mode JSON output missing expected fields"
fi

# 20.4 Integrity check with JSON (valid file)
echo "  Testing integrity check with JSON (valid file)..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"
JSON_OUT=$("$ZXC_BIN" -t -j "$TEST_FILE_XC_ARG")
if [[ "$JSON_OUT" == *'"filename"'* ]] && \
   [[ "$JSON_OUT" == *'"status": "ok"'* ]] && \
   [[ "$JSON_OUT" == *'"checksum_verified"'* ]]; then
    log_pass "Integrity check JSON output (valid file)"
else
    log_fail "Integrity check JSON output missing expected fields"
fi

# 20.5 Integrity check with JSON (corrupt file)
echo "  Testing integrity check with JSON (corrupt file)..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"
# Corrupt a byte
printf '\xff' | dd of="$TEST_FILE_XC_ARG" bs=1 seek=100 count=1 conv=notrunc 2>/dev/null
set +e
JSON_OUT=$("$ZXC_BIN" -t -j "$TEST_FILE_XC_ARG" 2>&1)
RET=$?
set -e
if [[ $RET -ne 0 ]] && \
   [[ "$JSON_OUT" == *'"filename"'* ]] && \
   [[ "$JSON_OUT" == *'"status": "failed"'* ]] && \
   [[ "$JSON_OUT" == *'"error"'* ]]; then
    log_pass "Integrity check JSON output (corrupt file)"
else
    log_fail "Integrity check JSON output for corrupt file incorrect"
fi

# 20.6 Verify JSON is parseable (if jq is available)
echo "  Checking JSON validity (if jq available)..."
if command -v jq &> /dev/null; then
    "$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"
    JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_FILE_XC_ARG")
    if echo "$JSON_OUT" | jq . > /dev/null 2>&1; then
        log_pass "JSON output is valid (verified with jq)"
    else
        log_fail "JSON output is not valid JSON"
    fi
else
    echo "  [SKIP] jq not available, skipping JSON validation"
fi

# 21. Multiple Mode (-m) Tests
echo "Testing Multiple Mode (-m)..."

# 21.1 Compress multiple files
echo "  Testing compress multiple files..."
cp "$TEST_FILE" "$TEST_DIR/multi1.txt"
cp "$TEST_FILE" "$TEST_DIR/multi2.txt"
"$ZXC_BIN" -m -3 "$TEST_DIR/multi1.txt" "$TEST_DIR/multi2.txt"

if [[ -f "$TEST_DIR/multi1.txt.zxc" ]] && [[ -f "$TEST_DIR/multi2.txt.zxc" ]]; then
    log_pass "Compress multiple files (-m)"
else
    log_fail "Compress multiple files failed"
fi

# 21.2 Decompress multiple files
echo "  Testing decompress multiple files..."
rm -f "$TEST_DIR/multi1.txt" "$TEST_DIR/multi2.txt"
"$ZXC_BIN" -d -m "$TEST_DIR/multi1.txt.zxc" "$TEST_DIR/multi2.txt.zxc"

if cmp -s "$TEST_FILE" "$TEST_DIR/multi1.txt" && cmp -s "$TEST_FILE" "$TEST_DIR/multi2.txt"; then
    log_pass "Decompress multiple files (-d -m)"
else
    log_fail "Decompress multiple files failed (content mismatch or missing files)"
fi

# 21.3 Error on multiple and stdout
echo "  Testing stdout restriction with multiple mode..."
set +e
"$ZXC_BIN" -m -c "$TEST_DIR/multi1.txt" "$TEST_DIR/multi2.txt" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Stdout rejected with multiple mode"
else
    log_fail "Stdout should be rejected with multiple mode"
fi

# 22. Recursive Mode (-r) Tests
echo "Testing Recursive Mode (-r)..."

# Create a nested directory structure
mkdir -p "$TEST_DIR/rec_test/subdir1"
mkdir -p "$TEST_DIR/rec_test/subdir2"
cp "$TEST_FILE" "$TEST_DIR/rec_test/fileA.txt"
cp "$TEST_FILE" "$TEST_DIR/rec_test/subdir1/fileB.txt"
cp "$TEST_FILE" "$TEST_DIR/rec_test/subdir2/fileC.txt"

# 22.1 Compress recursively
echo "  Testing compress recursive directory..."
"$ZXC_BIN" -r -3 "$TEST_DIR/rec_test"

if [[ -f "$TEST_DIR/rec_test/fileA.txt.zxc" ]] && \
   [[ -f "$TEST_DIR/rec_test/subdir1/fileB.txt.zxc" ]] && \
   [[ -f "$TEST_DIR/rec_test/subdir2/fileC.txt.zxc" ]]; then
    log_pass "Compress recursive directory (-r)"
else
    log_fail "Compress recursive directory failed"
fi

# 22.2 Decompress recursively
echo "  Testing decompress recursive directory..."
rm -f "$TEST_DIR/rec_test/fileA.txt" "$TEST_DIR/rec_test/subdir1/fileB.txt" "$TEST_DIR/rec_test/subdir2/fileC.txt"
"$ZXC_BIN" -d -r "$TEST_DIR/rec_test"

if cmp -s "$TEST_FILE" "$TEST_DIR/rec_test/fileA.txt" && \
   cmp -s "$TEST_FILE" "$TEST_DIR/rec_test/subdir1/fileB.txt" && \
   cmp -s "$TEST_FILE" "$TEST_DIR/rec_test/subdir2/fileC.txt"; then
    log_pass "Decompress recursive directory (-d -r)"
else
    log_fail "Decompress recursive directory failed (content mismatch or missing files)"
fi

# 23. Block Size Tests (-B)
echo "Testing Block Size (-B)..."

# 23.1 Round-trip with different block sizes
for BS in 4K 64K 512K 1M; do
    echo "  Testing block size -B $BS..."
    "$ZXC_BIN" -3 -B "$BS" -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_bs_${BS}.zxc"
    if [[ ! -s "$TEST_DIR/test_bs_${BS}.zxc" ]]; then
        log_fail "Block size $BS compression produced empty output"
    fi
    "$ZXC_BIN" -d -c "$TEST_DIR/test_bs_${BS}.zxc" > "$TEST_DIR/test_bs_${BS}.dec"
    if cmp -s "$TEST_FILE" "$TEST_DIR/test_bs_${BS}.dec"; then
        SIZE_BS=$(wc -c < "$TEST_DIR/test_bs_${BS}.zxc" | tr -d ' ')
        log_pass "Block size -B $BS (Size: $SIZE_BS)"
    else
        log_fail "Block size $BS round-trip failed"
    fi
done

# 23.2 Test suffix formats (KB, M, MB)
echo "  Testing block size suffix formats..."
"$ZXC_BIN" -3 -B 128KB -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_bs_128KB.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/test_bs_128KB.zxc" > "$TEST_DIR/test_bs_128KB.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_bs_128KB.dec"; then
    log_pass "Block size suffix KB"
else
    log_fail "Block size suffix KB round-trip failed"
fi

"$ZXC_BIN" -3 -B 2MB -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_bs_2MB.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/test_bs_2MB.zxc" > "$TEST_DIR/test_bs_2MB.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_bs_2MB.dec"; then
    log_pass "Block size suffix MB"
else
    log_fail "Block size suffix MB round-trip failed"
fi

# 23.3 Error: invalid block size (not power of 2)
echo "  Testing invalid block size..."
set +e
"$ZXC_BIN" -3 -B 100K "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Invalid block size rejected (100K)"
else
    log_fail "Invalid block size should be rejected (100K is not power of 2)"
fi

# 23.4 Error: block size too small
set +e
"$ZXC_BIN" -3 -B 2K "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Block size too small rejected (2K)"
else
    log_fail "Block size 2K should be rejected (min is 4K)"
fi

# 23.5 Error: block size too large
set +e
"$ZXC_BIN" -3 -B 4M "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Block size too large rejected (4M)"
else
    log_fail "Block size 4M should be rejected (max is 2M)"
fi

# 24. Seekable Format (-S)
echo "Testing Seekable Format (-S)..."

# 24.1 Basic seekable round-trip
echo "  Testing basic seekable round-trip..."
rm -f "$TEST_FILE_XC_ARG" "$TEST_FILE_DEC_BASH"
"$ZXC_BIN" -S -c "$TEST_FILE_ARG" > "$TEST_FILE_XC_ARG"
if [[ ! -s "$TEST_FILE_XC_ARG" ]]; then
    log_fail "Seekable compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_FILE_XC_ARG" > "$TEST_FILE_DEC_BASH"
if cmp -s "$TEST_FILE" "$TEST_FILE_DEC_BASH"; then
    log_pass "Seekable basic round-trip (-S)"
else
    log_fail "Seekable decompression mismatch"
fi

# 24.2 Seekable file must be larger than normal (SEK block overhead)
echo "  Testing seekable overhead..."
"$ZXC_BIN" -3 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/normal.zxc"
"$ZXC_BIN" -3 -S -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable.zxc"
SIZE_NORMAL=$(wc -c < "$TEST_DIR/normal.zxc" | tr -d ' ')
SIZE_SEEKABLE=$(wc -c < "$TEST_DIR/seekable.zxc" | tr -d ' ')
if [[ "$SIZE_SEEKABLE" -gt "$SIZE_NORMAL" ]]; then
    OVERHEAD=$((SIZE_SEEKABLE - SIZE_NORMAL))
    log_pass "Seekable overhead verified (+${OVERHEAD} bytes)"
else
    log_fail "Seekable file should be larger than normal (SEK block missing?)"
fi

# 24.3 Seekable with small block size (many blocks = larger seek table)
echo "  Testing seekable with small blocks (-B 4K)..."
"$ZXC_BIN" -3 -S -B 4K -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable_4k.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/seekable_4k.zxc" > "$TEST_DIR/seekable_4k.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/seekable_4k.dec"; then
    SIZE_4K=$(wc -c < "$TEST_DIR/seekable_4k.zxc" | tr -d ' ')
    # With 4K blocks, overhead should be larger than with default 256K blocks
    if [[ "$SIZE_4K" -gt "$SIZE_SEEKABLE" ]]; then
        log_pass "Seekable small blocks (-B 4K, Size: $SIZE_4K, more entries)"
    else
        log_pass "Seekable small blocks (-B 4K, Size: $SIZE_4K)"
    fi
else
    log_fail "Seekable small blocks round-trip failed"
fi

# 24.4 Seekable with checksum
echo "  Testing seekable + checksum (-S -C)..."
"$ZXC_BIN" -3 -S -C -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable_chk.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/seekable_chk.zxc" > "$TEST_DIR/seekable_chk.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/seekable_chk.dec"; then
    log_pass "Seekable + checksum (-S -C)"
else
    log_fail "Seekable + checksum round-trip failed"
fi

# 24.5 Seekable with multi-threading
echo "  Testing seekable + threads (-S -T2)..."
"$ZXC_BIN" -3 -S -T2 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable_mt.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/seekable_mt.zxc" > "$TEST_DIR/seekable_mt.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/seekable_mt.dec"; then
    log_pass "Seekable + multi-threading (-S -T2)"
else
    log_fail "Seekable + multi-threading round-trip failed"
fi

# 24.6 Seekable across all levels
echo "  Testing seekable across all levels..."
SEEK_ALL_OK=1
for LEVEL in 1 2 3 4 5 6 7; do
    "$ZXC_BIN" -$LEVEL -S -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable_lvl${LEVEL}.zxc"
    "$ZXC_BIN" -d -c "$TEST_DIR/seekable_lvl${LEVEL}.zxc" > "$TEST_DIR/seekable_lvl${LEVEL}.dec"
    if ! cmp -s "$TEST_FILE" "$TEST_DIR/seekable_lvl${LEVEL}.dec"; then
        SEEK_ALL_OK=0
        log_fail "Seekable level $LEVEL round-trip failed"
    fi
done
if [[ "$SEEK_ALL_OK" -eq 1 ]]; then
    log_pass "Seekable across all levels (1-7)"
fi
# 24.7 Seekable pipe round-trip (no fseeko - validates SEK skip on stdin)
echo "  Testing seekable pipe round-trip..."
cat "$TEST_FILE" | "$ZXC_BIN" -S -c | "$ZXC_BIN" -dc > "$TEST_DIR/seekable_pipe.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/seekable_pipe.dec"; then
    log_pass "Seekable pipe round-trip"
else
    log_fail "Seekable pipe round-trip content mismatch"
fi

# 24.8 Seekable + no-checksum
echo "  Testing seekable + no-checksum (-S -N)..."
"$ZXC_BIN" -3 -S -N -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable_nochk.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/seekable_nochk.zxc" > "$TEST_DIR/seekable_nochk.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/seekable_nochk.dec"; then
    log_pass "Seekable + no-checksum (-S -N)"
else
    log_fail "Seekable + no-checksum round-trip failed"
fi

# 24.9 List command on seekable archive
echo "  Testing list command on seekable archive..."
"$ZXC_BIN" -3 -S -C -k -f "$TEST_FILE_ARG"
OUT=$("$ZXC_BIN" -l "$TEST_FILE_XC_ARG")
if [[ "$OUT" == *"Compressed"* ]] && [[ "$OUT" == *"Uncompressed"* ]]; then
    log_pass "List command on seekable archive"
else
    log_fail "List command on seekable archive failed"
fi

# 25. Dictionary Tests (-D)
echo "Testing Dictionary (-D)..."

# 25.1 Train a dictionary using --train
echo "  Training dictionary from test data..."
# Create a few sample files for training
for i in 1 2 3 4 5; do
    cp "$TEST_FILE" "$TEST_DIR/sample_${i}.txt"
done
DICT_FILE="$TEST_DIR/test.zxd"
"$ZXC_BIN" --train -o "$DICT_FILE" "$TEST_DIR"/sample_*.txt 2>/dev/null
if [[ ! -f "$DICT_FILE" ]]; then
    log_fail "Dictionary training failed"
fi
log_pass "Dictionary trained via --train"

# 25.2 Round-trip with dictionary
echo "  Testing dict round-trip..."
"$ZXC_BIN" -3 -D "$DICT_FILE" -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_dict.zxc"
if [[ ! -s "$TEST_DIR/test_dict.zxc" ]]; then
    log_fail "Dict compression failed"
fi
"$ZXC_BIN" -d -D "$DICT_FILE" -c "$TEST_DIR/test_dict.zxc" > "$TEST_DIR/test_dict.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_dict.dec"; then
    log_pass "Dict round-trip (-D)"
else
    log_fail "Dict round-trip content mismatch"
fi

# 25.3 List shows dict_id
echo "  Testing list with dict_id..."
OUT=$("$ZXC_BIN" -l "$TEST_DIR/test_dict.zxc")
if [[ "$OUT" == *"Dict ID"* ]] && [[ "$OUT" == *"0x"* ]]; then
    log_pass "List shows dict_id"
else
    log_fail "List should show dict_id column with 0x value"
fi

# 25.4 List without dict shows dash
echo "  Testing list without dict shows dash..."
"$ZXC_BIN" -3 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_nodict2.zxc"
OUT=$("$ZXC_BIN" -l "$TEST_DIR/test_nodict2.zxc")
if [[ "$OUT" == *"Dict ID"* ]] && [[ "$OUT" == *" -  "* ]]; then
    log_pass "List without dict shows dash"
else
    log_fail "List without dict should show dash in Dict ID column"
fi

# 25.5 JSON list shows dict_id field
echo "  Testing JSON list with dict_id..."
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_DIR/test_dict.zxc")
if [[ "$JSON_OUT" == *'"dict_id"'* ]] && [[ "$JSON_OUT" == *"0x"* ]]; then
    log_pass "JSON list shows dict_id"
else
    log_fail "JSON list should contain dict_id field"
fi

# 25.6 Decompressing a dict archive without -D must fail (the dictionary is mandatory).
echo "  Testing decompress without required dict..."
set +e
"$ZXC_BIN" -d -c "$TEST_DIR/test_dict.zxc" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Decompress without -D correctly fails"
else
    log_fail "Decompress of a dict archive without -D should fail"
fi

# 25.7 Dict with seekable
echo "  Testing dict + seekable (-D -S)..."
"$ZXC_BIN" -3 -D "$DICT_FILE" -S -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_dict_seek.zxc"
"$ZXC_BIN" -d -D "$DICT_FILE" -c "$TEST_DIR/test_dict_seek.zxc" > "$TEST_DIR/test_dict_seek.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_dict_seek.dec"; then
    log_pass "Dict + seekable (-D -S)"
else
    log_fail "Dict + seekable round-trip failed"
fi

# 25.8 Dict with all levels
echo "  Testing dict across all levels..."
DICT_ALL_OK=1
for LEVEL in 1 2 3 4 5 6 7; do
    "$ZXC_BIN" -$LEVEL -D "$DICT_FILE" -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_dict_lvl${LEVEL}.zxc"
    "$ZXC_BIN" -d -D "$DICT_FILE" -c "$TEST_DIR/test_dict_lvl${LEVEL}.zxc" > "$TEST_DIR/test_dict_lvl${LEVEL}.dec"
    if ! cmp -s "$TEST_FILE" "$TEST_DIR/test_dict_lvl${LEVEL}.dec"; then
        DICT_ALL_OK=0
        log_fail "Dict level $LEVEL round-trip failed"
    fi
done
if [[ "$DICT_ALL_OK" -eq 1 ]]; then
    log_pass "Dict across all levels (1-7)"
fi

# 25.9 Invalid dict file should fail
echo "  Testing invalid dict file..."
echo "not a valid dict" > "$TEST_DIR/bad.zxd"
set +e
"$ZXC_BIN" -3 -D "$TEST_DIR/bad.zxd" -c "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Invalid dict file rejected"
else
    log_fail "Invalid dict file should be rejected"
fi

# 25.10 Train to a directory -> dictionary_<dict_id>.zxd
echo "  Testing train-to-directory naming..."
AUTO_DIR="$TEST_DIR/auto"
mkdir -p "$AUTO_DIR"
"$ZXC_BIN" --train -o "$AUTO_DIR/" "$TEST_DIR"/sample_*.txt 2>/dev/null
ZXD_NAME=$(ls "$AUTO_DIR" | grep -E '^dictionary_[0-9a-f]{8}\.zxd$' || true)
if [[ -n "$ZXD_NAME" ]]; then
    log_pass "Train to directory names dict dictionary_<dict_id>.zxd ($ZXD_NAME)"
else
    log_fail "Train to directory should create dictionary_<dict_id>.zxd"
fi

# 25.11 -D is mandatory on decompression: no auto-lookup, even if the .zxd sits
# right next to the archive.
echo "  Testing -D is mandatory (no auto-lookup)..."
"$ZXC_BIN" -3 -D "$AUTO_DIR/$ZXD_NAME" -B 4K -c -k "$TEST_FILE_ARG" > "$AUTO_DIR/payload.zxc"
set +e
"$ZXC_BIN" -d -c "$AUTO_DIR/payload.zxc" > /dev/null 2>&1   # .zxd present, but no -D
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Decompress without -D fails even with .zxd present (no auto-lookup)"
else
    log_fail "Decompress without -D should fail (auto-lookup must be removed)"
fi
"$ZXC_BIN" -d -D "$AUTO_DIR/$ZXD_NAME" -c "$AUTO_DIR/payload.zxc" > "$AUTO_DIR/payload.dec" 2>/dev/null
if cmp -s "$TEST_FILE" "$AUTO_DIR/payload.dec"; then
    log_pass "Decompress with -D succeeds"
else
    log_fail "Decompress with -D failed to recreate original"
fi

# 26. unzxc Alias (argv[0]-based mode detection)
echo "Testing unzxc alias..."

# "unzxc" is a symlink to zxc that defaults to decompression (like unzstd /
# gunzip). Create a local symlink to the binary under test and exercise it.
# Symlinks are POSIX-only; skip gracefully where they are unsupported.
ZXC_ABS=$(cd "$(dirname "$ZXC_BIN")" && pwd)/$(basename "$ZXC_BIN")
UNZXC_BIN="$TEST_DIR/unzxc"

if ln -sf "$ZXC_ABS" "$UNZXC_BIN" 2>/dev/null && [[ -L "$UNZXC_BIN" ]]; then
    # A known archive to decode through the alias.
    "$ZXC_BIN" -z -c -k "$TEST_FILE_ARG" > "$TEST_DIR/alias.zxc"

    # 26.1 unzxc with no mode flag must decompress (equivalent to zxc -d).
    if "$UNZXC_BIN" -c "$TEST_DIR/alias.zxc" > "$TEST_DIR/alias.dec" 2>/dev/null \
       && cmp -s "$TEST_FILE" "$TEST_DIR/alias.dec"; then
        log_pass "unzxc defaults to decompression"
    else
        log_fail "unzxc should decompress by default (like zxc -d)"
    fi

    # 26.2 An explicit -z on the unzxc-named binary overrides back to compression.
    # If the override were broken, unzxc would try to *decode* the plaintext input
    # and fail, so the round-trip below would not match.
    set +e
    "$UNZXC_BIN" -z -c "$TEST_FILE_ARG" > "$TEST_DIR/alias_z.zxc" 2>/dev/null
    "$ZXC_BIN" -d -c "$TEST_DIR/alias_z.zxc" > "$TEST_DIR/alias_z.dec" 2>/dev/null
    set -e
    if cmp -s "$TEST_FILE" "$TEST_DIR/alias_z.dec"; then
        log_pass "unzxc -z overrides back to compression"
    else
        log_fail "unzxc -z should compress (explicit flag must win)"
    fi
else
    echo "  [SKIP] symlinks unsupported here, skipping unzxc alias test"
fi

# 27. Multiple input files (-m) and native Windows wildcard expansion (setargv.obj)
echo "Testing multiple-file mode (-m) and wildcard expansion..."

MGLOB_DIR="$TEST_DIR/mglob"
mkdir -p "$MGLOB_DIR"
for n in 1 2 3; do
    printf 'multi-file payload %s — lorem ipsum dolor sit amet\n' "$n" > "$MGLOB_DIR/part_${n}.txt"
done

# 27.1 Cross-platform: the shell expands the glob; -m must compress every file to
#      <name>.zxc (keeping inputs with -k). Then verify one round-trip is valid.
"$ZXC_BIN" -k -m "$MGLOB_DIR"/part_*.txt >/dev/null 2>&1 || true
if [[ -f "$MGLOB_DIR/part_1.txt.zxc" ]] && [[ -f "$MGLOB_DIR/part_2.txt.zxc" ]] \
   && [[ -f "$MGLOB_DIR/part_3.txt.zxc" ]]; then
    if "$ZXC_BIN" -d -c "$MGLOB_DIR/part_2.txt.zxc" 2>/dev/null | cmp -s - "$MGLOB_DIR/part_2.txt"; then
        log_pass "multiple-file mode (-m) compresses every file"
    else
        log_fail "-m produced a .zxc that does not round-trip"
    fi
else
    log_fail "-m did not produce a .zxc for every input file"
fi

# 27.2 Native Windows wildcard expansion (setargv.obj): cmd/PowerShell don't glob,
#      so the binary must. Pass a *quoted* pattern so the shell does NOT expand it —
#      only the linked setargv.obj should. POSIX programs don't self-glob, so this
#      sub-check is Windows-only (skipped elsewhere, like the unzxc symlink test).
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*)
        ZXC_ABS_G=$(cd "$(dirname "$ZXC_BIN")" && pwd)/$(basename "$ZXC_BIN")
        rm -f "$MGLOB_DIR"/*.zxc
        ( cd "$MGLOB_DIR" && "$ZXC_ABS_G" -k -m 'part_*.txt' >/dev/null 2>&1 ) || true
        if [[ -f "$MGLOB_DIR/part_1.txt.zxc" ]] && [[ -f "$MGLOB_DIR/part_2.txt.zxc" ]] \
           && [[ -f "$MGLOB_DIR/part_3.txt.zxc" ]]; then
            log_pass "native Windows wildcard expansion (setargv.obj)"
        else
            log_fail "literal 'part_*.txt' was not expanded by the binary (setargv.obj missing?)"
        fi
        ;;
    *)
        echo "  [SKIP] native wildcard expansion is Windows-only (POSIX shells glob themselves)"
        ;;
esac

# 28. Output Option (-o / --output / positional OUTPUT-FILE)
echo "Testing Output Option (-o)..."

# 28.1 -o on compression: output goes to the named file and the input is KEPT
#      (auto-deletion only applies when the output name is auto-derived)
cp "$TEST_FILE" "$TEST_DIR/o_src.txt"
"$ZXC_BIN" -f -o "$TEST_DIR/o_out.zxc" "$TEST_DIR/o_src.txt"
if [[ -s "$TEST_DIR/o_out.zxc" ]] && [[ -f "$TEST_DIR/o_src.txt" ]]; then
    "$ZXC_BIN" -d -c "$TEST_DIR/o_out.zxc" > "$TEST_DIR/o_out.dec"
    if cmp -s "$TEST_FILE" "$TEST_DIR/o_out.dec"; then
        log_pass "Compress with -o (named output, input kept)"
    else
        log_fail "Compress with -o round-trip mismatch"
    fi
else
    log_fail "Compress with -o failed (missing output or input was deleted)"
fi

# 28.2 -o on decompression
"$ZXC_BIN" -d -f -o "$TEST_DIR/o_dec.txt" "$TEST_DIR/o_out.zxc"
if cmp -s "$TEST_FILE" "$TEST_DIR/o_dec.txt"; then
    log_pass "Decompress with -o"
else
    log_fail "Decompress with -o failed"
fi

# 28.3 Positional OUTPUT-FILE (zxc -d INPUT OUTPUT)
"$ZXC_BIN" -d -f "$TEST_DIR/o_out.zxc" "$TEST_DIR/o_pos.txt"
if cmp -s "$TEST_FILE" "$TEST_DIR/o_pos.txt"; then
    log_pass "Positional OUTPUT-FILE"
else
    log_fail "Positional OUTPUT-FILE failed"
fi

# 28.4 -o with stdin input, both directions
#      (regression: -o used to be silently ignored when reading from stdin)
"$ZXC_BIN" -f -o "$TEST_DIR/o_stdin.zxc" < "$TEST_FILE"
if [[ ! -s "$TEST_DIR/o_stdin.zxc" ]]; then
    log_fail "Compress from stdin with -o produced no output"
fi
"$ZXC_BIN" -d -f -o "$TEST_DIR/o_stdin.dec" < "$TEST_DIR/o_stdin.zxc"
if cmp -s "$TEST_FILE" "$TEST_DIR/o_stdin.dec"; then
    log_pass "stdin + -o (compress and decompress)"
else
    log_fail "stdin + -o round-trip mismatch"
fi

# 28.5 Identical input/output must be rejected with the input left intact,
#      even when spelled differently (regression: raw string comparison let
#      './file' alias 'file' and O_TRUNC destroyed the input)
cp "$TEST_DIR/o_out.zxc" "$TEST_DIR/ident.zxc"
SZ_BEFORE=$(wc -c < "$TEST_DIR/ident.zxc" | tr -d ' ')
set +e
"$ZXC_BIN" -d -f -o "$TEST_DIR/./ident.zxc" "$TEST_DIR/ident.zxc" > /dev/null 2>&1
RET=$?
set -e
SZ_AFTER=$(wc -c < "$TEST_DIR/ident.zxc" | tr -d ' ')
if [[ $RET -ne 0 ]] && [[ "$SZ_BEFORE" == "$SZ_AFTER" ]]; then
    log_pass "Identical input/output rejected, input intact"
else
    log_fail "Identical input/output should be rejected without touching the input"
fi

# 28.6 -o combined with -m must be rejected
set +e
"$ZXC_BIN" -m -f -o "$TEST_DIR/x.zxc" "$TEST_DIR/multi1.txt" "$TEST_DIR/multi2.txt" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "-o rejected with multiple mode (-m)"
else
    log_fail "-o with -m should be rejected"
fi

# 29. Default Input Deletion (gzip-like behavior)
echo "Testing Default Input Deletion..."
cp "$TEST_FILE" "$TEST_DIR/del_me.txt"
"$ZXC_BIN" -f "$TEST_DIR/del_me.txt"
if [[ ! -f "$TEST_DIR/del_me.txt" ]] && [[ -f "$TEST_DIR/del_me.txt.zxc" ]]; then
    log_pass "Compression deletes input by default"
else
    log_fail "Compression without -k should delete the input"
fi
"$ZXC_BIN" -d -f "$TEST_DIR/del_me.txt.zxc"
if [[ -f "$TEST_DIR/del_me.txt" ]] && [[ ! -f "$TEST_DIR/del_me.txt.zxc" ]] \
   && cmp -s "$TEST_FILE" "$TEST_DIR/del_me.txt"; then
    log_pass "Decompression deletes archive by default"
else
    log_fail "Decompression without -k should delete the archive and restore the file"
fi

# 30. Long Options and Option Parsing
echo "Testing Long Options and Parsing..."

# 30.1 Long-option round-trip (--compress/--decompress/--keep/--force/--stdout)
"$ZXC_BIN" --compress --keep --force --stdout "$TEST_FILE_ARG" > "$TEST_DIR/long.zxc"
"$ZXC_BIN" --decompress --stdout "$TEST_DIR/long.zxc" > "$TEST_DIR/long.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/long.dec"; then
    log_pass "Long options round-trip (--compress/--decompress)"
else
    log_fail "Long options round-trip failed"
fi

# 30.2 Long options with values: separate (--threads 2) and '=' (--block-size=64K)
"$ZXC_BIN" --threads 2 --block-size=64K --keep --force --stdout "$TEST_FILE_ARG" > "$TEST_DIR/long2.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/long2.zxc" > "$TEST_DIR/long2.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/long2.dec"; then
    log_pass "Long options with values (--threads N, --block-size=64K)"
else
    log_fail "Long options with values failed"
fi

# 30.3 --list / --test / --version / --help / -h
OUT_L=$("$ZXC_BIN" --list "$TEST_DIR/long.zxc")
OUT_T=$("$ZXC_BIN" --test "$TEST_DIR/long.zxc")
OUT_V=$("$ZXC_BIN" --version)
OUT_H=$("$ZXC_BIN" --help)
OUT_H2=$("$ZXC_BIN" -h)
if [[ "$OUT_L" == *"Compressed"* ]] && [[ "$OUT_T" == *": OK"* ]] \
   && [[ "$OUT_V" == *"ZXC CLI"* ]] && [[ "$OUT_H" == *"Usage:"* ]] \
   && [[ "$OUT_H2" == *"Usage:"* ]]; then
    log_pass "Long mode options (--list/--test/--version/--help)"
else
    log_fail "Long mode options failed"
fi

# 30.4 Grouped short flags (-zkf)
cp "$TEST_FILE" "$TEST_DIR/group.txt"
"$ZXC_BIN" -zkf "$TEST_DIR/group.txt"
if [[ -f "$TEST_DIR/group.txt" ]] && [[ -f "$TEST_DIR/group.txt.zxc" ]]; then
    log_pass "Grouped short flags (-zkf)"
else
    log_fail "Grouped short flags failed (-zkf should behave like -z -k -f)"
fi

# 30.5 "--" end-of-options marker with a dash-prefixed filename
echo "dash-file payload" > "$TEST_DIR/-dash.txt"
ZXC_ABS_P=$(cd "$(dirname "$ZXC_BIN")" && pwd)/$(basename "$ZXC_BIN")
( cd "$TEST_DIR" && "$ZXC_ABS_P" -z -k -f -- "-dash.txt" ) 2>/dev/null || true
if [[ -f "$TEST_DIR/-dash.txt.zxc" ]]; then
    log_pass "'--' end-of-options marker"
else
    log_fail "'--' should stop option parsing (dash-prefixed file)"
fi

# 30.6 "-" as explicit stdin marker
"$ZXC_BIN" -d -c - < "$TEST_DIR/long.zxc" > "$TEST_DIR/dash_stdin.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/dash_stdin.dec"; then
    log_pass "'-' explicit stdin marker"
else
    log_fail "'-' stdin marker failed"
fi

# 30.7 Invalid option values must be rejected
#      (regressions: trailing junk accepted, signed overflow, atoi fallback to 0)
set +e
"$ZXC_BIN" -T abc "$TEST_FILE_ARG" > /dev/null 2>&1; RET_T=$?
"$ZXC_BIN" -B 4Kxyz "$TEST_FILE_ARG" > /dev/null 2>&1; RET_B1=$?
"$ZXC_BIN" -B 9223372036854775807K "$TEST_FILE_ARG" > /dev/null 2>&1; RET_B2=$?
set -e
if [[ $RET_T -ne 0 ]] && [[ $RET_B1 -ne 0 ]] && [[ $RET_B2 -ne 0 ]]; then
    log_pass "Invalid option values rejected (-T abc, -B 4Kxyz, -B overflow)"
else
    log_fail "Invalid option values should be rejected"
fi

# 30.8 -b must not swallow a digit-leading filename as the duration
set +e
ERR_B=$("$ZXC_BIN" -b 5nonexistent.bin 2>&1)
RET=$?
set -e
if [[ $RET -ne 0 ]] && [[ "$ERR_B" == *"Invalid input file"* ]]; then
    log_pass "-b leaves digit-leading filenames alone"
else
    log_fail "-b consumed a digit-leading filename as duration"
fi

# 31. JSON List Robustness
#     (regression: a failed entry printed nothing, leaving stray commas in the array)
echo "Testing JSON list with a failing entry..."
set +e
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_DIR/long.zxc" "$TEST_DIR/does_not_exist.zxc" "$TEST_DIR/long2.zxc" 2>/dev/null)
RET=$?
set -e
if [[ $RET -ne 0 ]] && [[ "$JSON_OUT" == "["* ]] && [[ "$JSON_OUT" == *"]" ]] \
   && [[ "$JSON_OUT" == *'"error"'* ]]; then
    log_pass "JSON list stays an array when one entry fails (exit code still non-zero)"
else
    log_fail "JSON list with a failing entry produced malformed output or wrong exit code"
fi
if command -v jq &> /dev/null; then
    if echo "$JSON_OUT" | jq . > /dev/null 2>&1; then
        log_pass "JSON list with failing entry is valid JSON (verified with jq)"
    else
        log_fail "JSON list with failing entry is not valid JSON"
    fi
fi

# 32. Benchmark with Dictionary
#     (regression: the dictionary was freed before the benchmark used it)
echo "Testing benchmark with dictionary (-b -D)..."
set +e
"$ZXC_BIN" -q -b1 -D "$DICT_FILE" "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -eq 0 ]]; then
    log_pass "Benchmark with dictionary (-b1 -D)"
else
    log_fail "Benchmark with dictionary failed"
fi

# 33. Deferred Write Error (Linux-only: /dev/full reports ENOSPC on flush)
#     (regression: fclose/fflush failures were ignored, truncated output
#      was reported as success and the input file deleted)
if [[ -w /dev/full ]]; then
    set +e
    "$ZXC_BIN" -c -k -f "$TEST_FILE_ARG" > /dev/full 2>/dev/null
    RET=$?
    set -e
    if [[ $RET -ne 0 ]]; then
        log_pass "Deferred write error detected (full device)"
    else
        log_fail "Write to full device should fail"
    fi
else
    echo "  [SKIP] /dev/full not available (Linux-only)"
fi

# 34. Progress Display (--progress)
echo "Testing Progress Display (--progress)..."

# 34.1 --progress=always emits updates on a non-tty stderr (file input, known size)
"$ZXC_BIN" --progress=always -f -k -c "$TEST_FILE_ARG" > /dev/null 2> "$TEST_DIR/prog1.err"
if grep -q "Compressing \[" "$TEST_DIR/prog1.err" && grep -q "MB/s" "$TEST_DIR/prog1.err"; then
    log_pass "--progress=always emits updates off-tty"
else
    log_fail "--progress=always should emit progress lines on stderr"
fi

# 34.2 --progress=always with stdin input (unknown total size)
"$ZXC_BIN" --progress=always -f -o "$TEST_DIR/prog_stdin.zxc" < "$TEST_FILE" 2> "$TEST_DIR/prog2.err"
if grep -q "Compressing" "$TEST_DIR/prog2.err"; then
    log_pass "--progress=always works with stdin (unknown size)"
else
    log_fail "--progress=always with stdin should report bytes processed"
fi

# 34.3 --progress=never and auto off-tty stay silent
"$ZXC_BIN" --progress=never -f -k -c "$TEST_FILE_ARG" > /dev/null 2> "$TEST_DIR/prog3.err"
"$ZXC_BIN" -f -k -c "$TEST_FILE_ARG" > /dev/null 2> "$TEST_DIR/prog4.err"
if [[ ! -s "$TEST_DIR/prog3.err" ]] && [[ ! -s "$TEST_DIR/prog4.err" ]]; then
    log_pass "--progress=never and auto off-tty are silent"
else
    log_fail "No progress output expected (never / auto off-tty)"
fi

# 34.4 Invalid --progress value rejected
set +e
"$ZXC_BIN" --progress=bogus "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "--progress rejects invalid values"
else
    log_fail "--progress must reject invalid values"
fi

# 34.5 -t with --progress=always labels the operation "Testing"
"$ZXC_BIN" -z -k -f "$TEST_FILE_ARG"
"$ZXC_BIN" -t --progress=always "$TEST_FILE_XC_ARG" > /dev/null 2> "$TEST_DIR/prog5.err"
if grep -q "Testing" "$TEST_DIR/prog5.err"; then
    log_pass "-t progress labeled 'Testing'"
else
    log_fail "-t progress should be labeled 'Testing'"
fi

echo "All tests passed!"
exit 0
