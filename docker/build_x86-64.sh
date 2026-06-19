#!/bin/bash
set -e

BUILD_TYPE="${1:-Release}"   # Default = Release if no argument

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="Build-x86-64-linux"

JOBS=$(nproc 2>/dev/null || echo 4)

docker run --rm -v "$PROJECT_ROOT":/src -w /src pixelmapper-x86-64-linux \
    bash -c "mkdir -p $BUILD_DIR && cd $BUILD_DIR && \
             cmake .. \
               -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
               -DCMAKE_C_COMPILER=clang \
               -DCMAKE_CXX_COMPILER=clang++ && \
             make -j$JOBS && cpack"