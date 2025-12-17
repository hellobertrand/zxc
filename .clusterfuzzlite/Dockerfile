FROM gcr.io/oss-fuzz-base/base-builder:v1

RUN apt-get update && apt-get install -y make cmake

COPY . $SRC/zxc
WORKDIR $SRC/zxc

COPY .clusterfuzzlite/build.sh $SRC/build.sh