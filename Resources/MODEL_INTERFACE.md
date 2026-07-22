# Model interface HALLUCINATOR expects

HALLUCINATOR loads a torchscript (`.ts`) module and needs three numbers from
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

## 3. nn~-style introspection (best effort, for real RAVE exports)

Real `rave export`ed checkpoints (via the acids-ircam `nn_tilde` convention
used across their neural audio synthesis models) are expected to expose:

```python
def get_methods(self) -> List[str]: ...
def get_method_params(self, name: str) -> List[int]: ...  # [in_ch, in_ratio, out_ch, out_ratio]
```

HALLUCINATOR reads `latent_size = out_channels` and `ratio = out_ratio` from
`get_method_params("encode")`, and best-effort tries `get_samplerate()` or an
`sr` attribute for the sample rate.

**This path was not validated against a real RAVE checkpoint** - this
environment has no network access to fetch one from Hugging Face or the
IRCAM forum (both are blocked by the sandbox's network policy). Treat it as
a starting hypothesis. If it loads but the reported ratio/latent size look
wrong (e.g. the plugin sounds pitched/broken relative to what you'd expect),
add a sidecar JSON with the correct numbers instead of trusting this path.

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
