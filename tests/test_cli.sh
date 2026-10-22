#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# ZXC - High-performance lossless compression
#
# Copyright (c) Bertrand Lebonnois and contributors.
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

# The banner names the target: "(<arch> <32|64>-bit <LE|BE>)", never "unknown".
VER_TARGET_RE='^ZXC CLI \(([a-z0-9_]+) (32|64)-bit (LE|BE)\) v'
if [[ "$OUT_VER" =~ $VER_TARGET_RE ]]; then
    VER_ARCH=${BASH_REMATCH[1]}
    VER_ENDIAN=${BASH_REMATCH[3]}
    # The test runs natively, so the shell shares the binary's byte order.
    if [[ $(printf 'AB' | od -An -tx2 | tr -d ' \n') == "4241" ]]; then HOST_ENDIAN=LE; else HOST_ENDIAN=BE; fi
    if [[ "$VER_ARCH" == "unknown" ]]; then
        log_fail "Version banner has no architecture: $OUT_VER"
    elif [[ "$VER_ENDIAN" != "$HOST_ENDIAN" ]]; then
        log_fail "Version banner says $VER_ENDIAN, host is $HOST_ENDIAN: $OUT_VER"
    else
        log_pass "Version banner names the target ($VER_ARCH $VER_ENDIAN)"
    fi
else
    log_fail "Version banner lacks arch, width or byte order: $OUT_VER"
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

# 9b. Checksum presence in -l comes from the header flag
echo "Testing archive checksum detection (-l)..."
EMPTY="$TEST_DIR/empty"
: > "$EMPTY"
"$ZXC_BIN" -z -k -f -C "$EMPTY"
if ! wait_for_file "$EMPTY.zxc"; then
    log_fail "Could not create empty archive $EMPTY.zxc"
fi

# An empty archive still carries a checksum and a digest
LIST_OUT=$("$ZXC_BIN" -l -j "$EMPTY.zxc")
if [[ "$LIST_OUT" == *'"checksum": "enabled"'* ]] &&
   [[ "$LIST_OUT" == *'"digest": "0x'* ]]; then
    log_pass "-l reports the checksum of an empty -C archive"
else
    log_fail "-l should report a checksum on an empty -C archive: $LIST_OUT"
fi

LIST_OUT=$("$ZXC_BIN" -l -v "$EMPTY.zxc")
if [[ "$LIST_OUT" == *"Checksum:      enabled"* ]] &&
   [[ "$LIST_OUT" == *"Digest:        0x"* ]]; then
    log_pass "-l -v reports the checksum of an empty -C archive, digest unquoted"
else
    log_fail "-l -v should report a checksum on an empty -C archive: $LIST_OUT"
fi

# A -N archive carries no checksum
NOCK="$TEST_DIR/nochecksum"
head -c 200000 "${TEST_FILE}.orig" > "$NOCK"
"$ZXC_BIN" -z -k -f -N "$NOCK"
if ! wait_for_file "$NOCK.zxc"; then
    log_fail "Could not create no-checksum archive $NOCK.zxc"
fi
LIST_OUT=$("$ZXC_BIN" -l -j "$NOCK.zxc")
if [[ "$LIST_OUT" == *'"checksum": "disabled"'* ]]; then
    log_pass "-l reports no checksum for a -N archive"
else
    log_fail "-l should report no checksum for a -N archive: $LIST_OUT"
fi

# 9c. Integrity Check reports the archive's checksum, not the -C/-N flag
echo "Testing Integrity Check checksum reporting..."

# -N archive from 9b: nothing to verify
OUT=$("$ZXC_BIN" -t -v "$NOCK.zxc" 2>&1)
if [[ "$OUT" == *": OK"* ]] && [[ "$OUT" == *"not verified (archive has none)"* ]]; then
    log_pass "-t says a -N archive carries no checksum to verify"
else
    log_fail "-t claims the checksum was verified on an archive that carries none: $OUT"
fi

OUT=$("$ZXC_BIN" -t -j "$NOCK.zxc" 2>&1)
if [[ "$OUT" == *'"checksum_verified": false'* ]] &&
   [[ "$OUT" == *'"checksum": "disabled"'* ]]; then
    log_pass "-t -j reports checksum_verified=false, method none for a -N archive"
else
    log_fail "-t -j reports a verified checksum for an archive that carries none: $OUT"
fi

# A corrupted -N archive must never be reported as checksum-verified
BAD="$TEST_DIR/nochecksum_bad.zxc"
cp "$NOCK.zxc" "$BAD"
BAD_SZ=$(wc -c < "$BAD" | tr -d ' ')
printf '\x5a' | dd of="$BAD" bs=1 seek=$((BAD_SZ / 2)) count=1 conv=notrunc 2>/dev/null
set +e
OUT=$("$ZXC_BIN" -t -v "$BAD" 2>&1)
OUTJ=$("$ZXC_BIN" -t -j "$BAD" 2>&1)
set -e
if [[ "$OUT" != *"Checksum:     verified"* ]] && [[ "$OUTJ" != *'"checksum_verified": true'* ]]; then
    log_pass "-t does not vouch for a corrupted -N archive"
else
    log_fail "-t reports a corrupted -N archive as checksum-verified: $OUT / $OUTJ"
fi

# Section 9 corrupted the shared archive; recreate it
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"

# Piped input: checksum unknown, and the pipe still decodes
set +e
OUT=$(cat "$TEST_FILE_XC_ARG" | "$ZXC_BIN" -t -v - 2>&1)
RET=$?
OUTJ=$(cat "$TEST_FILE_XC_ARG" | "$ZXC_BIN" -t -j - 2>&1)
set -e
if [[ $RET -eq 0 ]] && [[ "$OUT" == *": OK"* ]] &&
   [[ "$OUT" == *"unknown (streamed input)"* ]] &&
   [[ "$OUTJ" == *'"checksum_verified": false'* ]] &&
   [[ "$OUTJ" == *'"checksum": "unknown"'* ]]; then
    log_pass "-t on a pipe reads the archive and reports an unknown checksum"
else
    log_fail "-t on a pipe should succeed and report an unknown checksum (exit $RET): $OUT / $OUTJ"
fi

# A -C archive is still verified
OUT=$("$ZXC_BIN" -t -v "$TEST_FILE_XC_ARG" 2>&1)
OUTJ=$("$ZXC_BIN" -t -j "$TEST_FILE_XC_ARG" 2>&1)
if [[ "$OUT" == *"Checksum:     verified"* ]] &&
   [[ "$OUTJ" == *'"checksum_verified": true'* ]] &&
   [[ "$OUTJ" == *'"checksum": "enabled"'* ]]; then
    log_pass "-t still reports a checksummed archive as verified"
