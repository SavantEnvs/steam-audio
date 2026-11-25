#!/usr/bin/env bash
# mayhem/build.sh — builds steam-audio's core library (libphonon) twice and the fuzz harnesses.
#   1) mayhem/build-fuzz: libphonon.a with $SANITIZER_FLAGS $DEBUG_FLAGS (asserts kept), then every
#      harness in mayhem/harnesses/ as a libFuzzer binary + a -standalone reproducer in /mayhem.
#   2) mayhem/build-test: upstream's Catch unit tests (phonon_test) with normal flags, for test.sh.
# Dependencies (flatbuffers/flatc, pffft, libmysofa, Catch) are prebuilt in /opt/toolchains/deps by
# mayhem/install-deps.sh (libmysofa is rebuilt instrumented from its source there for the fuzzers); zlib is the base image's. Re-runnable and offline.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

SRC="${SRC:-/mayhem}"
cd "$SRC"
DEPS=/opt/toolchains/deps
FB=$DEPS/flatbuffers-6df40a2
PFFFT=$DEPS/pffft-e0bf595
SOFA=$DEPS/mysofa-dd315a8
CATCH=$DEPS/catch-3b18d9e

# Three UBSan checks are relaxed (only these; ASan and the rest of UBSan stay halting), each because it
# fires on every input through a benign upstream pattern and would hide everything behind it:
#   alignment          the FlatBuffers loaders read 3-band Material structs through a 4-byte-aligned
#                      `const Material* const*` view (core/src/core/static_mesh.cpp, materials()->data()),
#                      on every valid serialized scene / static mesh.
#   nonnull-attribute  Array<T>::zero() memsets a zero-length (null) array (core/src/core/array.h:235),
#                      e.g. ReverbEffect's Allpass delays, on every reflection-effect creation.
#   bool               ReflectionSimulationState::impulseResponseUpdated, a std::atomic<bool> that is
#                      never initialised (core/src/core/simulation_data.h), is read on every
#                      reflection simulation (simulation_manager.cpp:246).
# The library and harnesses get libFuzzer's coverage instrumentation (fuzzer-no-link) so the fuzzer
# sees edges in libphonon, not only in the harness; the fuzzer runtime is linked in per binary below.
FUZZ_FLAGS="$SANITIZER_FLAGS -fno-sanitize=alignment,nonnull-attribute,bool -fsanitize=fuzzer-no-link $DEBUG_FLAGS"

CMAKE_COMMON=(
  -S core -G Ninja
  -DBUILD_SHARED_LIBS=OFF
  -DSTEAMAUDIO_BUILD_ITESTS=OFF -DSTEAMAUDIO_BUILD_BENCHMARKS=OFF -DSTEAMAUDIO_BUILD_SAMPLES=OFF
  -DSTEAMAUDIO_BUILD_DOCS=OFF -DSTEAMAUDIO_ENABLE_IPP=OFF -DSTEAMAUDIO_ENABLE_EMBREE=OFF
  -DSTEAMAUDIO_ENABLE_RADEONRAYS=OFF -DSTEAMAUDIO_ENABLE_TRUEAUDIONEXT=OFF -DSTEAMAUDIO_ENABLE_AVX=OFF
  -DSTEAMAUDIO_ENABLE_MKL=OFF -DSTEAMAUDIO_ENABLE_FFTS=OFF
  -DFlatBuffers_INCLUDE_DIR="$FB/include" -DFlatBuffers_EXECUTABLE="$FB/bin/flatc"
  -DPFFFT_INCLUDE_DIR="$PFFFT/include" -DPFFFT_LIBRARY="$PFFFT/lib/libpffft.a"
  -DMySOFA_INCLUDE_DIR="$SOFA/include" -DMySOFA_LIBRARY="$SOFA/lib/libmysofa.a"
  -DZLIB_USE_STATIC_LIBS=ON -DCMAKE_PROJECT_INCLUDE="$SRC/mayhem/zlib-target.cmake"
  -DCatch_INCLUDE_DIR="$CATCH/include"
)

# 1) Sanitized library. The project adds a plain `-g` after our flags; -gdwarf-3 still fixes the version.
cmake -B mayhem/build-fuzz "${CMAKE_COMMON[@]}" -DSTEAMAUDIO_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE= \
  -DCMAKE_C_FLAGS="-O1 $FUZZ_FLAGS" \
  -DCMAKE_CXX_FLAGS="-O1 $FUZZ_FLAGS"
cmake --build mayhem/build-fuzz -j"$MAYHEM_JOBS" --target phonon

# libmysofa is the parser fuzz_hrtf_sofa targets (SOFA/HDF5), so the fuzz binaries link a copy built
# here from its pinned in-image source with the same sanitizer + coverage flags, not the uninstrumented
# prebuilt $SOFA/lib/libmysofa.a (which only the unit-test build below uses). zlib stays uninstrumented.
cmake -S "$SOFA/src" -B mayhem/build-sofa -G Ninja -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
  -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE= -DCMAKE_C_FLAGS="-O1 $FUZZ_FLAGS" -DCMAKE_CXX_FLAGS="-O1 $FUZZ_FLAGS"
cmake --build mayhem/build-sofa -j"$MAYHEM_JOBS" --target mysofa-static
SOFA_FUZZ_LIB=$(find mayhem/build-sofa -name libmysofa.a | head -1)
[ -n "$SOFA_FUZZ_LIB" ] || { echo "instrumented libmysofa.a not built" >&2; exit 1; }

LIBS=(mayhem/build-fuzz/src/core/libphonon.a "$PFFFT/lib/libpffft.a" "$SOFA_FUZZ_LIB"
      /usr/lib/x86_64-linux-gnu/libz.a -lpthread -ldl -lm)
INC=(-Icore/src/core -Imayhem/build-fuzz/src/core -Imayhem/harnesses)

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o mayhem/build-fuzz/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o mayhem/build-fuzz/standalone_main.o

for h in mayhem/harnesses/fuzz_*.cpp; do
  name=$(basename "$h" .cpp)
  $CXX -std=c++14 -O1 $FUZZ_FLAGS "${INC[@]}" -c "$h" -o "mayhem/build-fuzz/$name.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "mayhem/build-fuzz/$name.o" \
    mayhem/build-fuzz/lsan_off.o "${LIBS[@]}" -o "/mayhem/$name"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "mayhem/build-fuzz/$name.o" mayhem/build-fuzz/standalone_main.o \
    mayhem/build-fuzz/lsan_off.o "${LIBS[@]}" -o "/mayhem/$name-standalone"
done

# 2) Upstream unit tests with the project's normal flags (no sanitizers), run by mayhem/test.sh.
cmake -B mayhem/build-test "${CMAKE_COMMON[@]}" -DSTEAMAUDIO_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$COVERAGE_FLAGS"
cmake --build mayhem/build-test -j"$MAYHEM_JOBS" --target phonon_test
