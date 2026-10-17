#!/usr/bin/env bash
set -euo pipefail

# The same pinned headless OCCT source and toolkit closure as build-occt.ps1.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root=${ARK_BUILD_ROOT:-"$root/out"}
jobs=${ARK_BUILD_JOBS:-4}
configure_only=false
allow_candidate=false
while (($#)); do
    case "$1" in
        --jobs) jobs=${2:?Missing --jobs value}; shift 2 ;;
        --configure-only) configure_only=true; shift ;;
        --allow-candidate-patchset) allow_candidate=true; shift ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
[[ $jobs =~ ^[1-9][0-9]*$ && $jobs -le 64 ]] || { echo 'Jobs must be 1..64.' >&2; exit 2; }
deps="$build_root/deps/occt"
cache="$deps/cache"
archive="$cache/OCCT-V8_0_1.zip"
patchset=ark-offset-001
source="$deps/source-$patchset"
build="$deps/build-linux-$patchset"
install="$deps/install-linux-$patchset"
expected_sha256=7c033d917ee8f040c0512d289dcc5f02c148889d5bac17c3e25639accb44f0da
cmake -DARK_OCCT_MODE=BUILD_POLICY -DARK_OCCT_EXPECTED_PATCHSET="$patchset" -DARK_OCCT_EXPECTED_ARCHIVE_SHA256="$expected_sha256" -DARK_OCCT_ALLOW_CANDIDATE="$allow_candidate" -P "$root/scripts/ApplyOcctPatch.cmake"
mkdir -p -- "$cache"
if [[ ! -f $archive ]]; then
    curl -fL --retry 3 -o "$archive.part" https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.zip
    mv -- "$archive.part" "$archive"
fi
printf '%s  %s\n' "$expected_sha256" "$archive" | sha256sum --check --status || { echo 'OCCT archive SHA-256 mismatch.' >&2; exit 1; }
if [[ ! -e $source ]]; then
    stage=$(mktemp -d "$deps/.occt-source-XXXXXXXX")
    unzip -oq "$archive" -d "$stage"
    [[ -f $stage/OCCT-8_0_1/src/MODULES.cmake ]] || { echo 'OCCT source tree is incomplete.' >&2; exit 1; }
    mv -T --no-clobber -- "$stage/OCCT-8_0_1" "$source"
    printf '%s' "$expected_sha256" > "$source/.ark-source-sha256"
fi
[[ -f $source/.ark-source-sha256 && $(cat "$source/.ark-source-sha256") == "$expected_sha256" ]] || { echo 'Separate OCCT source archive stamp differs; preserve it and use a fresh dependency directory.' >&2; exit 1; }
cmake -DARK_OCCT_SOURCE="$source" -P "$root/scripts/ApplyOcctPatch.cmake"
cmake -S "$source" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DINSTALL_DIR="$install" '-DCMAKE_INSTALL_RPATH=$ORIGIN' \
    -DBUILD_MODULE_FoundationClasses=ON -DBUILD_MODULE_ModelingData=ON \
    -DBUILD_MODULE_ModelingAlgorithms=ON -DBUILD_MODULE_ApplicationFramework=OFF \
    -DBUILD_MODULE_Visualization=OFF -DBUILD_MODULE_DataExchange=OFF -DBUILD_MODULE_Draw=OFF \
    '-DBUILD_ADDITIONAL_TOOLKITS=TKDESTEP;TKDEIGES' -DBUILD_LIBRARY_TYPE=Shared \
    -DBUILD_USE_PCH=ON -DBUILD_USE_VCPKG=OFF -DUSE_FREETYPE=OFF -DUSE_OPENGL=OFF \
    -DUSE_TK=OFF -DUSE_TBB=OFF -DUSE_RAPIDJSON=OFF -DUSE_XLIB=OFF \
    -DINSTALL_DOC_Overview=OFF -DINSTALL_TEST_CASES=OFF
$configure_only && exit 0
cmake --build "$build" --parallel "$jobs"
cmake --install "$build"
[[ -f $install/lib/cmake/opencascade/OpenCASCADEConfig.cmake && -f $install/lib/libTKDESTEP.so && -f $install/lib/libTKDEIGES.so ]] || { echo 'Installed OCCT SDK is incomplete.' >&2; exit 1; }
mkdir -p -- "$install/licenses"
cp -- "$source/LICENSE_LGPL_21.txt" "$source/OCCT_LGPL_EXCEPTION.txt" "$install/licenses/"
cp -- "$source/.ark-occt-patchset.json" "$install/"
cmake -DARK_OCCT_MODE=RECORD_BUILD -DARK_OCCT_BUILD_KIND=clean-source-rebuild -DARK_OCCT_ALLOW_CANDIDATE="$allow_candidate" -DARK_OCCT_INSTALL="$install" -P "$root/scripts/ApplyOcctPatch.cmake"
cmake -DARK_OCCT_MODE=NOTICE -DARK_OCCT_ALLOW_CANDIDATE="$allow_candidate" -DARK_OCCT_INSTALL="$install" -DARK_OCCT_NOTICE="$install/licenses/OCCT-ARK-patches.txt" -P "$root/scripts/ApplyOcctPatch.cmake"
printf 'OCCT_DIR=%s\n' "$install/lib/cmake/opencascade"
