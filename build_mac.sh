#!/bin/sh
# Build APImageIO.plugin for macOS (universal: arm64 + x86_64).
#
#   VCPKG_INSTALLED=<dir> sh build_mac.sh
#
# <dir> holds one vcpkg install root per architecture, as the CI workflow
# makes them:
#   <dir>/arm64/arm64-osx-lw/{include,lib}
#   <dir>/x64/x64-osx-lw/{include,lib}
# (vcpkg install --x-manifest-root=mac --overlay-triplets=mac/triplets
#  --triplet <arch>-osx-lw --x-install-root=<dir>/<arch>)
#
# Every dependency is a static archive, and the bundle exports ONLY
# _mod_descrip, so the OpenImageIO / OpenColorIO / OpenEXR compiled in here
# can't collide with copies LightWave itself loads.
#
# Mirrors the SDK's samples.xcconfig: a .plugin bundle (Mach-O bundle), macOS
# 11.0+, _MACOS / LW_OSX / MAC_OS_X defines.
set -eu
cd "$(dirname "$0")"

NAME=APImageIO
OUT=build/mac
BUNDLE="$OUT/$NAME.plugin"
VERSION="$(sed -n 's/^#define APIMAGEIO_VERSION "\(.*\)".*/\1/p' src/apimageio_version.h)"
VCPKG_INSTALLED="${VCPKG_INSTALLED:-build/vcpkg}"

MIN="-mmacosx-version-min=11.0"
DEFS="-D_MACOS -DLW_OSX -DMAC_OS_X=1 -DNDEBUG"
WARN="-Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable \
      -Wno-unused-but-set-variable -Wno-sign-compare -Wno-missing-field-initializers \
      -Wno-deprecated-declarations -Wno-unknown-pragmas -Wno-visibility \
      -Wno-missing-braces -Wno-pointer-sign"

C_SRC="src/apimageio_module.c src/ap_prefs.c src/ap_report.c src/load_image_cmd.c
       third_party/apprefs/apprefs.c src/exr_loader.c src/webp_loader.c src/webp_saver.c
       src/webpbridge.c src/avif_loader.c src/avif_saver.c src/avifbridge.c src/svg_loader.c
       src/png_loader.c src/pngbridge.c src/kra_loader.c src/krabridge.c src/oiio_loader.c
       src/oiio_saver.c src/exr_session.c src/exr_multilayer.c src/crypto_encode.c
       src/crypto_accum.c src/crypto_matte.c"
CXX_SRC="src/exrbridge.cpp src/svgbridge.cpp src/oiiobridge.cpp"

# build_arch <vcpkg-arch-dir> <triplet> <clang -arch>
build_arch() {
    inst="$VCPKG_INSTALLED/$1/$2"
    arch="$3"
    obj="$OUT/obj-$arch"
    [ -d "$inst/lib" ] || { echo "ERROR: no vcpkg install at $inst"; exit 1; }
    rm -rf "$obj"; mkdir -p "$obj"
    inc="-Isdk/include -Ithird_party/apprefs -Isrc -I$inst/include -I$inst/include/OpenEXR -I$inst/include/Imath"
    common="-arch $arch $MIN -O2 -fPIC -fvisibility=hidden $DEFS $inc $WARN"

    echo "== Compiling ($arch)"
    objs=""
    for f in $C_SRC; do
        o="$obj/$(basename "$f" .c).o"
        # shellcheck disable=SC2086
        clang $common -std=gnu11 -c "$f" -o "$o"
        objs="$objs $o"
    done
    for f in $CXX_SRC; do
        o="$obj/$(basename "$f" .cpp).o"
        # shellcheck disable=SC2086
        clang++ $common -std=c++17 -c "$f" -o "$o"
        objs="$objs $o"
    done

    echo "== Linking ($arch)"
    # All of the triplet's static archives: ld64 pulls only the members it
    # needs and searches archives order-independently, so the Windows link
    # list doesn't have to be re-derived by hand. Duplicate definitions across
    # archives (e.g. lodepng's C and C++ builds) are harmless unless both
    # members get pulled, which the linker would report.
    # shellcheck disable=SC2086
    clang++ -arch "$arch" $MIN -bundle -o "$obj/$NAME" $objs "$inst"/lib/*.a \
        -framework CoreFoundation -framework CoreServices -framework Security \
        -liconv -lz \
        -Wl,-exported_symbol,__mod_descrip -Wl,-dead_strip
}

rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/Contents/MacOS" "$BUNDLE/Contents/Resources"

build_arch arm64 arm64-osx-lw arm64
build_arch x64   x64-osx-lw   x86_64

lipo -create "$OUT/obj-arm64/$NAME" "$OUT/obj-x86_64/$NAME" -output "$BUNDLE/Contents/MacOS/$NAME"
sed "s/@VERSION@/$VERSION/g" mac/Info.plist.in > "$BUNDLE/Contents/Info.plist"

# Ad-hoc signature (arm64 code must be signed to load; a Developer ID
# signature + notarisation replaces this for distribution).
codesign --force --sign - "$BUNDLE"

echo "== Checks"
lipo -info "$BUNDLE/Contents/MacOS/$NAME"
nm -gU "$BUNDLE/Contents/MacOS/$NAME" | grep -q " __mod_descrip$" \
    || { echo "ERROR: _mod_descrip is not exported"; exit 1; }
echo "exported:"; nm -gU "$BUNDLE/Contents/MacOS/$NAME"
echo "dylibs:";   otool -L "$BUNDLE/Contents/MacOS/$NAME"
codesign --verify --verbose "$BUNDLE"
ls -lh "$BUNDLE/Contents/MacOS/$NAME"

# LightWave dlopens with RTLD_NOW: every symbol must resolve. The plugin only
# reaches LightWave through the GlobalFunc it is handed, so a plain dlopen
# outside LightWave must succeed -- and runs all the static initialisers of
# the bundled OIIO/OCIO/OpenEXR (the Windows EXE-only init crash must not have
# a macOS counterpart).
cat > "$OUT/dlopen_smoke.c" <<'EOF'
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("dlopen FAILED: %s\n", dlerror()); return 1; }
    if (!dlsym(h, "_mod_descrip")) { printf("no _mod_descrip\n"); return 1; }
    printf("dlopen OK, _mod_descrip found\n");
    dlclose(h);
    return 0;
}
EOF
clang -arch arm64 -arch x86_64 $MIN -o "$OUT/dlopen_smoke" "$OUT/dlopen_smoke.c"
arch -arm64 "$OUT/dlopen_smoke" "$BUNDLE/Contents/MacOS/$NAME"
# x86_64 slice: only where Rosetta is installed (not guaranteed on CI runners)
if arch -x86_64 /usr/bin/true 2>/dev/null; then
    arch -x86_64 "$OUT/dlopen_smoke" "$BUNDLE/Contents/MacOS/$NAME"
else
    echo "(x86_64 slice not load-tested: no Rosetta on this machine)"
fi

echo "== Packaging"
rm -f "$OUT/$NAME-mac.zip"
ditto -c -k --keepParent "$BUNDLE" "$OUT/$NAME-mac.zip"
echo "Built $OUT/$NAME-mac.zip ($VERSION)"