else
    log_fail "-t should report a -C archive as checksum-verified: $OUT / $OUTJ"
fi

# -N on a -C archive: checksum present but skipped, exit status still 0
set +e
OUT=$("$ZXC_BIN" -t -v -N "$TEST_FILE_XC_ARG" 2>&1)
RET=$?
OUTJ=$("$ZXC_BIN" -t -j -N "$TEST_FILE_XC_ARG" 2>&1)
set -e
if [[ $RET -eq 0 ]] && [[ "$OUT" == *"not verified (skipped by -N)"* ]] &&
   [[ "$OUTJ" == *'"checksum_verified": false'* ]] &&
   [[ "$OUTJ" == *'"checksum": "enabled"'* ]]; then
    log_pass "-t -N distinguishes a skipped check from an absent checksum"
else
    log_fail "-t -N should report the archive checksum as present but unchecked (exit $RET): $OUT / $OUTJ"
fi

# Empty -C archive from 9b
OUTJ=$("$ZXC_BIN" -t -j "$EMPTY.zxc" 2>&1)
if [[ "$OUTJ" == *'"checksum": "enabled"'* ]] &&
   [[ "$OUTJ" == *'"checksum_verified": true'* ]]; then
    log_pass "-t reports the checksum of an empty -C archive"
else
    log_fail "-t should verify the checksum of an empty -C archive: $OUTJ"
fi

# 10. Block Checksum Integrity
echo "Testing Block Checksum Integrity..."
"$ZXC_BIN" -z -k -f -C "$TEST_FILE_ARG"

# Flip the last byte of the last block's checksum, before the 8-byte EOF block
# and the footer (digest, then the sizes whose lengths the last byte gives).
FILE_SZ=$(wc -c < "$TEST_FILE_XC_ARG" | tr -d ' ')
LENS=$(dd if="$TEST_FILE_XC_ARG" bs=1 skip=$((FILE_SZ - 1)) count=1 2>/dev/null | od -An -tu1 | tr -d ' ')
FOOTER_SZ=$((8 + (LENS & 7) + (LENS >> 4) + 3))
CK_BYTE_OFFSET=$((FILE_SZ - FOOTER_SZ - 8 - 1))
CK_BYTE=$(dd if="$TEST_FILE_XC_ARG" bs=1 skip=$CK_BYTE_OFFSET count=1 2>/dev/null | od -An -tu1 | tr -d ' ')
printf "$(printf '\\x%02x' $((CK_BYTE ^ 0xFF)))" | dd of="$TEST_FILE_XC_ARG" bs=1 seek=$CK_BYTE_OFFSET count=1 conv=notrunc 2>/dev/null

set +e
OUT=$("$ZXC_BIN" -t "$TEST_FILE_XC_ARG" 2>&1)
RET=$?
set -e
if [[ $RET -ne 0 ]] && [[ "$OUT" == *": FAILED"* ]]; then
    log_pass "Integrity check correctly failed on corrupt block checksum"
else
    log_fail "Integrity check PASSED on corrupt block checksum (False Negative)"
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
if [[ "$OUT" == *"Block Format:"* ]] && [[ "$OUT" == *"Block Size:"* ]] && [[ "$OUT" == *"Checksum:"* ]]; then
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
   [[ "$JSON_OUT" == *'"checksum"'* ]]; then
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

# 22.3 Links are reported and skipped, never followed; the walk still exits 0
#      (POSIX only: symlinks need privileges on Windows).
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *)
        mkdir -p "$TEST_DIR/rec_links/d" "$TEST_DIR/rec_outside"
        cp "$TEST_FILE" "$TEST_DIR/rec_links/d/real.txt"
        cp "$TEST_FILE" "$TEST_DIR/rec_outside/outside.txt"
        ln -s "$TEST_DIR/rec_outside" "$TEST_DIR/rec_links/to_outside"
        ln -s "$TEST_DIR/rec_links/d/real.txt" "$TEST_DIR/rec_links/to_file.txt"
        ln -s . "$TEST_DIR/rec_links/loop"
        ln -s /nonexistent "$TEST_DIR/rec_links/dangling"
        REC_OUT=$("$ZXC_BIN" -r -k -3 "$TEST_DIR/rec_links" 2>&1); REC_RC=$?
        if [[ $REC_RC -eq 0 ]] && [[ -f "$TEST_DIR/rec_links/d/real.txt.zxc" ]] && \
           [[ ! -e "$TEST_DIR/rec_links/to_file.txt.zxc" ]] && \
           [[ ! -e "$TEST_DIR/rec_outside/outside.txt.zxc" ]] && \
           [[ $(printf '%s\n' "$REC_OUT" | grep -c "is a link, ignored") -eq 4 ]]; then
            log_pass "Recursive mode skips links (-r)"
        else
            log_fail "Recursive mode followed a link or failed (rc=$REC_RC): $REC_OUT"
        fi
        ;;
esac

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

# 25.10b An existing dictionary, or a link at its path, needs -f and is then replaced
set +e
"$ZXC_BIN" --train -o "$DICT_FILE" "$TEST_DIR"/sample_*.txt 2>/dev/null
RC_NOF=$?
"$ZXC_BIN" --train -f -o "$DICT_FILE" "$TEST_DIR"/sample_*.txt 2>/dev/null
RC_F=$?
set -e
if [[ $RC_NOF -ne 0 && $RC_F -eq 0 && -s "$DICT_FILE" ]]; then
    log_pass "--train refuses an existing output without -f"
else
    log_fail "--train onto an existing output: rc=$RC_NOF/$RC_F (want !=0/0)"
fi
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) echo "  [SKIP] --train link output: POSIX only" ;;
    *)
        DICT_LN="$TEST_DIR/dict_ln.zxd"
        ln -s "$TEST_DIR/dict_victim" "$DICT_LN"
        set +e
        "$ZXC_BIN" --train -o "$DICT_LN" "$TEST_DIR"/sample_*.txt 2>/dev/null
        RC_NOF=$?
        "$ZXC_BIN" --train -f -o "$DICT_LN" "$TEST_DIR"/sample_*.txt 2>/dev/null
        RC_F=$?
        set -e
        if [[ $RC_NOF -ne 0 && $RC_F -eq 0 && ! -e "$TEST_DIR/dict_victim" && ! -L "$DICT_LN" ]]; then
            log_pass "--train replaces a link at its output, never follows it"
        else
            log_fail "--train link output: rc=$RC_NOF/$RC_F (want !=0/0), target created or link kept"
        fi
        ;;
