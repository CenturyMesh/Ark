#!/usr/bin/env bash
set -euo pipefail

# Linux CLI, MCP and native OCCT worker. The Win32 Studio is built by build.ps1.
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root=${ARK_BUILD_ROOT:-"$root/out"}
jobs=${ARK_BUILD_JOBS:-4}
native=ON
test=false
configure_only=false
while (($#)); do
    case "$1" in
        --jobs) jobs=${2:?Missing --jobs value}; shift 2 ;;
        --core-only) native=OFF; shift ;;
        --test) test=true; shift ;;
        --configure-only) configure_only=true; shift ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
[[ $jobs =~ ^[1-9][0-9]*$ && $jobs -le 64 ]] || { echo 'Jobs must be 1..64.' >&2; exit 2; }
[[ $(uname -s) == Linux ]] || { echo 'Use scripts/build.ps1 on Windows.' >&2; exit 2; }
configure=(-S "$root" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DARK_WITH_OCCT=$native")
if [[ $native == ON ]]; then
    build="$build_root/linux-native-release"
    occt_dir=${OpenCASCADE_DIR:-"$build_root/deps/occt/install-linux/lib/cmake/opencascade"}
    if [[ ! -f $occt_dir/OpenCASCADEConfig.cmake ]]; then
        bash "$root/scripts/build-occt.sh" --jobs "$jobs"
    fi
    configure+=("-DOpenCASCADE_DIR=$occt_dir")
else
    build="$build_root/linux-core-release"
fi
cmake "${configure[@]}" -B "$build"
$configure_only && exit 0
cmake --build "$build" --parallel "$jobs"
if $test; then
    ctest --test-dir "$build" --output-on-failure --parallel 1 --output-junit "$build/regression-results.xml"
fi
