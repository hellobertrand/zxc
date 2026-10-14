# SPDX-License-Identifier: BSD-3-Clause
#  Copyright (c) Bertrand Lebonnois and contributors.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FROM gcr.io/oss-fuzz-base/base-builder:v1@sha256:1f9cf74bc3d7029b528494542f9d9851b256dceed38d3f420989434dc5ae0760

COPY src $SRC/zxc/src
COPY include $SRC/zxc/include
COPY tests $SRC/zxc/tests
COPY .clusterfuzzlite/build.sh $SRC/build.sh

WORKDIR $SRC/zxc