esac

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

# 26. unzxc and zxccat Aliases (argv[0]-based mode detection)
echo "Testing unzxc and zxccat aliases..."

# "unzxc" is a symlink to zxc that defaults to decompression.
# Create a local symlink to the binary under test and exercise it.
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

    # 26.3 zxccat: every argument in order, or stdin; the second must not truncate the first.
    ZXCCAT_BIN="$TEST_DIR/zxccat"
    ln -sf "$ZXC_ABS" "$ZXCCAT_BIN"
    printf 'second archive\n' > "$TEST_DIR/cat_second.txt"
    "$ZXC_BIN" -c "$TEST_DIR/cat_second.txt" > "$TEST_DIR/cat_second.zxc"
    cat "$TEST_FILE" "$TEST_DIR/cat_second.txt" > "$TEST_DIR/cat_expected"
    set +e
    "$ZXCCAT_BIN" "$TEST_DIR/alias.zxc" "$TEST_DIR/cat_second.zxc" > "$TEST_DIR/cat_out" 2>/dev/null
    CAT_RC=$?
    "$ZXCCAT_BIN" < "$TEST_DIR/alias.zxc" > "$TEST_DIR/cat_stdin" 2>/dev/null
    CAT_STDIN_RC=$?
    set -e
    if [[ $CAT_RC -eq 0 ]] && cmp -s "$TEST_DIR/cat_expected" "$TEST_DIR/cat_out" && \
       [[ $CAT_STDIN_RC -eq 0 ]] && cmp -s "$TEST_FILE" "$TEST_DIR/cat_stdin"; then
        log_pass "zxccat decompresses every argument, or stdin, to stdout"
    else
        log_fail "zxccat output differs from the concatenated inputs (rc=$CAT_RC/$CAT_STDIN_RC)"
    fi

    # 26.3b zxccat implies -f: plain files and stdin come out as is
    cat "$TEST_FILE" "$TEST_DIR/cat_second.txt" > "$TEST_DIR/cat_mixed_expected"
    set +e
    "$ZXCCAT_BIN" "$TEST_DIR/alias.zxc" "$TEST_DIR/cat_second.txt" > "$TEST_DIR/cat_mixed" 2>/dev/null
    CAT_RC=$?
    "$ZXCCAT_BIN" < "$TEST_DIR/cat_second.txt" > "$TEST_DIR/cat_plain_stdin" 2>/dev/null
    CAT_STDIN_RC=$?
    set -e
    if [[ $CAT_RC -eq 0 && $CAT_STDIN_RC -eq 0 ]] &&
       cmp -s "$TEST_DIR/cat_mixed_expected" "$TEST_DIR/cat_mixed" &&
       cmp -s "$TEST_DIR/cat_second.txt" "$TEST_DIR/cat_plain_stdin"; then
        log_pass "zxccat passes plain files and stdin through"
    else
        log_fail "zxccat must pass non-archives through (rc=$CAT_RC/$CAT_STDIN_RC)"
    fi
else
    echo "  [SKIP] symlinks unsupported here, skipping unzxc and zxccat alias tests"
fi

# 26.4 -c appends to a >> redirect
"$ZXC_BIN" -z -c -k "$TEST_FILE_ARG" > "$TEST_DIR/append.zxc"
printf 'kept line\n' > "$TEST_DIR/append_out"
"$ZXC_BIN" -d -c "$TEST_DIR/append.zxc" >> "$TEST_DIR/append_out"
{ printf 'kept line\n'; cat "$TEST_FILE"; } > "$TEST_DIR/append_expected"
if cmp -s "$TEST_DIR/append_expected" "$TEST_DIR/append_out"; then
    log_pass "-c appends to a >> redirect"
else
    log_fail "-c truncated the file it should append to"
fi

