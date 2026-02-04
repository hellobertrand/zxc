#!/bin/bash
# Copyright (c) 2025-2026, Bertrand Lebonnois
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree.
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
TEST_FILE_XC="${TEST_FILE}.xc"
TEST_FILE_DEC="${TEST_FILE}.dec"
PIPE_XC="$TEST_DIR/test_pipe.xc"
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
touch "$TEST_DIR/out.xc"
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
"$ZXC_BIN" -b "$TEST_FILE_ARG" > /dev/null
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
if [ -f "${TEST_FILE}.xc.xc" ] || [ -f "${TEST_FILE}.xc.dec" ]; then
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
if [[ "$OUT" == *"Format:"* ]] && [[ "$OUT" == *"Block Size:"* ]] && [[ "$OUT" == *"Checksum:"* ]]; then
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
for LEVEL in 1 2 3 4 5; do
    "$ZXC_BIN" -$LEVEL -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_lvl${LEVEL}.xc"
    if [ ! -f "$TEST_DIR/test_lvl${LEVEL}.xc" ]; then
        log_fail "Compression level $LEVEL failed"
    fi
    
    # Decompress and verify
    "$ZXC_BIN" -d -c "$TEST_DIR/test_lvl${LEVEL}.xc" > "$TEST_DIR/test_lvl${LEVEL}.dec"
    if ! cmp -s "$TEST_FILE" "$TEST_DIR/test_lvl${LEVEL}.dec"; then
        log_fail "Level $LEVEL decompression mismatch"
    fi
    
    SIZE=$(wc -c < "$TEST_DIR/test_lvl${LEVEL}.xc" | tr -d ' ')
    log_pass "Level $LEVEL (Size: $SIZE bytes)"
done

# 13. Data Type Tests
echo "Testing Different Data Types..."

# 13.1 Highly Repetitive Text (Best Compression)
echo "  Testing repetitive data..."
yes "Lorem ipsum dolor sit amet" | head -n 1000 > "$TEST_DIR/test_repetitive.txt"
"$ZXC_BIN" -3 -k "$TEST_DIR/test_repetitive.txt"
if [ ! -f "$TEST_DIR/test_repetitive.txt.xc" ]; then
    log_fail "Repetitive data compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_repetitive.txt.xc" > "$TEST_DIR/test_repetitive.dec"
if cmp -s "$TEST_DIR/test_repetitive.txt" "$TEST_DIR/test_repetitive.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_repetitive.txt" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_repetitive.txt.xc" | tr -d ' ')
    RATIO=$((SIZE_ORIG / SIZE_COMP))
    log_pass "Repetitive text (Ratio: ${RATIO}:1)"
else
    log_fail "Repetitive data round-trip failed"
fi

# 13.2 Binary Zeros (Highly Compressible)
echo "  Testing binary zeros..."
dd if=/dev/zero bs=1024 count=100 of="$TEST_DIR/test_zeros.bin" 2>/dev/null
"$ZXC_BIN" -3 -k "$TEST_DIR/test_zeros.bin"
if [ ! -f "$TEST_DIR/test_zeros.bin.xc" ]; then
    log_fail "Binary zeros compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_zeros.bin.xc" > "$TEST_DIR/test_zeros.dec"
if cmp -s "$TEST_DIR/test_zeros.bin" "$TEST_DIR/test_zeros.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_zeros.bin" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_zeros.bin.xc" | tr -d ' ')
    RATIO=$((SIZE_ORIG / SIZE_COMP))
    log_pass "Binary zeros (Ratio: ${RATIO}:1)"
else
    log_fail "Binary zeros round-trip failed"
fi

# 13.3 Random Data (Incompressible - Should use RAW blocks)
echo "  Testing random data (incompressible)..."
dd if=/dev/urandom bs=1024 count=100 of="$TEST_DIR/test_random.bin" 2>/dev/null
"$ZXC_BIN" -3 -k "$TEST_DIR/test_random.bin"
if [ ! -f "$TEST_DIR/test_random.bin.xc" ]; then
    log_fail "Random data compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_random.bin.xc" > "$TEST_DIR/test_random.dec"
