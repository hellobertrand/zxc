#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1

RUN apt-get update && apt-get install -y make cmake

COPY . $SRC/zxc
WORKDIR $SRC/zxc

COPY .clusterfuzzlite/build.sh $SRC/build.sh