# 26.5 zxcless pages .zxc files through less, and other files unchanged (POSIX
#      only: not installed on Windows). Off a terminal, less copies to stdout.
ZXCLESS="$(dirname "$0")/../src/cli/zxcless"
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) echo "  [SKIP] zxcless is not installed on Windows" ;;
    *)
        if command -v less >/dev/null 2>&1; then
            "$ZXC_BIN" -z -c -k "$TEST_FILE_ARG" > "$TEST_DIR/less.zxc"
            printf 'plain text\n' > "$TEST_DIR/less_plain.txt"
            { cat "$TEST_FILE"; cat "$TEST_DIR/less_plain.txt"; } > "$TEST_DIR/less_expected"
            set +e
            ZXC="$ZXC_ABS" sh "$ZXCLESS" "$TEST_DIR/less.zxc" "$TEST_DIR/less_plain.txt" \
                > "$TEST_DIR/less_out" 2>/dev/null
            set -e
            if cmp -s "$TEST_DIR/less_expected" "$TEST_DIR/less_out"; then
                log_pass "zxcless decompresses .zxc files and shows others unchanged"
            else
                log_fail "zxcless output differs from the decompressed file"
            fi

            # Standard input: an archive is decompressed, plain text shown unchanged
            set +e
            ZXC="$ZXC_ABS" sh "$ZXCLESS" < "$TEST_DIR/less.zxc" > "$TEST_DIR/less_stdin_zxc" 2>/dev/null
            ZXC="$ZXC_ABS" sh "$ZXCLESS" < "$TEST_DIR/less_plain.txt" > "$TEST_DIR/less_stdin_plain" 2>/dev/null
            set -e
            if cmp -s "$TEST_FILE" "$TEST_DIR/less_stdin_zxc" &&
               cmp -s "$TEST_DIR/less_plain.txt" "$TEST_DIR/less_stdin_plain"; then
                log_pass "zxcless decompresses an archive on stdin and shows plain stdin unchanged"
            else
                log_fail "zxcless on stdin: archive not decompressed or plain text lost"
            fi

            # The filter: -f for stdin only, a named plain file is left to less
            set +e
            ZXC="$ZXC_ABS" sh "$ZXCLESS" --zxcless-filter - < "$TEST_DIR/less_plain.txt" \
                > "$TEST_DIR/less_filter_stdin" 2>/dev/null
            ZXC="$ZXC_ABS" sh "$ZXCLESS" --zxcless-filter "$TEST_DIR/less_plain.txt" \
                > "$TEST_DIR/less_filter_named" 2>/dev/null < /dev/null
            set -e
            if cmp -s "$TEST_DIR/less_plain.txt" "$TEST_DIR/less_filter_stdin" &&
               [[ ! -s "$TEST_DIR/less_filter_named" ]]; then
                log_pass "zxcless passes plain stdin through and leaves named files to less"
            else
                log_fail "zxcless filter: plain stdin not copied, or a named plain file piped"
            fi

            # An empty archive shows empty, not as its raw bytes
            printf '' | "$ZXC_BIN" -z -c > "$TEST_DIR/less_empty.zxc"
            set +e
            ZXC="$ZXC_ABS" sh "$ZXCLESS" "$TEST_DIR/less_empty.zxc" \
                > "$TEST_DIR/less_empty_out" 2>/dev/null < /dev/null
            set -e
            if [[ -s "$TEST_DIR/less_empty.zxc" && ! -s "$TEST_DIR/less_empty_out" ]]; then
                log_pass "zxcless shows an empty archive as empty"
            else
                log_fail "zxcless showed the raw bytes of an empty archive"
            fi

            # --dict supplies the dictionary of archives that need one
            mkdir -p "$TEST_DIR/less_dict"
            for n in 1 2 3 4 5; do
                printf 'sample %s: connection to database lost, retrying\n' "$n" > "$TEST_DIR/less_dict/s$n"
            done
            "$ZXC_BIN" --train -q -o "$TEST_DIR/less_dict/d.zxd" "$TEST_DIR/less_dict"/s* > /dev/null 2>&1
            "$ZXC_BIN" -z -c -k -D "$TEST_DIR/less_dict/d.zxd" "$TEST_FILE_ARG" > "$TEST_DIR/less_dict/a.zxc"
            set +e
            ZXC="$ZXC_ABS" sh "$ZXCLESS" --dict="$TEST_DIR/less_dict/d.zxd" "$TEST_DIR/less_dict/a.zxc" \
                > "$TEST_DIR/less_dict_out" 2>/dev/null < /dev/null
            set -e
            if cmp -s "$TEST_FILE" "$TEST_DIR/less_dict_out"; then
                log_pass "zxcless --dict shows an archive compressed with a dictionary"
            else
                log_fail "zxcless --dict did not decode the archive"
            fi

            # A file named like an option is still a file
            cp "$TEST_DIR/less.zxc" "$TEST_DIR/-k.zxc"
            ZXCLESS_ABS="$(cd "$(dirname "$ZXCLESS")" && pwd)/zxcless"
            set +e
            (cd "$TEST_DIR" && ZXC="$ZXC_ABS" sh "$ZXCLESS_ABS" -- -k.zxc) \
                > "$TEST_DIR/less_dash_out" 2>/dev/null < /dev/null
            set -e
            if cmp -s "$TEST_FILE" "$TEST_DIR/less_dash_out"; then
                log_pass "zxcless shows a file whose name starts with '-'"
            else
                log_fail "zxcless passed a file named '-k.zxc' to zxc as an option"
            fi
        else
            echo "  [SKIP] less is not installed"
        fi
        ;;
esac

