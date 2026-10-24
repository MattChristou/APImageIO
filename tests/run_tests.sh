#!/bin/sh
# APImageIO regression harness, macOS counterpart of run_tests.bat: generate
# the test files, build qa_harness against the host architecture's vcpkg
# install, run it. Needs python3 with: pip install OpenEXR numpy pypng
#
#   VCPKG_INSTALLED=<dir> sh tests/run_tests.sh
#
# Exit code = number of failed checks (0 = all passed); 90-92 = setup failure.
set -u
cd "$(dirname "$0")"

case "$(uname -m)" in
    arm64)  INST="${VCPKG_INSTALLED:-../build/vcpkg}/arm64/arm64-osx-lw" ;;
    *)      INST="${VCPKG_INSTALLED:-../build/vcpkg}/x64/x64-osx-lw" ;;
esac
case "$INST" in /*) ;; *) INST="$(cd .. && pwd)/${INST#../}" ;; esac
SRC=../src

python3 mk_kra.py || exit 90
python3 mk_exr.py || exit 91

mkdir -p build/obj
FLAGS="-O2 -mmacosx-version-min=11.0 -I$SRC -I$INST/include -I$INST/include/OpenEXR -I$INST/include/Imath -w"
{
    for f in qa_harness.c $SRC/krabridge.c $SRC/crypto_encode.c $SRC/exr_session.c; do
        clang $FLAGS -std=gnu11 -c "$f" -o "build/obj/$(basename "$f" .c).o" || exit 92
    done
    for f in $SRC/exrbridge.cpp $SRC/svgbridge.cpp; do
        clang++ $FLAGS -std=c++17 -c "$f" -o "build/obj/$(basename "$f" .cpp).o" || exit 92
    done
    # shellcheck disable=SC2086
    clang++ -mmacosx-version-min=11.0 -o build/qa_harness build/obj/*.o "$INST"/lib/*.a \
        -framework CoreFoundation -framework Security -liconv -lz || exit 92
} > build/build.log 2>&1 || { cat build/build.log; exit 92; }

# OpenEXR prints its own diagnostics for the deliberately corrupt file.
./build/qa_harness 2>/dev/null
exit $?
