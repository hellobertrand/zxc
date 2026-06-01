#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:7b8357d631519f5b1ecb06eff2d7bb7985ba7b512a52688b46214142e1c22ed4

COPY . $SRC/zxc
WORKDIR $SRC/zxc

COPY .clusterfuzzlite/build.sh $SRC/build.sh