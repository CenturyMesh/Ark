#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_root=${ARK_BUILD_ROOT:-"$root/out"}
runtime=${ARK_RUNTIME_DIR:-"$build_root/linux-native-release"}
sdk=${ARK_OCCT_INSTALL_DIR:-"$build_root/deps/occt/install-linux"}
occt_source=${ARK_OCCT_SOURCE_DIR:-"$build_root/deps/occt/cache/OCCT-8_0_1"}
output=${ARK_OUTPUT_DIR:-"$root/out/releases"}
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || { echo 'This package currently targets Linux x64.' >&2; exit 2; }
command -v patchelf >/dev/null || { echo 'Install patchelf to prepare a relocatable package.' >&2; exit 1; }
metadata="$runtime/app-version.json"
[[ -f $metadata ]] || { echo 'Build metadata is missing.' >&2; exit 1; }
version=$(sed -nE 's/.*"version":"([0-9]+\.[0-9]+\.[0-9]+)".*/\1/p' "$metadata")
[[ -n $version ]] || { echo 'Build metadata has no numeric version.' >&2; exit 1; }
grep -q '"platform":"linux-x64-headless"' "$metadata" || { echo 'Build metadata is not Linux x64.' >&2; exit 1; }
for file in ark-cli ark-mcp ark-occt-worker; do
    [[ -x $runtime/$file ]] || { printf 'Missing native executable: %s\n' "$file" >&2; exit 1; }
done
mkdir -p -- "$output"
name="ark-linux-x64-headless-$version"
archive="$output/$name.tar.gz"
[[ ! -e $archive && ! -e $archive.sha256 ]] || { printf 'Package already exists: %s\n' "$archive" >&2; exit 1; }
staging=$(mktemp -d "$output/.linux-package-XXXXXXXX")
trap 'rm -rf -- "$staging"' EXIT
package="$staging/$name"
mkdir -p -- "$package/bin" "$package/lib" "$package/licenses" "$package/assets/contracts"
cp -- "$metadata" "$package/app-version.json"
for file in ark-cli ark-mcp ark-occt-worker; do
    cp -- "$runtime/$file" "$package/bin/$file"
    patchelf --set-rpath '$ORIGIN/../lib' "$package/bin/$file"
done
# Preserve the SONAME symlink chain, and never bundle user documents/sessions.
cp -a -- "$sdk/lib/"*.so* "$package/lib/"
for file in "$package/lib/"*.so*; do
    [[ -L $file ]] || patchelf --set-rpath '$ORIGIN' "$file"
done
cp -- "$root/third_party/mapbox/LICENSE" "$package/licenses/mapbox-earcut-ISC.txt"
cp -- "$root/third_party/stb/LICENSE" "$package/licenses/stb-image-LICENSE.txt"
cp -- "$root/third_party/stb/README.md" "$package/licenses/stb-image-provenance.md"
cp -- "$root/third_party/sqlite/README.md" "$package/licenses/sqlite-provenance.txt"
cp -- "$root/third_party/sqlite/sqlite3.h" "$package/licenses/sqlite3.h"
cp -- "$root/docs/THIRD_PARTY_NOTICES.md" "$package/licenses/THIRD_PARTY_NOTICES.md"
cp -- "$root/assets/ui/README.md" "$package/licenses/ark-ui-assets.txt"
for file in LICENSE NOTICE; do
    cp -- "$root/$file" "$package/$file"
done
for file in LICENSE_LGPL_21.txt OCCT_LGPL_EXCEPTION.txt; do
    cp -- "$occt_source/$file" "$package/licenses/$file"
done
cat > "$package/licenses/OCCT-SOURCE.txt" <<'EOF'
OCCT 8.0.1, official V8_0_1 source archive:
https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V8_0_1.zip
SHA-256: 7c033d917ee8f040c0512d289dcc5f02c148889d5bac17c3e25639accb44f0da
ARK scripts/build-occt.sh records the dependency build options.
Redistributed OCCT libraries remain separate shared objects and can be replaced.
EOF
for file in BOM AGENT_ACTIVITY MEASUREMENT; do
    cp -- "$root/docs/$file.md" "$package/assets/contracts/$file.md"
done
cp -- "$root/docs/schemas/ark-bom-v1.schema.json" "$package/assets/contracts/"
glibc=$(readelf --version-info "$package/bin/"* "$package/lib/"*.so.8.0.1 | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
glibcxx=$(readelf --version-info "$package/bin/"* "$package/lib/"*.so.8.0.1 | grep -oE 'GLIBCXX_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
cxxabi=$(readelf --version-info "$package/bin/"* "$package/lib/"*.so.8.0.1 | grep -oE 'CXXABI_[0-9]+(\.[0-9]+)+' | sort -Vu | tail -1)
cat > "$package/README.txt" <<EOF
ARK Linux x64 headless runtime

Run bin/ark-mcp for the standalone MCP stdio server, bin/ark-cli <output-directory>
for the kernel demo, or configure your MCP client with the absolute bin/ark-mcp path.
The native OCCT worker is found beside ark-mcp. Runtime metadata is app-version.json.
Extract the whole directory and keep bin/ and lib/ together.

Build host: $(. /etc/os-release && printf '%s' "$PRETTY_NAME").
Compiler: $(c++ --version | head -1).
Required symbol versions: $glibc, $glibcxx, $cxxabi.
Requires compatible glibc/libstdc++ plus the usual libc/libm/libgcc system runtime.
OCCT libraries and their notices are bundled. This is a CLI/MCP/worker distribution;
the Win32 Studio, live Studio bridge, Windows login/update manager and CUDA transport
are outside this Linux package. No Linux OTA endpoint is implemented.
EOF
for file in "$package/bin/"*; do
    if ldd "$file" | grep -q 'not found'; then printf 'Unresolved runtime dependency: %s\n' "$file" >&2; exit 1; fi
done
tar -czf "$archive" -C "$staging" "$name"
(cd -- "$output" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
printf 'Prepared %s\n' "$archive"