# 26.6 zxcgrep searches .zxc files and other files unchanged (POSIX only)
ZXCGREP="$(dirname "$0")/../src/cli/zxcgrep"
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) echo "  [SKIP] zxcgrep is not installed on Windows" ;;
    *)
        zxcgrep() { ZXC="$ZXC_ABS" sh "$ZXCGREP" "$@"; }
        printf 'alpha\nERROR one\nbeta\n' > "$TEST_DIR/grep.log"
        "$ZXC_BIN" -z -c -k "$TEST_DIR/grep.log" > "$TEST_DIR/grep.log.zxc"
        printf "ERROR it's\n" > "$TEST_DIR/grep it's.txt"
        printf '%s:ERROR one\n%s:ERROR it'"'"'s\n' "$TEST_DIR/grep.log.zxc" \
            "$TEST_DIR/grep it's.txt" > "$TEST_DIR/grep_expected"
        set +e
        zxcgrep ERROR "$TEST_DIR/grep.log.zxc" "$TEST_DIR/grep it's.txt" > "$TEST_DIR/grep_out" 2>/dev/null
        GREP_RC=$?
        zxcgrep -c -i -e error -e beta < "$TEST_DIR/grep.log.zxc" > "$TEST_DIR/grep_stdin" 2>/dev/null
        GREP_STDIN_RC=$?
        zxcgrep nomatch "$TEST_DIR/grep.log.zxc" > /dev/null 2>&1
        GREP_NONE_RC=$?
        set -e
        if [[ $GREP_RC -eq 0 && $GREP_STDIN_RC -eq 0 && $GREP_NONE_RC -eq 1 ]] &&
           cmp -s "$TEST_DIR/grep_expected" "$TEST_DIR/grep_out" &&
           [[ "$(cat "$TEST_DIR/grep_stdin")" == 2 ]]; then
            log_pass "zxcgrep searches archives, plain files and stdin, with grep's status"
        else
            log_fail "zxcgrep output or status wrong (rc=$GREP_RC/$GREP_STDIN_RC/$GREP_NONE_RC)"
        fi

        # A damaged archive is 2 despite a match elsewhere; -s silences zxc
        head -c 30 "$TEST_DIR/grep.log.zxc" > "$TEST_DIR/grep_trunc.zxc"
        set +e
        zxcgrep ERROR "$TEST_DIR/grep_trunc.zxc" "$TEST_DIR/grep.log.zxc" > /dev/null 2>&1
        GREP_BAD_RC=$?
        zxcgrep -s ERROR "$TEST_DIR/grep_missing.zxc" > /dev/null 2> "$TEST_DIR/grep_s_err"
        GREP_S_RC=$?
        set -e
        if [[ $GREP_BAD_RC -eq 2 && $GREP_S_RC -eq 2 && ! -s "$TEST_DIR/grep_s_err" ]]; then
            log_pass "zxcgrep reports a damaged archive, and -s silences zxc"
        else
            log_fail "zxcgrep: damaged archive not an error, or -s let zxc talk (rc=$GREP_BAD_RC/$GREP_S_RC)"
        fi

        # Options that pick files, and -f -, are refused
        GREP_REFUSED=0
        for opt in -ir --exclude=x -d --include; do
            set +e
            zxcgrep "$opt" skip ERROR "$TEST_DIR/grep.log.zxc" > /dev/null 2> "$TEST_DIR/grep_opt_err"
            GREP_OPT_RC=$?
            set -e
            if [[ $GREP_OPT_RC -eq 2 ]] && grep -q "not supported" "$TEST_DIR/grep_opt_err"; then
                GREP_REFUSED=$((GREP_REFUSED + 1))
            fi
        done
        set +e
        echo ERROR | zxcgrep -f - "$TEST_DIR/grep.log.zxc" > /dev/null 2> "$TEST_DIR/grep_opt_err"
        GREP_OPT_RC=$?
        set -e
        if [[ $GREP_REFUSED -eq 4 && $GREP_OPT_RC -eq 2 ]] && grep -q "not supported" "$TEST_DIR/grep_opt_err"; then
            log_pass "zxcgrep refuses -r, -d, --include, --exclude and -f -"
        else
            log_fail "zxcgrep accepted an option it can't honor ($GREP_REFUSED/4, -f - rc=$GREP_OPT_RC)"
        fi

        # --dict supplies the dictionary; without it the archive is an error
        "$ZXC_BIN" --train -q -o "$TEST_DIR/grep_d.zxd" "$TEST_DIR/grep.log" "$TEST_DIR/grep it's.txt" \
            "$TEST_DIR/grep.log" "$TEST_DIR/grep it's.txt" "$TEST_DIR/grep.log" > /dev/null 2>&1
        "$ZXC_BIN" -z -c -k -D "$TEST_DIR/grep_d.zxd" "$TEST_DIR/grep.log" > "$TEST_DIR/grep_dict.zxc"
        set +e
        zxcgrep --dict "$TEST_DIR/grep_d.zxd" ERROR "$TEST_DIR/grep_dict.zxc" > "$TEST_DIR/grep_dict_out" 2>/dev/null
        GREP_DICT_RC=$?
        zxcgrep ERROR "$TEST_DIR/grep_dict.zxc" > /dev/null 2>&1
        GREP_NODICT_RC=$?
        set -e
        if [[ $GREP_DICT_RC -eq 0 && $GREP_NODICT_RC -eq 2 &&
              "$(cat "$TEST_DIR/grep_dict_out")" == "ERROR one" ]]; then
            log_pass "zxcgrep --dict searches an archive compressed with a dictionary"
        else
            log_fail "zxcgrep --dict failed (rc=$GREP_DICT_RC, without: $GREP_NODICT_RC)"
        fi

        # grep stopping early is no error, even with SIGPIPE ignored (CI); -q stops at a match
        "$ZXC_BIN" -z -c -k "$TEST_FILE_ARG" > "$TEST_DIR/grep_big.zxc"
        set +e
        zxcgrep -l Lorem "$TEST_DIR/grep_big.zxc" > /dev/null 2> "$TEST_DIR/grep_l_err"
        GREP_EARLY_RC=$?
        zxcgrep -q ERROR "$TEST_DIR/grep.log.zxc" "$TEST_DIR/grep_missing.zxc" > /dev/null 2>&1
        GREP_Q_RC=$?
        set -e
        if [[ $GREP_EARLY_RC -eq 0 && $GREP_Q_RC -eq 0 && ! -s "$TEST_DIR/grep_l_err" ]]; then
            log_pass "zxcgrep ignores SIGPIPE when grep stops early, and -q stops at a match"
        else
            log_fail "zxcgrep failed when grep stopped reading (rc=$GREP_EARLY_RC/$GREP_Q_RC)"
        fi

        # Output closed: stop with grep's status (SIGPIPE, or 2 if ignored)
        cp "$TEST_DIR/grep_big.zxc" "$TEST_DIR/grep_big_next.zxc"
        set +e
        zxcgrep -v nomatch "$TEST_DIR/grep_big.zxc" "$TEST_DIR/grep_big_next.zxc" \
            2> "$TEST_DIR/grep_head_err" | head -n 1 > /dev/null
        GREP_HEAD_RC=${PIPESTATUS[0]}
        set -e
        if [[ $GREP_HEAD_RC -gt 128 || $GREP_HEAD_RC -eq 2 ]] &&
           ! grep -q grep_big_next "$TEST_DIR/grep_head_err"; then
            log_pass "zxcgrep stops when its output is closed"
        else
            log_fail "zxcgrep kept going after its output closed (rc=$GREP_HEAD_RC)"
        fi
        unset -f zxcgrep
        ;;
esac

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
#     Cygwin/MSYS also expose /dev/full, but a NATIVE Windows child does not
#     inherit its ENOSPC semantics (writes succeed), so require real Linux.
if [[ "$(uname -s 2>/dev/null)" == "Linux" ]] && [[ -w /dev/full ]]; then
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
    echo "  [SKIP] deferred write-error check requires Linux /dev/full"
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
set +e
"$ZXC_BIN" -z -k -f "$TEST_FILE_ARG" 2> "$TEST_DIR/prog5z.err"
ZRET=$?
set -e
if [[ $ZRET -ne 0 ]]; then
    echo "  -z exit $ZRET, stderr: $(cat "$TEST_DIR/prog5z.err")"
    log_fail "-t progress setup: compressing $TEST_FILE_ARG failed"
else
    set +e
    "$ZXC_BIN" -t --progress=always "$TEST_FILE_XC_ARG" > /dev/null 2> "$TEST_DIR/prog5.err"
    TRET=$?
    set -e
    if [[ $TRET -ne 0 ]]; then
        echo "  -t exit $TRET, stderr: $(cat "$TEST_DIR/prog5.err")"
        log_fail "-t --progress=always failed on a valid archive"
    elif grep -q "Testing" "$TEST_DIR/prog5.err"; then
        log_pass "-t progress labeled 'Testing'"
    else
        echo "  -t stderr: $(cat "$TEST_DIR/prog5.err")"
        log_fail "-t progress should be labeled 'Testing'"
    fi
fi

# 35. Containers: concatenated frames
echo "Testing containers..."

# 35.1 Two archives back to back decode as one stream, from a file and from stdin
"$ZXC_BIN" -3 -c -k "$TEST_FILE_ARG" > "$TEST_DIR/cat_a.zxc"
"$ZXC_BIN" -1 -N -S -c -k "$TEST_FILE_ARG" > "$TEST_DIR/cat_b.zxc"
cat "$TEST_DIR/cat_a.zxc" "$TEST_DIR/cat_b.zxc" > "$TEST_DIR/cat.zxc"
cat "$TEST_FILE" "$TEST_FILE" > "$TEST_DIR/cat.expected"
"$ZXC_BIN" -d -c "$TEST_DIR/cat.zxc" > "$TEST_DIR/cat.dec"
"$ZXC_BIN" -d -c < "$TEST_DIR/cat.zxc" > "$TEST_DIR/cat_pipe.dec"
if cmp -s "$TEST_DIR/cat.expected" "$TEST_DIR/cat.dec" &&
    cmp -s "$TEST_DIR/cat.expected" "$TEST_DIR/cat_pipe.dec"; then
    log_pass "Concatenated archives decode (file and stdin)"
