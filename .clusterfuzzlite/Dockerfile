#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:33f90392168663c1b55ca3daf82089a619db54c1b9a43121198e50271c4ed266

COPY src $SRC/zxc/src
COPY include $SRC/zxc/include
COPY tests $SRC/zxc/tests
COPY .clusterfuzzlite/build.sh $SRC/build.sh

WORKDIR $SRC/zxc