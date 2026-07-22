#!/usr/bin/env bash
# Fetches the two build-time dependencies that are NOT committed to the repo:
#   libs/JUCE         - the JUCE framework
#   libs/torch-venv   - a Python venv holding a CPU-usable LibTorch build
#
# Why a pip venv instead of the official LibTorch C++ zip:
#   The official CPU LibTorch archives are served from download.pytorch.org,
#   which is not reachable from this environment's network policy. The plain
#   PyPI `torch` wheel IS reachable, and its `torch/lib/` + `torch/include/`
#   + `torch/share/cmake/Torch/` directories are the same LibTorch that the
#   official zip ships - pip is just a convenient transport here.
#
#   The catch: the default PyPI wheel is CUDA-enabled, and even its CPU
#   backend (libtorch_cpu.so) has hard NEEDED entries on libcudart/libcupti
#   (used for profiler hooks), so those two small runtime shim packages are
#   installed too. The much larger CUDA compute libraries (cublas, cudnn,
#   nccl, ...) are deliberately NOT installed - they're only NEEDED by
#   libtorch_cuda.so, which this project never links against.
set -euo pipefail
cd "$(dirname "$0")/.."

TORCH_VERSION="2.4.0"
CUDA_SHIM_VERSION="12.1.105"

if [ ! -d libs/JUCE ]; then
    git clone --depth 1 --branch 8.0.6 https://github.com/juce-framework/JUCE.git libs/JUCE
fi

if [ ! -d libs/torch-venv ]; then
    python3 -m venv libs/torch-venv
    source libs/torch-venv/bin/activate
    pip install --upgrade pip -q
    # --no-deps: skip torch's declared nvidia-*/triton dependencies entirely.
    pip install -q torch=="${TORCH_VERSION}" --no-deps
    # Only the two shim libs libtorch_cpu.so itself needs at dynamic-link time.
    pip install -q "nvidia-cuda-runtime-cu12==${CUDA_SHIM_VERSION}" \
                   "nvidia-cuda-cupti-cu12==${CUDA_SHIM_VERSION}"
    deactivate
fi

echo "Toolchain ready."
echo "Configure the plugin with, e.g.:"
echo "  cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=\$(pwd)/libs/torch-venv/lib/python3.11/site-packages/torch/share/cmake/Torch"