else
    log_fail "Concatenated archives should decode to both inputs"
fi

# 35.2 -t accepts them and reports the mixed checksums; -l counts the frames
OUT=$("$ZXC_BIN" -t -v "$TEST_DIR/cat.zxc")
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_DIR/cat.zxc")
LIST_OUT=$("$ZXC_BIN" -l "$TEST_DIR/cat.zxc")
if [[ "$OUT" == *"OK"* ]] && [[ "$OUT" == *"partly verified"* ]] &&
    [[ "$JSON_OUT" == *'"frames": 2'* ]] && [[ "$JSON_OUT" == *'"checksum": "mixed"'* ]] &&
    [[ "$LIST_OUT" == *"Frames"* ]] &&
    echo "$LIST_OUT" | awk 'NR==3 {exit !($NF == 2)}'; then
    log_pass "-t and -l on concatenated archives"
else
    echo "  -t: $OUT"
    echo "  -l: $JSON_OUT"
    echo "$LIST_OUT"
    log_fail "-t / -l should handle concatenated archives"
fi

# 35.2b -l: a Seekable column; Filename only when several files are listed
ONE=$("$ZXC_BIN" -l "$TEST_DIR/cat.zxc")
TWO=$("$ZXC_BIN" -l "$TEST_DIR/cat_a.zxc" "$TEST_DIR/cat_b.zxc")
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_DIR/cat_b.zxc")
if [[ "$ONE" != *"Filename"* ]] && [[ "$ONE" == *"Seekable"* ]] && [[ "$ONE" == *"mixed"* ]] &&
    [[ "$TWO" == *"Filename"* ]] && [[ "$TWO" == *"cat_b.zxc"* ]] &&
    [[ "$JSON_OUT" == *'"seekable": "enabled"'* ]]; then
    log_pass "-l shows Seekable, and Filename only for several files"
else
    echo "$ONE"
    echo "$TWO"
    log_fail "-l columns are wrong"
fi

# 35.2c Block sizes that differ between frames: listed by -v, an array in JSON
"$ZXC_BIN" -B 256K -c -k "$TEST_FILE_ARG" > "$TEST_DIR/cat_c.zxc"
cat "$TEST_DIR/cat_a.zxc" "$TEST_DIR/cat_c.zxc" > "$TEST_DIR/cat_bs.zxc"
VOUT=$("$ZXC_BIN" -l -v "$TEST_DIR/cat_bs.zxc")
JSON_OUT=$("$ZXC_BIN" -l -j "$TEST_DIR/cat_bs.zxc")
if [[ "$VOUT" == *"Block Size:    256 KB, 512 KB"* ]] && [[ "$JSON_OUT" == *'"block_size_kb": [256, 512]'* ]]; then
    log_pass "-l reports block sizes that differ between frames"
else
    echo "$VOUT"
    log_fail "-l should report every block size"
fi

# 35.3 Trailing garbage after the last footer is an error
cp "$TEST_DIR/cat.zxc" "$TEST_DIR/cat_bad.zxc"
printf 'JUNK' >> "$TEST_DIR/cat_bad.zxc"
set +e
"$ZXC_BIN" -t "$TEST_DIR/cat_bad.zxc" > /dev/null 2>&1
RET=$?
set -e
if [[ $RET -ne 0 ]]; then
    log_pass "Trailing garbage rejected"
else
    log_fail "Trailing garbage after the last footer must fail"
fi

# 36. Modes come from options only: a bare argument is a file, whatever its name
echo "Testing files named like modes..."
WORD_DIR="$TEST_DIR/words"
mkdir -p "$WORD_DIR"
for w in z d l list t test b; do
    printf 'file named %s\n' "$w" > "$WORD_DIR/$w"
    cp "$WORD_DIR/$w" "$WORD_DIR/$w.orig"
    set +e
    (cd "$WORD_DIR" && "$ZXC_ABS" "$w" && "$ZXC_ABS" -d -- "$w.zxc") > /dev/null 2>&1
    RET=$?
    set -e
    if [[ $RET -ne 0 ]] || ! cmp -s "$WORD_DIR/$w.orig" "$WORD_DIR/$w"; then
        log_fail "'zxc $w' must compress the file named '$w'"
    fi
done
log_pass "Files named like modes compress and decompress"

# 37. -dcf passes through input that is not an archive
echo "Testing -dcf pass-through..."
printf 'plain text, not an archive\n' > "$TEST_DIR/pass_plain.txt"
"$ZXC_BIN" -z -c -k "$TEST_FILE_ARG" > "$TEST_DIR/pass.zxc"

# 37.1 Plain input, from a file or stdin, comes out as is; without -f it is refused
set +e
"$ZXC_BIN" -dcf "$TEST_DIR/pass_plain.txt" > "$TEST_DIR/pass_file_out" 2>/dev/null
RC_FILE=$?
"$ZXC_BIN" -dcf < "$TEST_DIR/pass_plain.txt" > "$TEST_DIR/pass_stdin_out" 2>/dev/null
RC_STDIN=$?
"$ZXC_BIN" -dc < "$TEST_DIR/pass_plain.txt" > /dev/null 2>&1
RC_NOF=$?
set -e
if [[ $RC_FILE -eq 0 && $RC_STDIN -eq 0 && $RC_NOF -ne 0 ]] &&
   cmp -s "$TEST_DIR/pass_plain.txt" "$TEST_DIR/pass_file_out" &&
   cmp -s "$TEST_DIR/pass_plain.txt" "$TEST_DIR/pass_stdin_out"; then
    log_pass "-dcf copies plain input unchanged; -dc without -f rejects it"
else
    log_fail "-dcf pass-through failed (rc=$RC_FILE/$RC_STDIN, without -f: $RC_NOF)"
fi

# 37.2 An archive on stdin still decodes after the peek
if "$ZXC_BIN" -dcf < "$TEST_DIR/pass.zxc" 2>/dev/null | cmp -s - "$TEST_FILE"; then
    log_pass "-dcf decodes an archive read from stdin"
else
    log_fail "-dcf lost the start of an archive read from stdin"
fi

