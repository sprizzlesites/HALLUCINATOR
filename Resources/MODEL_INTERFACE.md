# Model interface HALLUCINATOR expects

## How a model gets loaded

In priority order, first one found wins:

1. **`HALLUCINATOR_RAVE_MODEL` env var** - an explicit path, for a fixed
   studio setup or headless use.
2. **A model bundled next to the plugin binary** at
   `<Name>.vst3/Contents/Resources/default_rave_model.ts`
   (`HallucinatorAudioProcessor::findBundledDefaultModel()` in
   `Source/PluginProcessor.cpp`). This is what makes a `dist/` package
   "unzip and go" - the Windows CI workflow places the VCTK model there
   before zipping, so installing the plugin and having a working model are
   the same single step, not two. The model stays swappable at the code
   level (it's read from disk at runtime, not compiled in) - this is just
   automating *where* it's read from by default.
3. **The editor's "Load RAVE Model..." file picker**, which always takes
   precedence once used (see `setStateInformation`).

## What HALLUCINATOR needs from the .ts file itself

It loads a torchscript (`.ts`) module and needs three numbers from
it before it can buffer audio correctly:

- **sample_rate** - the rate the model was trained at
- **latent_size** - number of channels in one latent frame
- **ratio** - audio samples per latent frame (RAVE's compression ratio)

It looks for these in three ways, in this priority order (first success wins):

## 1. Sidecar JSON (always wins if present)

Next to `my_model.ts`, add `my_model.rave.json`:

```json
{ "sample_rate": 44100, "latent_size": 16, "ratio": 2048 }
```

Use this for any model whose scripted methods don't match either convention
below - it's the guaranteed-to-work escape hatch.

## 2. Purpose-built getters (what the dummy test model implements)

If the module defines these scripted methods, HALLUCINATOR calls them
directly:

```python
def get_sample_rate(self) -> int: ...
def get_latent_size(self) -> int: ...
def get_ratio(self) -> int: ...
```

`tools/make_dummy_rave_model.cpp` builds a module with exactly this
interface, so it's a convenient reference implementation.

## 3. nn~-style introspection via callable getters (unconfirmed convention)

Some `rave export`ed checkpoints may expose:

```python
def get_methods(self) -> List[str]: ...
def get_method_params(self, name: str) -> List[int]: ...  # [in_ch, in_ratio, out_ch, out_ratio]
```

HALLUCINATOR reads `latent_size = out_channels` and `ratio = out_ratio` from
`get_method_params("encode")`, and best-effort tries `get_samplerate()` or an
`sr` attribute for the sample rate.

**Confirmed NOT to match the real acids-ircam VCTK checkpoint** (see tier 4
below, and `tools/dump_model_interface.py`'s CI output): that export has no
`get_methods`/`get_method_params` scripted methods at all. Kept only in
case some other real export genuinely uses this convention; the JSON
sidecar is the reliable fallback if a model matches neither this nor tier 4.

## 4. The real VCTK checkpoint's actual convention (confirmed via CI)

Fetching and inspecting the genuine acids-ircam VCTK checkpoint (see
`tools/dump_model_interface.py` and `.github/workflows/fetch-vctk-model.yml`)
revealed a different, real convention:

- The loaded module is a thin **"Combined" wrapper**: its own `forward()`
  just delegates to a `_rave` submodule (`return self._rave.forward(x)`,
  which itself is `self.decode(self.encode(x))`). `encode`/`decode`
  themselves only exist as callable methods on that submodule, not on the
  wrapper - Python's `dir()` doesn't list them there either (a limitation of
  Python's introspection of scripted modules, not the module itself), but
  `torch::jit::Module::get_method()` finds them fine.
- Metadata comes from **buffer tensors**, not callable getters:
  `encode_params`/`decode_params` on the `_rave` submodule, each
  `[in_channels, in_ratio, out_channels, out_ratio]` (the same 4 numbers
  tier 3 hoped to get from a `get_method_params()` call - this export just
  stores them as plain buffers instead). `latent_size = encode_params[2]`,
  `ratio = encode_params[3]`.
- Sample rate comes from a `sampling_rate` buffer/attribute, also on the
  `_rave` submodule (observed as `48000` for this checkpoint) - not
  `sr`/`sample_rate`/`sampling_rate` on the wrapper itself, all of which
  fail.
- **This checkpoint's `decode()` returns 2 channels, not 1**
  (`decode_params = [8, 2048, 2, 1]`). HALLUCINATOR keeps only channel 0 of
  whatever `decode()` returns (see `RaveModel::decode()`), preserving the
  plugin's fixed `[1, 1, ratio]` mono contract rather than reworking the
  whole plugin to be stereo-latent-aware.

See `RaveModel::tryCombinedWrapperInterface()` in `Source/RaveModel.cpp`
for the implementation.

### A real checkpoint's floating-point execution isn't bit-exact across runs

Unlike the deterministic dummy test model (plain arithmetic, no BLAS/SIMD),
a real checkpoint's CPU inference has some inherent run-to-run
floating-point non-determinism (kernel/SIMD codepath selection, denormal
handling) - `RaveModel::load()` forces single-threaded execution
(`at::set_num_threads(1)`), which fixes the overwhelming majority of it, but
RAVE's cached-convolution decoder is recursive, so even a tiny residual can
compound unpredictably over a multi-second render (observed as low as
0.0024 max-abs-diff in one CI run and as high as 0.32 in another, identical
code and model). **Freeze Seed's exact-reproducibility guarantee is for the
plugin's own randomness sources (noise/prior-mix/feedback seeds), not for a
real model's own floating-point execution** - `tests/offline_render_test.cpp`
takes a `--relaxed-freeze-seed` flag (used by `fetch-vctk-model.yml`) that
turns that one check into a report instead of a hard failure for exactly
this reason.

## Encode / decode contract

Regardless of how the metadata was found, `encode` and `decode` are called
with a fixed contract:

- `encode(x)`: `x` is `[1, 1, ratio]` float32 → returns `[1, latent_size, 1]`
- `decode(z)`: `z` is `[1, latent_size, 1]` → returns `[1, 1, ratio]` float32

HALLUCINATOR always calls one full `ratio`-sample frame at a time (never a
partial frame, never multiple latent timesteps in one call). If your export
is a genuine RAVE streaming/causal model, its internal cached-convolution
state persists across calls on the same `torch::jit::script::Module`
instance, which is what makes frame-by-frame concatenation sound
continuous in the first place - HALLUCINATOR doesn't reset that state
between frames.

Inference is expected to be deterministic (eval mode, no dropout). Any
randomness the plugin introduces (Noise Amount, Prior Mix, Feedback
Iterations' effect on the latent trajectory) happens outside of `encode`/
`decode`, in `LatentEngine`.
