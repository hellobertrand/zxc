#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:8b4a73d83374b298a0a771eeec9a1d44ceff3416a678b7d7946303389d022aca

COPY src $SRC/zxc/src
COPY include $SRC/zxc/include
COPY tests $SRC/zxc/tests
COPY .clusterfuzzlite/build.sh $SRC/build.sh

WORKDIR $SRC/zxc