# 37.3 Empty stdin gives empty output and success
set +e
"$ZXC_BIN" -dcf < /dev/null > "$TEST_DIR/pass_empty_out" 2>/dev/null
RC=$?
set -e
if [[ $RC -eq 0 && ! -s "$TEST_DIR/pass_empty_out" ]]; then
    log_pass "-dcf on empty stdin writes nothing and succeeds"
else
    log_fail "-dcf on empty stdin must write nothing and succeed (rc=$RC)"
fi

# 37.4 A named file needs the whole magic word; stdin only its first byte (0xF5)
printf '\365not an archive\n' > "$TEST_DIR/pass_f5.bin"
set +e
"$ZXC_BIN" -dcf "$TEST_DIR/pass_f5.bin" > "$TEST_DIR/pass_f5_out" 2>/dev/null
RC_FILE=$?
"$ZXC_BIN" -dcf < "$TEST_DIR/pass_f5.bin" > /dev/null 2>&1
RC_STDIN=$?
set -e
if [[ $RC_FILE -eq 0 && $RC_STDIN -ne 0 ]] && cmp -s "$TEST_DIR/pass_f5.bin" "$TEST_DIR/pass_f5_out"; then
    log_pass "-dcf checks the whole magic word in a file, its first byte on stdin"
else
    log_fail "-dcf on input starting with 0xF5: file rc=$RC_FILE (want 0), stdin rc=$RC_STDIN (want != 0)"
fi

# 38. Outputs inherit the input's mode and mtime, as with gzip and zstd
echo "Testing metadata preservation..."
META="$TEST_DIR/meta"
IS_WINDOWS=0
case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;; esac
file_attrs() {  # "<mode> <mtime>"; Windows has no mode bits: mtime only. "missing" under set -e
    if [[ $IS_WINDOWS -eq 1 ]]; then
        stat -c '%Y' "$1" 2>/dev/null || echo missing
    else
        stat -c '%a %Y' "$1" 2>/dev/null || stat -f '%Lp %m' "$1" 2>/dev/null || echo missing
    fi
}
head -c 100000 "$TEST_FILE" > "$META"
chmod 600 "$META"
touch -t 202001011200 "$META"
WANT=$(file_attrs "$META")

# 38.1 Round trip: the archive, then the restored file, carry the original's
"$ZXC_BIN" -q "$META"
GOT_XC=$(file_attrs "$META.zxc")
"$ZXC_BIN" -d -q "$META.zxc"
GOT_DEC=$(file_attrs "$META")
if [[ "$GOT_XC" == "$WANT" && "$GOT_DEC" == "$WANT" ]]; then
    log_pass "Mode and mtime survive compression and decompression"
else
    log_fail "Metadata lost: want '$WANT', archive '$GOT_XC', restored '$GOT_DEC'"
fi

# 38.2 -f onto an existing, wider file still yields the input's mode
printf 'old\n' > "$META.zxc"
chmod 644 "$META.zxc"
"$ZXC_BIN" -q -k -f "$META"
GOT=$(file_attrs "$META.zxc")
if [[ "$GOT" == "$WANT" ]]; then
    log_pass "-f overwrite takes the input's mode and mtime"
else
    log_fail "-f overwrite: want '$WANT', got '$GOT'"
fi
rm -f "$META.zxc"

# 38.3 --no-preserve leaves the output's mtime current
"$ZXC_BIN" -q -k --no-preserve "$META"
GOT=$(file_attrs "$META.zxc")
if [[ "${GOT##* }" != "${WANT##* }" ]]; then
    log_pass "--no-preserve keeps a fresh mtime"
else
    log_fail "--no-preserve copied the input's mtime"
fi
rm -f "$META.zxc"

# 38.4 -m and -r keep each file's own attributes; --no-preserve reaches them too
META_DIR="$TEST_DIR/meta_tree"
mkdir -p "$META_DIR/sub"
head -c 50000 "$TEST_FILE" > "$META_DIR/a"
head -c 60000 "$TEST_FILE" > "$META_DIR/sub/b"
chmod 600 "$META_DIR/a"
chmod 640 "$META_DIR/sub/b"
touch -t 202001011200 "$META_DIR/a"
touch -t 201806150930 "$META_DIR/sub/b"
WANT_A=$(file_attrs "$META_DIR/a")
WANT_B=$(file_attrs "$META_DIR/sub/b")
"$ZXC_BIN" -q -m "$META_DIR/a" "$META_DIR/sub/b"
GOT_A=$(file_attrs "$META_DIR/a.zxc")
GOT_B=$(file_attrs "$META_DIR/sub/b.zxc")
"$ZXC_BIN" -d -q -r "$META_DIR"
GOT_DEC="$(file_attrs "$META_DIR/a") $(file_attrs "$META_DIR/sub/b")"
if [[ "$GOT_A $GOT_B" == "$WANT_A $WANT_B" && "$GOT_DEC" == "$WANT_A $WANT_B" ]]; then
    log_pass "-m and -r keep each file's mode and mtime"
else
    log_fail "-m/-r metadata: want '$WANT_A $WANT_B', archives '$GOT_A $GOT_B', restored '$GOT_DEC'"
fi
"$ZXC_BIN" -q -r -k --no-preserve "$META_DIR"
GOT_A=$(file_attrs "$META_DIR/a.zxc")
GOT_B=$(file_attrs "$META_DIR/sub/b.zxc")
if [[ "${GOT_A##* }" != "${WANT_A##* }" && "${GOT_B##* }" != "${WANT_B##* }" ]]; then
    log_pass "-r --no-preserve keeps fresh mtimes"
else
    log_fail "-r --no-preserve copied an input's mtime"
fi

if [[ $IS_WINDOWS -eq 1 ]]; then
    echo "  [SKIP] read-only and link outputs: POSIX only"
