#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:eb77f5af37831bde58f992e15783ff23f04ee81af67962b7e0aaa7e1e26a7662

COPY src $SRC/zxc/src
COPY include $SRC/zxc/include
COPY tests $SRC/zxc/tests
COPY .clusterfuzzlite/build.sh $SRC/build.sh

WORKDIR $SRC/zxc