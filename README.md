# HALLUCINATOR — RAVE Hallucinator

A real-time VST3 effect that loads a pretrained RAVE (IRCAM/ACIDS) torchscript
model and manipulates its latent space live to produce audio "hallucinations"
— the audio equivalent of DeepDream's activation-maximization image effect.

## Status: builds and passes its offline test harness on Linux/VST3

What that means concretely, and what it doesn't, before you read further:

- Builds clean (zero warnings) as a VST3 on this Linux dev environment.
- An offline render harness (`tests/offline_render_test.cpp`) exercises the
  full audio pipeline against a small synthetic placeholder model and passes:
  latency-aligned Dry/Wet=0 passthrough, no NaN/Inf, all six parameters
  audibly change output, Freeze Seed reproducibility. See "What wasn't
  possible to verify here" below — this harness is a proxy, not the manual
  DAW check the build brief asks for.
- **Not tested against a real trained RAVE checkpoint.** This dev environment
  has no network access to Hugging Face or the IRCAM model forum, so the
  wrapper's model-introspection code (`RaveModel::tryNnTildeInterface()`) is
  a best-effort, undocumented-convention guess — see
  `Resources/MODEL_INTERFACE.md`. Bring your own exported `.ts` model and
  check it loads; if the reported ratio/latent size look wrong, use the
  JSON sidecar override described there.
- **No AU build.** This environment is Linux; AU (`.component`) requires
  Xcode/the AudioUnit SDK on macOS. Only VST3 is built here.

## Build

```sh
tools/setup_toolchain.sh   # fetches JUCE + a CPU-usable libtorch (see below)
cmake -B build -G Ninja \
  -DTORCH_ROOT="$(pwd)/libs/torch-venv/lib/python3.11/site-packages/torch"
cmake --build build -j$(nproc)
```

The built VST3 ends up at
`build/HallucinatorRAVE_artefacts/Release/VST3/HallucinatorRAVE.vst3`.

### Why a pip venv instead of the official LibTorch zip

