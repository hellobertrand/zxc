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

# Test Files
TEST_FILE="testdata"
TEST_FILE_XC="${TEST_FILE}.xc"
TEST_FILE_DEC="${TEST_FILE}.dec"
PIPE_XC="./test_pipe.xc"
PIPE_DEC="./test_pipe.dec"

# Variables for checking file existence
TEST_FILE_XC_BASH="./$TEST_FILE_XC"
TEST_FILE_DEC_BASH="./${TEST_FILE}.dec"

# Arguments passed to ZXC
TEST_FILE_ARG="${TEST_FILE}"
TEST_FILE_XC_ARG="${TEST_FILE}.xc" 

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
    rm -f "$TEST_FILE" "$TEST_FILE_XC" "$TEST_FILE_DEC_BASH" "$PIPE_XC" "$PIPE_DEC" "out.xc"
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
touch "out.xc"
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

echo "All tests passed!"
exit 0
