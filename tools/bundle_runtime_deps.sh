#!/usr/bin/env bash
# The committed dist/linux/HallucinatorRAVE.vst3 ships ONLY the ~10MB plugin
# binary, not the ~490MB libtorch_cpu.so it dynamically links against - that
# was a deliberate call to avoid permanently bloating this repo's git history
# with a large binary (easy to add later; not easy to remove once committed).
#
# The plugin's rpath is $ORIGIN, i.e. it looks for its runtime deps in the
# same directory as HallucinatorRAVE.so. Run this script after building (or
# against the committed dist/ bundle, pointing -DTORCH_ROOT at your own
# toolchain) to copy those deps in and make the bundle self-contained /
# loadable without any environment setup on the host machine:
set -euo pipefail
cd "$(dirname "$0")/.."

TORCH_ROOT="${1:-libs/torch-venv/lib/python3.11/site-packages/torch}"
NVIDIA_ROOT="$(dirname "$TORCH_ROOT")/nvidia"
DEST="dist/linux/HallucinatorRAVE.vst3/Contents/x86_64-linux"

if [ ! -f "$DEST/HallucinatorRAVE.so" ]; then
    echo "error: $DEST/HallucinatorRAVE.so not found - build and package dist/ first" >&2
    exit 1
fi

cp "$TORCH_ROOT/lib/libtorch_cpu.so" "$DEST/"
cp "$TORCH_ROOT/lib/libc10.so" "$DEST/"
cp "$TORCH_ROOT"/lib/libgomp-*.so.1 "$DEST/"
cp "$NVIDIA_ROOT/cuda_cupti/lib/libcupti.so.12" "$DEST/"
cp "$NVIDIA_ROOT/cuda_runtime/lib/libcudart.so.12" "$DEST/"

echo "Bundled runtime deps into $DEST ($(du -sh "$DEST" | cut -f1) total)."
echo "Verify with: ldd $DEST/HallucinatorRAVE.so | grep -v 'lib/x86_64-linux-gnu'"