The official CPU LibTorch archive is served from `download.pytorch.org`,
which this environment's network policy blocks. Plain PyPI (`pip install
torch`) is reachable, and a torch wheel's `torch/lib/`, `torch/include/`,
and `torch/share/cmake/Torch/` directories are the same LibTorch the official
zip ships. Two wrinkles this project works around (see
`tools/setup_toolchain.sh` and `CMakeLists.txt` for the full story):

- The wheel is CUDA-enabled, and even `libtorch_cpu.so` itself has hard
  `NEEDED` entries on `libcudart`/`libcupti` (profiler hooks). Rather than
  installing gigabytes of CUDA compute libraries (cuDNN, cuBLAS, NCCL) that
  this project never uses, only the two small runtime shim packages are
  installed (`--no-deps` skips the rest).
- `TorchConfig.cmake` (via `Caffe2Config.cmake`) hard-requires a full CUDA
  toolkit at CMake configure time purely because the wheel was built with
  `USE_CUDA=1` — even though nothing here links `libtorch_cuda.so`. The
  project bypasses `find_package(Torch)` entirely and points
  `-DTORCH_ROOT=...` at the two libraries it actually needs
  (`libtorch_cpu.so`, `libc10.so`), proven to link and run standalone.

## Plugin parameters

| Parameter | Behavior |
|---|---|
| **Dry/Wet** | Mix between untouched input and hallucinated output |
| **Noise Amount** | Adds Gaussian noise to the latent before decoding |
| **Exaggeration** | Scales each latent dimension's deviation from its own running (EMA) mean — pushes whatever the model already "detected" further |
| **Feedback Iterations** (0–8) | Re-encodes the decoded output N times before the final decode, compounding the hallucination |
| **Prior Mix** | Blends the real encoded latent with one sampled from N(0,1) — at 100% the output is pure "dream" |
| **Freeze Latent** | Holds the base latent constant (skips re-encoding), turning a transient into a sustained drone. Noise/Exaggeration/Prior Mix/Feedback still apply on top each frame, so the drone isn't perfectly static. |
| **Seed** (int) + **Freeze Seed** (toggle) | Freeze Seed on: RNG is reseeded deterministically at `prepareToPlay()`, so noise/prior-mix/feedback become reproducible. Off (default): reseeded from system entropy at `prepareToPlay()`, so hallucinations vary between playbacks — intended behavior, not a bug. |

## Design decisions (and why)

**Stereo handling: mono-through-model + fixed stereo-width decorrelation.**
Real RAVE checkpoints are almost universally trained on mono audio, and
inference is already the single heaviest item in the real-time budget —
running L/R independently would double that cost for a mode the model was
never trained to exploit. Input is summed to mono before encode; the
hallucinated mono output is spread to stereo via a small fixed Schroeder
allpass on the right channel only (`PluginProcessor::applyStereoWidth`) —
same magnitude spectrum, decorrelated phase, no added parameter.

**Inference runs inline on the audio thread, not on a worker thread.** A
background-inference + double-buffer architecture is the "correct" real-time
design, but getting the hand-off right needs profiling against a real
model's actual inference cost, which wasn't possible here (see below). This
is the single sharpest edge in this codebase: if your model + Feedback
Iterations setting can't finish inside one block's time budget, you'll hear
dropouts, not a graceful fallback. The editor shows a running "inference
load" meter (`HallucinatorAudioProcessor::getCpuLoadFraction()`) so at least
that ceiling is visible instead of silent.

**Crossfade instead of true overlapping-window overlap-add.** A properly
`--streaming`-exported RAVE model carries cached-conv state across calls on
the same `torch::jit::script::Module` instance, so back-to-back frames
should already be close to continuous. As a cheap safety net against
residual boundary discontinuities (or non-streaming-exported models),
consecutive decoded frames are blended over a short raised-cosine crossfade
window at the frame boundary, rather than paying for a second overlapping
inference call per frame purely for smoothing.

**No host/model sample-rate resampling.** If the model's native sample rate
differs from the host's, this plugin does not resample — the RAVE ratio is
applied directly in host samples, which pitches/time-warps the output by the
mismatch ratio. A resampling path is easy to get subtly wrong, and there was
no real model + mismatched-host-rate combination available here to validate
one against. The editor surfaces a warning when rates disagree; run the host
at the model's native rate as the workaround.

**Dry path is latency-aligned.** The dry signal is delayed by the same
`ratio`-sample latency as the wet path (`dryDelayL`/`dryDelayR` ring
buffers), so Dry/Wet blending at any intermediate value stays phase-aligned
instead of comb-filtering. The host is told about this delay via
`setLatencySamples()` for DAW latency compensation.

## Model interface

See `Resources/MODEL_INTERFACE.md` for the full contract, including a JSON
sidecar override for models whose scripted methods don't match what
`RaveModel` looks for. Short version: point the plugin at a `.ts` file
(editor's "Load RAVE Model..." button, or the `HALLUCINATOR_RAVE_MODEL` env
var for a fixed default) exporting `encode`/`decode`, plus either
`get_sample_rate()`/`get_latent_size()`/`get_ratio()` or the acids-ircam
`nn~`-style `get_methods()`/`get_method_params()` convention.

`tools/make_dummy_rave_model.cpp` generates a tiny placeholder model with a
trivial deterministic "compression" (mean-pool encode / repeat-expand
decode) implementing that interface — **not a trained RAVE model, will not
sound like one** — purely so the plugin's buffering/latency/crossfade/latent
pipeline could be exercised end-to-end in an environment with no access to a
real pretrained checkpoint.

## What wasn't possible to verify here, and why

This dev environment is a headless Linux container: no audio device, no DAW
GUI, and a network policy that blocks Hugging Face, the IRCAM model forum,
and `download.pytorch.org`. Three things the build brief asks for couldn't
be done as literally specified:

1. **"Load it in a real host and confirm..."** — there is no host/GUI here.
   `tests/offline_render_test.cpp` is an automated proxy: it drives
   `HallucinatorAudioProcessor` directly through `processBlock()` at several
   buffer sizes and checks passthrough correctness, absence of NaN/Inf, and
   that every parameter measurably changes the output. It does not replace
   actually loading the `.vst3` in a DAW and listening — that step is still
   outstanding and should be done before trusting `dist/`.
2. **A real pretrained RAVE checkpoint** — unreachable from this network.
   Tested against the synthetic placeholder model instead (see above).
3. **AU build** — this container is Linux; AU needs macOS/Xcode.

## Repo layout

```
CMakeLists.txt
Source/              PluginProcessor, PluginEditor, RaveModel, LatentEngine, Params, RingBuffer
Resources/           MODEL_INTERFACE.md, dummy_rave_model.ts (placeholder test fixture)
tools/               setup_toolchain.sh, make_dummy_rave_model.cpp
tests/               offline_render_test.cpp
dist/                packaged plugin bundle(s) (committed separately from source)
libs/                JUCE + torch-venv (gitignored, fetched by setup_toolchain.sh)
```
