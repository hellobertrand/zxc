#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:fd6ca3a7a6b8e1dcbdd4f03cfdc0fc2603c7a1097aed1d730de2ca4124e0a405

COPY . $SRC/zxc
WORKDIR $SRC/zxc

COPY .clusterfuzzlite/build.sh $SRC/build.sh