else
    # 38.5 A read-only output, inherited from a read-only input, is still replaced by -f
    META_RO="$TEST_DIR/meta_ro"
    head -c 50000 "$TEST_FILE" > "$META_RO"
    chmod 444 "$META_RO"
    set +e
    "$ZXC_BIN" -q -k "$META_RO" && "$ZXC_BIN" -q -k -f "$META_RO" &&
        "$ZXC_BIN" -q -d -k -f "$META_RO.zxc"
    RC=$?
    set -e
    GOT="$(file_attrs "$META_RO") $(file_attrs "$META_RO.zxc")"
    if [[ $RC -eq 0 && "${GOT%% *}" == "444" ]] && cmp -s "$META_RO" <(head -c 50000 "$TEST_FILE"); then
        log_pass "-f replaces read-only outputs"
    else
        log_fail "-f onto a read-only output: rc=$RC, attrs '$GOT'"
    fi

    # 38.6 A link at the output path is replaced, never written through
    META_LN="$TEST_DIR/meta_ln"
    head -c 50000 "$TEST_FILE" > "$META_LN"
    cp "$META_LN" "$META_LN.orig"
    ln -s "$TEST_DIR/meta_victim" "$META_LN.zxc"
    set +e
    "$ZXC_BIN" -q -k "$META_LN" 2>/dev/null
    RC_NOF=$?
    "$ZXC_BIN" -q -k -f "$META_LN"
    RC_F=$?
    set -e
    if [[ $RC_NOF -ne 0 && $RC_F -eq 0 && ! -e "$TEST_DIR/meta_victim" && ! -L "$META_LN.zxc" ]]; then
        log_pass "A dangling symlink output is refused, then replaced by -f"
    else
        log_fail "Symlink output: rc=$RC_NOF/$RC_F (want !=0/0), target created or link kept"
    fi
    rm -f "$META_LN.zxc"
    ln -s "$(basename "$META_LN")" "$META_LN.zxc"  # output links to the input
    "$ZXC_BIN" -q -f "$META_LN"
    if "$ZXC_BIN" -dc "$META_LN.zxc" | cmp -s - "$META_LN.orig"; then
        log_pass "An output symlinked to the input does not destroy it"
    else
        log_fail "Compressing onto a symlink to the input lost the data"
    fi
    # A link to a device (/dev/stdout -> fd/1) is written through and kept
    if [[ -L /dev/stdout ]]; then
        if "$ZXC_BIN" -q -f -o /dev/stdout "$META_LN.orig" | "$ZXC_BIN" -dc | cmp -s - "$META_LN.orig" &&
           [[ -L /dev/stdout ]]; then
            log_pass "-f -o /dev/stdout writes through the device link"
        else
            log_fail "-f -o /dev/stdout: output wrong or /dev/stdout replaced"
        fi
    fi
    cp "$META_LN.orig" "$META_LN"
    rm -f "$META_LN.zxc"
    ln "$META_LN" "$META_LN.zxc"  # output hard-linked to the input
    "$ZXC_BIN" -q -k -f "$META_LN"
    if cmp -s "$META_LN" "$META_LN.orig" && "$ZXC_BIN" -dc "$META_LN.zxc" | cmp -s - "$META_LN.orig"; then
        log_pass "An output hard-linked to the input does not overwrite it"
    else
        log_fail "Compressing onto a hard link to the input overwrote it"
    fi
fi

# 39. --favor-decode-speed: an encoder policy. Every 7th line repeats: 40-50 byte
# matches, the lengths the split re-emits inline.
echo "Testing --favor-decode-speed..."
PDS="$TEST_DIR/pds.txt"
: > "$PDS"
for i in $(seq 1 4000); do
    printf 'the quick brown fox jumps over the lazy dog, line %d of 7\n' $((i % 7)) >> "$PDS"
done
"$ZXC_BIN" -3 -c "$PDS" > "$TEST_DIR/pds_off.zxc"
"$ZXC_BIN" -3 --favor-decode-speed -c "$PDS" > "$TEST_DIR/pds_on.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/pds_on.zxc" > "$TEST_DIR/pds_on.dec"
if cmp -s "$PDS" "$TEST_DIR/pds_on.dec"; then
    log_pass "--favor-decode-speed round trip"
else
    log_fail "--favor-decode-speed round-trip failed"
fi
# 39.1 The level 3 archive differs
if ! cmp -s "$TEST_DIR/pds_off.zxc" "$TEST_DIR/pds_on.zxc"; then
    log_pass "--favor-decode-speed changes the level 3 archive"
else
    log_fail "--favor-decode-speed left the level 3 archive unchanged"
fi
# 39.2 Levels 1-2 (PAK) never split
"$ZXC_BIN" -1 -c "$PDS" > "$TEST_DIR/pds_off1.zxc"
"$ZXC_BIN" -1 --favor-decode-speed -c "$PDS" > "$TEST_DIR/pds_on1.zxc"
if cmp -s "$TEST_DIR/pds_off1.zxc" "$TEST_DIR/pds_on1.zxc"; then
    log_pass "--favor-decode-speed has no effect at level 1"
else
    log_fail "--favor-decode-speed changed the level 1 archive"
fi
# 39.3 Benchmark mode reports it
if "$ZXC_BIN" -b1 -j --favor-decode-speed "$TEST_FILE_ARG" 2>/dev/null | grep -q '"favor_decode_speed": true'; then
    log_pass "-b -j reports favor_decode_speed"
else
    log_fail "-b -j does not report favor_decode_speed"
fi
# 39.4 =PCT widens the budget: a different archive that still decodes
"$ZXC_BIN" -3 --favor-decode-speed=100 -c "$PDS" > "$TEST_DIR/pds_on100.zxc"
"$ZXC_BIN" -d -c "$TEST_DIR/pds_on100.zxc" > "$TEST_DIR/pds_on100.dec"
if cmp -s "$PDS" "$TEST_DIR/pds_on100.dec" && ! cmp -s "$TEST_DIR/pds_on.zxc" "$TEST_DIR/pds_on100.zxc"; then
    log_pass "--favor-decode-speed=100 round trip, differs from the default budget"
else
    log_fail "--favor-decode-speed=100 round trip or budget ignored"
fi
# 39.5 The budget is 1 to 100
if ! "$ZXC_BIN" -3 --favor-decode-speed=0 -c "$PDS" > /dev/null 2>&1 && \
   ! "$ZXC_BIN" -3 --favor-decode-speed=101 -c "$PDS" > /dev/null 2>&1 && \
   ! "$ZXC_BIN" -3 --favor-decode-speed=x -c "$PDS" > /dev/null 2>&1; then
    log_pass "--favor-decode-speed rejects a budget outside 1-100"
else
    log_fail "--favor-decode-speed accepted a bad budget"
fi
# 39.6 Benchmark mode reports the budget
if "$ZXC_BIN" -b1 -j --favor-decode-speed=25 "$TEST_FILE_ARG" 2>/dev/null | grep -q '"split_budget_pct": 25'; then
    log_pass "-b -j reports split_budget_pct"
else
    log_fail "-b -j does not report split_budget_pct"
fi
rm -f "$TEST_DIR"/pds_*

echo "All tests passed!"
exit 0