if cmp -s "$TEST_DIR/test_random.bin" "$TEST_DIR/test_random.dec"; then
    SIZE_ORIG=$(wc -c < "$TEST_DIR/test_random.bin" | tr -d ' ')
    SIZE_COMP=$(wc -c < "$TEST_DIR/test_random.bin.xc" | tr -d ' ')
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
if ! "$ZXC_BIN" -d -c "$TEST_DIR/test_large.bin.xc" > "$TEST_DIR/test_large.dec"; then
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
if [ ! -f "$TEST_DIR/test_empty.txt.xc" ]; then
    log_fail "Empty file compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_empty.txt.xc" > "$TEST_DIR/test_empty.dec"
if [ ! -s "$TEST_DIR/test_empty.dec" ]; then
    log_pass "Empty file round-trip"
else
    log_fail "Empty file produced non-empty output"
fi

# 17. Stdin Detection (No -c flag needed for compression)
echo "Testing Stdin Auto-Detection..."
cat "$TEST_FILE" | "$ZXC_BIN" > "$TEST_DIR/test_stdin_auto.xc" 2>/dev/null
if [ -s "$TEST_DIR/test_stdin_auto.xc" ]; then
    "$ZXC_BIN" -d -c "$TEST_DIR/test_stdin_auto.xc" > "$TEST_DIR/test_stdin_auto.dec"
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
if [ -f "$TEST_DIR/test_keep.txt" ] && [ -f "$TEST_DIR/test_keep.txt.xc" ]; then
    log_pass "Keep source file (-k)"
else
    log_fail "Keep source file failed (source was deleted)"
fi

# 19. Multi-Threading Tests (-T)
echo "Testing Multi-Threading..."

# 19.1 Single Thread (baseline)
echo "  Testing single thread (-T1)..."
"$ZXC_BIN" -3 -T1 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T1.xc"
if [ ! -f "$TEST_DIR/test_T1.xc" ]; then
    log_fail "Single thread compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_T1.xc" > "$TEST_DIR/test_T1.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T1.dec"; then
    SIZE_T1=$(wc -c < "$TEST_DIR/test_T1.xc" | tr -d ' ')
    log_pass "Single thread (-T1, Size: $SIZE_T1)"
else
    log_fail "Single thread round-trip failed"
fi

# 19.2 Multi-Thread (2 threads)
echo "  Testing 2 threads (-T2)..."
"$ZXC_BIN" -3 -T2 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T2.xc"
if [ ! -f "$TEST_DIR/test_T2.xc" ]; then
    log_fail "2-thread compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_T2.xc" > "$TEST_DIR/test_T2.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T2.dec"; then
    SIZE_T2=$(wc -c < "$TEST_DIR/test_T2.xc" | tr -d ' ')
    log_pass "2 threads (-T2, Size: $SIZE_T2)"
else
    log_fail "2-thread round-trip failed"
fi

# 19.3 Multi-Thread (all threads)
echo "  Testing all threads (-T0)..."
"$ZXC_BIN" -3 -T0 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/test_T0.xc"
if [ ! -f "$TEST_DIR/test_T0.xc" ]; then
    log_fail "all-thread compression failed"
fi
"$ZXC_BIN" -d -c "$TEST_DIR/test_T0.xc" > "$TEST_DIR/test_T0.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T0.dec"; then
    SIZE_T0=$(wc -c < "$TEST_DIR/test_T0.xc" | tr -d ' ')
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
"$ZXC_BIN" -d -c "$TEST_DIR/test_T2.xc" > "$TEST_DIR/test_T2_compat.dec"
if cmp -s "$TEST_FILE" "$TEST_DIR/test_T2_compat.dec"; then
    log_pass "Cross-compatible decompression"
else
    log_fail "Multi-threaded file not compatible with standard decompression"
fi

# 19.6 Large file with threads (performance validation)
echo "  Testing large file with threads..."
dd if=/dev/zero bs=1M count=5 of="$TEST_DIR/test_large_mt.bin" 2>/dev/null
"$ZXC_BIN" -3 -T4 -c -k "$TEST_DIR/test_large_mt.bin" > "$TEST_DIR/test_large_mt.xc"
"$ZXC_BIN" -d -c "$TEST_DIR/test_large_mt.xc" > "$TEST_DIR/test_large_mt.dec"
if cmp -s "$TEST_DIR/test_large_mt.bin" "$TEST_DIR/test_large_mt.dec"; then
    log_pass "Large file multi-threading"
else
    log_fail "Large file multi-threading round-trip failed"
fi

echo "All tests passed!"
exit 0

