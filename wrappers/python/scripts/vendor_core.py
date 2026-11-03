#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Copy the C core into zxc-core/ so an sdist built next builds on its own."""

import pathlib
import shutil

PKG = pathlib.Path(__file__).resolve().parent.parent
REPO = PKG.parent.parent
CORE = PKG / "zxc-core"

# A subproject build turns the CLI, the tests and the install rules off
# (cmake/zxcOptions.cmake), so the core needs nothing beyond these.
FILES = ["CMakeLists.txt"]
DIRS = ["cmake", "include", "src/lib"]

shutil.rmtree(CORE, ignore_errors=True)
for rel in FILES:
    (CORE / rel).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO / rel, CORE / rel)
for rel in DIRS:
    shutil.copytree(REPO / rel, CORE / rel)
