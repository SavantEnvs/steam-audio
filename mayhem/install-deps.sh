#!/bin/sh
# Builds steam-audio's external dependencies (core/build/dependencies.json pins) once, at image build
# time, into /opt/toolchains/deps/<name>-<rev>/ (SPEC §6.2 item 19). Run as root by mayhem/Dockerfile.
# Dependencies stay uninstrumented (-O2 -g -gdwarf-3); no .so is kept.
set -eux
fetch() { mkdir -p "$2"; git -C "$2" init -q; git -C "$2" fetch -q --depth 1 "$1" "$3"; git -C "$2" checkout -q FETCH_HEAD; rm -rf "$2/.git"; }
DBG="-O2 -g -gdwarf-3"
J=$(nproc)

# flatbuffers 1.12 (flatc + headers). This old flatc does not compile with clang 19 (deleted
# operator= in vector_downward), so the tool itself is built with gcc.
d=/opt/toolchains/deps/flatbuffers-6df40a2
fetch https://github.com/google/flatbuffers.git "$d/src" 6df40a2471737b27271bdd9b900ab5f3aec746c7
# flatbuffers.h's TableKeyComparator has a private copy-assignment that assigns through a reference
# member; clang 19 rejects it ("selected deleted operator '='") in every TU that includes the header.
# Later flatbuffers deletes that operator; do the same here (it is never called).
python3 - "$d/src/include/flatbuffers/flatbuffers.h" <<'PY'
import sys
p = sys.argv[1]; s = open(p).read()
old = """    TableKeyComparator &operator=(const TableKeyComparator &other) {
      buf_ = other.buf_;
      return *this;
    }"""
assert old in s
open(p, "w").write(s.replace(old, "    TableKeyComparator &operator=(const TableKeyComparator &other) = delete;"))
PY
cmake -S "$d/src" -B /tmp/b-fb -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLATBUFFERS_BUILD_FLATLIB=OFF \
  -DFLATBUFFERS_BUILD_FLATHASH=OFF -DFLATBUFFERS_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$d" \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_FLAGS="-w"
cmake --build /tmp/b-fb -j"$J"
cmake --install /tmp/b-fb

# pffft
d=/opt/toolchains/deps/pffft-e0bf595
fetch https://github.com/marton78/pffft.git "$d/src" e0bf595c98ded55cc457a371c1b29c8cab552628
cmake -S "$d/src" -B /tmp/b-pffft -G Ninja -DCMAKE_BUILD_TYPE=Release -DPFFFT_USE_TYPE_DOUBLE=OFF \
  -DPFFFT_USE_FFTPACK=OFF -DPFFFT_USE_BENCH_GREEN=OFF -DPFFFT_USE_BENCH_KISS=OFF -DPFFFT_USE_BENCH_POCKET=OFF \
  -DCMAKE_C_FLAGS="$DBG -fPIC" -DCMAKE_CXX_FLAGS="$DBG -fPIC"
cmake --build /tmp/b-pffft -j"$J" --target PFFFT
mkdir -p "$d/include" "$d/lib"
cp "$d/src/pffft.h" "$d/include/"
cp "$(find /tmp/b-pffft -name "lib*.a" | head -1)" "$d/lib/libpffft.a"

# libmysofa (SOFA HRTF reader)
d=/opt/toolchains/deps/mysofa-dd315a8
fetch https://github.com/hoene/libmysofa.git "$d/src" dd315a8ec1fee7193d40e4a59b12c5590a4a918c
cmake -S "$d/src" -B /tmp/b-sofa -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF \
  -DCMAKE_C_FLAGS="$DBG -fPIC" -DCMAKE_CXX_FLAGS="$DBG -fPIC" -DCMAKE_INSTALL_PREFIX="$d"
cmake --build /tmp/b-sofa -j"$J"
cmake --install /tmp/b-sofa

# Catch2 v2 single header (upstream unit tests, core/src/test)
d=/opt/toolchains/deps/catch-3b18d9e
fetch https://github.com/catchorg/Catch2.git "$d/src" 3b18d9e962835100d7e12ac80d22882e408e40dc
mkdir -p "$d/include"
cp "$(find "$d/src/single_include" -name catch.hpp | head -1)" "$d/include/"

rm -rf /tmp/b-*
find /opt/toolchains/deps -name '*.so*' -delete
chmod -R a+rX,go-w /opt/toolchains/deps
