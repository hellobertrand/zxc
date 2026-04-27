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
if [ ! -f "$ZXC_BIN" ]; then
    log_fail "Binary not found at $ZXC_BIN. Please build the project first."
fi
echo "Using binary: $ZXC_BIN"

# 1. Generate Test File (Lorem Ipsum)
echo "Generating test file..."
cat > "$TEST_FILE" <<EOF
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.
Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo. Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit qui in ea voluptate velit esse quam nihil molestiae consequatur, vel illum qui dolorem eum fugiat quo voluptas nulla pariatur?
EOF
# Duplicate content to make it slightly larger
for i in {1..1000}; do
    cat "$TEST_FILE" >> "${TEST_FILE}.tmp"
done
mv "${TEST_FILE}.tmp" "$TEST_FILE"

FILE_SIZE=$(wc -c < "$TEST_FILE" | tr -d ' ')
echo "Test file generated: $TEST_FILE ($FILE_SIZE bytes)"

# Helper: Wait for file to be ready and readable
wait_for_file() {
    local file="$1"
    local retries=10
    local count=0
    # On Windows, file locking can cause race conditions immediately after creation.
    while [ $count -lt $retries ]; do
        if [ -f "$file" ]; then
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

# Re-generate source for valid comparison
cat > "$TEST_FILE" <<EOF
Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.
Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo. Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. Neque porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit qui in ea voluptate velit esse quam nihil molestiae consequatur, vel illum qui dolorem eum fugiat quo voluptas nulla pariatur?
EOF
for i in {1..1000}; do
    cat "$TEST_FILE" >> "${TEST_FILE}.tmp"
done
mv "${TEST_FILE}.tmp" "$TEST_FILE"

if cmp -s "$TEST_FILE" "$TEST_FILE_DEC_BASH"; then
    log_pass "Basic Round-Trip"
else
    log_fail "Basic Round-Trip content mismatch"
fi

# 3. Piping
echo "Testing Piping..."
rm -f "$PIPE_XC" "$PIPE_DEC"
cat "$TEST_FILE" | "$ZXC_BIN" > "$PIPE_XC"
if [ ! -s "$PIPE_XC" ]; then
    log_fail "Piping compression failed (empty output)"
fi

cat "$PIPE_XC" | "$ZXC_BIN" -d > "$PIPE_DEC"
if [ ! -s "$PIPE_DEC" ]; then
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
if [ ! -f "$TEST_FILE_XC_BASH" ]; then log_fail "Level 1 flag failed"; fi
log_pass "Flag -1"

# Force Overwrite (-f)
touch "$TEST_DIR/out.zxc"
touch "${TEST_FILE_XC_BASH}"
set +e
"$ZXC_BIN" -z -k "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [ $RET -eq 0 ]; then
     log_fail "Should have failed to overwrite without -f"
else
     log_pass "Overwrite protection verified"
fi

"$ZXC_BIN" -z -k -f "$TEST_FILE_ARG"
if [ $? -eq 0 ]; then
   log_pass "Force overwrite (-f)"
else
   log_fail "Force overwrite failed"
fi

# 5. Benchmark
echo "Testing Benchmark..."
"$ZXC_BIN" -b1 "$TEST_FILE_ARG" > /dev/null
if [ $? -eq 0 ]; then
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
if [ $RET -ne 0 ]; then
    log_pass "Missing file error handled"
else
    log_fail "Missing file should return error"
fi

# 7. Version
echo "Testing Version..."
OUT_VER=$("$ZXC_BIN" -V)
if [[ "$OUT_VER" == *"zxc"* ]]; then
    log_pass "Version flag"
else
    log_fail "Version flag failed"
fi

# 8. Checksum
echo "Testing Checksum..."
"$ZXC_BIN" -C -k -f "$TEST_FILE_ARG"
if [ ! -f "$TEST_FILE_XC_BASH" ]; then log_fail "Checksum compression failed"; fi
rm -f "$TEST_FILE"
"$ZXC_BIN" -d "$TEST_FILE_XC_ARG"
if [ ! -f "$TEST_FILE" ]; then log_fail "Checksum decompression failed"; fi
log_pass "Checksum enabled (-C)"

"$ZXC_BIN" -N -k -f "$TEST_FILE_ARG"
if [ ! -f "$TEST_FILE_XC_BASH" ]; then log_fail "No-Checksum compression failed"; fi
rm -f "$TEST_FILE"
"$ZXC_BIN" -d "$TEST_FILE_XC_ARG"
if [ ! -f "$TEST_FILE" ]; then log_fail "No-Checksum decompression failed"; fi
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
if [ $RET -ne 0 ] && [[ "$OUT" == *": FAILED"* ]]; then
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
if [ $RET -ne 0 ] && [[ "$OUT" == *": FAILED"* ]]; then
    log_pass "Integrity check correctly failed on corrupt Global Checksum"
else
    log_fail "Integrity check PASSED on corrupt Global Checksum (False Negative)"
fi

# Ensure no output file is created
if [ -f "${TEST_FILE}.zxc.zxc" ] || [ -f "${TEST_FILE}.zxc.dec" ]; then
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
if [[ "$OUT" == *"Block Format:"* ]] && [[ "$OUT" == *"Block Units:"* ]] && [[ "$OUT" == *"Checksum Method:"* ]]; then
    log_pass "List command verbose output"
else
    log_fail "List command verbose output failed"
fi

# List with invalid file should fail
set +e
"$ZXC_BIN" -l "nonexistent_file" > /dev/null 2>&1
RET=$?
set -e
if [ $RET -ne 0 ]; then
    log_pass "List command error handling"
else
    log_fail "List command should fail on nonexistent file"
fi

# 12. Compression Levels (All)
echo "Testing All Compression Levels..."
for LEVEL in 1 2 3 4 5 6; do
    "$ZXC_BIN" -$LEVEL -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_lvl${LEVEL}.zxc"
    if [ ! -f "$TEST_DIR/test_lvl${LEVEL}.zxc" ]; then
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
if [ ! -f "$TEST_DIR/test_repetitive.txt.zxc" ]; then
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
if [ ! -f "$TEST_DIR/test_zeros.bin.zxc" ]; then
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
if [ ! -f "$TEST_DIR/test_random.bin.zxc" ]; then
    log_fail "Random data compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_random.bin.zxc" > "$TEST_DIR/test_random.dec"
if cmp -s "$TEST_DIR/test_random.bin" "$TEST_DIR/test_random.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_random.bin" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_random.bin.zxc" | tr -d ' ')
    # Random data should expand slightly (RAW blocks + headers)
    if [ $SIZE_COMP -le $((SIZE_ORIG + 200)) ]; then
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
if [ ! -f "$TEST_DIR/test_empty.txt.zxc" ]; then
    log_fail "Empty file compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_empty.txt.zxc" > "$TEST_DIR/test_empty.dec"
if [ ! -s "$TEST_DIR/test_empty.dec" ]; then
    log_pass "Empty file round-trip"
else
    log_fail "Empty file produced non-empty output"
fi

# 17. Stdin Detection (No -c flag needed for compression)
echo "Testing Stdin Auto-Detection..."
cat "$TEST_FILE" | "$ZXC_BIN" > "$TEST_DIR/test_stdin_auto.zxc" 2>/dev/null
if [ -s "$TEST_DIR/test_stdin_auto.zxc" ]; then
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
if [ -f "$TEST_DIR/test_keep.txt" ] && [ -f "$TEST_DIR/test_keep.txt.zxc" ]; then
    log_pass "Keep source file (-k)"
else
    log_fail "Keep source file failed (source was deleted)"
fi

# 19. Multi-Threading Tests (-T)
echo "Testing Multi-Threading..."

# 19.1 Single Thread (baseline)
echo "  Testing single thread (-T1)..."
"$ZXC_BIN" -3 -T1 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T1.zxc"
if [ ! -f "$TEST_DIR/test_T1.zxc" ]; then
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
if [ ! -f "$TEST_DIR/test_T2.zxc" ]; then
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
if [ ! -f "$TEST_DIR/test_T0.zxc" ]; then
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
if [ $RET -ne 0 ] && \
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

if [ -f "$TEST_DIR/multi1.txt.zxc" ] && [ -f "$TEST_DIR/multi2.txt.zxc" ]; then
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
if [ $RET -ne 0 ]; then
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

if [ -f "$TEST_DIR/rec_test/fileA.txt.zxc" ] && \
   [ -f "$TEST_DIR/rec_test/subdir1/fileB.txt.zxc" ] && \
   [ -f "$TEST_DIR/rec_test/subdir2/fileC.txt.zxc" ]; then
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
    if [ ! -s "$TEST_DIR/test_bs_${BS}.zxc" ]; then
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
if [ $RET -ne 0 ]; then
    log_pass "Invalid block size rejected (100K)"
else
    log_fail "Invalid block size should be rejected (100K is not power of 2)"
fi

# 23.4 Error: block size too small
set +e
"$ZXC_BIN" -3 -B 2K "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [ $RET -ne 0 ]; then
    log_pass "Block size too small rejected (2K)"
else
    log_fail "Block size 2K should be rejected (min is 4K)"
fi

# 23.5 Error: block size too large
set +e
"$ZXC_BIN" -3 -B 4M "$TEST_FILE_ARG" > /dev/null 2>&1
RET=$?
set -e
if [ $RET -ne 0 ]; then
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
if [ ! -s "$TEST_FILE_XC_ARG" ]; then
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
if [ "$SIZE_SEEKABLE" -gt "$SIZE_NORMAL" ]; then
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
    if [ "$SIZE_4K" -gt "$SIZE_SEEKABLE" ]; then
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
for LEVEL in 1 2 3 4 5 6; do
    "$ZXC_BIN" -$LEVEL -S -c -k "$TEST_FILE_ARG" > "$TEST_DIR/seekable_lvl${LEVEL}.zxc"
    "$ZXC_BIN" -d -c "$TEST_DIR/seekable_lvl${LEVEL}.zxc" > "$TEST_DIR/seekable_lvl${LEVEL}.dec"
    if ! cmp -s "$TEST_FILE" "$TEST_DIR/seekable_lvl${LEVEL}.dec"; then
        SEEK_ALL_OK=0
        log_fail "Seekable level $LEVEL round-trip failed"
    fi
done
if [ "$SEEK_ALL_OK" -eq 1 ]; then
    log_pass "Seekable across all levels (1-5)"
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

echo "All tests passed!"
exit 0
