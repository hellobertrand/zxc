#!/bin/sh
# Copies the C core into the module: a module zip holds nothing above its directory.
set -eu
mod=$(cd "$(dirname "$0")/.." && pwd)
root=$mod/../..
rm -rf "$mod/zxc-core"
mkdir -p "$mod/zxc-core/src"
cp -R "$root/include" "$mod/zxc-core/include"
cp -R "$root/src/lib" "$mod/zxc-core/src/lib"
