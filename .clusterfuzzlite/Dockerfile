#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:b2ac08d613b128064d77f5622ac363cbf6818329be6d3f1852a9728a998abd0a

COPY . $SRC/zxc
WORKDIR $SRC/zxc

COPY .clusterfuzzlite/build.sh $SRC/build.sh