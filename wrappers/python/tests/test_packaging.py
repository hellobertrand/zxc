# SPDX-License-Identifier: BSD-3-Clause
"""
ZXC - High-performance lossless compression

Copyright (c) Bertrand Lebonnois and contributors.

What an installed distribution must carry.
"""

import ast
import importlib.machinery
import importlib.metadata
import pathlib

import zxc
from zxc import _zxc

DIST = importlib.metadata.distribution("zxc-compress")


def test_version_matches_the_distribution():
    assert zxc.__version__ == DIST.version


def test_license_is_shipped():
    assert any(f.name == "LICENSE" for f in DIST.files)


def test_c_source_is_not_shipped():
    assert not any(f.suffix == ".c" for f in DIST.files)


def test_requires_python_is_declared():
    assert DIST.metadata["Requires-Python"]


def test_extension_carries_the_abi_tag():
    assert _zxc.__file__.endswith(importlib.machinery.EXTENSION_SUFFIXES[0])


def _stub_function(name, cls=None):
    tree = ast.parse(pathlib.Path(zxc.__file__).with_suffix(".pyi").read_text())
    if cls:
        tree = next(
            n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == cls
        )
    return next(
        n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == name
    )


def test_stub_stream_decompress_takes_a_dictionary():
    args = _stub_function("stream_decompress").args.args
    annotation = next(a.annotation for a in args if a.arg == "dict")
    assert "Dictionary" in ast.unparse(annotation)


def test_stub_zxc_reader_takes_multistream():
    args = _stub_function("__init__", "ZxcReader").args.kwonlyargs
    assert "multistream" in [a.arg for a in args]
