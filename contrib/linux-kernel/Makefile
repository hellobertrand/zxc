# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause

# Builds zxc_selftest.ko against a kernel tree: `make`, or `make KDIR=<tree>`
# (BUILD=<dir> stages elsewhere). Kbuild compiles what sits under one directory,
# so the sources are staged into $(BUILD) first, this zxc_deps.h over the stock one.
KDIR  ?= /lib/modules/$(shell uname -r)/build
BUILD ?= $(CURDIR)/build
ROOT  := $(abspath $(CURDIR)/../..)
UNITS := zxc_common zxc_pivco_tables zxc_dispatch zxc_dict zxc_compress zxc_decompress zxc_huffman

.PHONY: all stage clean

all: stage
	$(MAKE) -C $(KDIR) M=$(abspath $(BUILD)) modules

stage:
	rm -rf $(BUILD)
	mkdir -p $(BUILD)/zxc/src/lib $(BUILD)/shim
	cp -r $(ROOT)/include $(BUILD)/zxc/
	cp $(addprefix $(ROOT)/src/lib/,$(addsuffix .c,$(UNITS))) $(ROOT)/src/lib/*.h $(BUILD)/zxc/src/lib/
	cp -r $(ROOT)/src/lib/vendors $(BUILD)/zxc/src/lib/
	cp zxc_deps.h $(BUILD)/zxc/src/lib/
	cp shim/*.h $(BUILD)/shim/
	cp Kbuild selftest.c $(BUILD)/

clean:
	rm -rf $(BUILD)
