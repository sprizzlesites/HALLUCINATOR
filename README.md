# RAVE Hallucinator — VST Scaffold

A JUCE plugin skeleton that loads a pretrained RAVE (IRCAM/ACIDS) torchscript
model and lets you manipulate its latent space live — the audio equivalent of
DeepDream's activation maximization.

This is a **scaffold, not a finished plugin**. It hasn't been compiled — you'll
need a JUCE project set up with libtorch linked, and an actual exported RAVE
`.ts` model file. Treat the `.cpp` as a strong starting skeleton with the hard
design decisions already made, not drop-in-and-build code.

## Why RAVE fits

RAVE is a VAE: it encodes audio into a small latent vector at a reduced frame
rate, and decodes latents back into audio. Both halves are already exported as
independent, callable, real-time-capable torchscript methods
(`encode(x)` / `decode(z)`). That split is exactly what DeepDream needs —
DeepDream pushes an input toward a feature-space target and re-renders it;
here the "feature space" is RAVE's latent space, and "re-render" is `decode`.

## Parameter → DeepDream concept mapping

| Plugin parameter | DeepDream analog | What it does |
|---|---|---|
| **Dry/Wet** | — | Blends hallucinated signal with the original |
| **Noise Amount** | octave jitter / random shift | Adds Gaussian noise to the latent before decoding |
| **Exaggeration** | activation maximization strength | Scales each latent dim's deviation from its running mean — pushes the "features" the model already detected further, the closest 1:1 to DeepDream's core trick |
| **Feedback Iterations** | number of gradient-ascent steps | Re-encodes the decoded output N times before final decode — compounds the hallucination like DeepDream's iteration count |
| **Prior Mix** | starting from noise instead of a photo | Blends the real encoded latent with a latent sampled straight from the VAE prior N(0,1) — at 100% the output is pure dream, unrelated to input |
| **Freeze Latent** | "dream on one frame" | Holds the latent constant and keeps decoding it — turns a transient sound into a sustained drone-hallucination |
| **Seed / Freeze Seed** | — | See determinism section below |

## On your determinism question

Two separate things are going on:

- **The RAVE model itself (encode → decode) is deterministic.** Same input,
  same weights, no dropout at inference → same output, every time, on CPU.
- **The hallucination layer you're adding is what introduces randomness.**
  Noise injection and prior-mix sampling both draw from a random generator.
  Left unseeded, every stop/play will sound different because you're drawing
  fresh random numbers each run — which is honestly the more interesting
  behavior for a "dream" effect, mirroring how DeepDream looks different if
  you jitter/octave-shift with a different random offset each run.
- **If you want reproducibility**, expose a `Seed` parameter and a
  `Freeze Seed` toggle that seeds the RNG at `prepareToPlay()` instead of
  reseeding from system entropy. With a fixed seed, feedback iterations,
  noise, and prior-mix all become deterministic — same seed, same input,
  same hallucination, every time.

So: non-deterministic by default (by design — that's the "alive" quality
you want), deterministic on demand via a seed lock.

## Real-time caveats worth knowing before you build

- RAVE compresses audio at a fixed ratio (commonly 2048:1 latent-frame to
  sample, varies by model). You need an internal ring buffer to accumulate
  enough samples before you can call `encode()`, and you'll report that
  accumulation as **plugin latency** via `setLatencySamples()` — don't skip
  this or the plugin will misbehave in a host doing latency compensation.
- `encode`/`decode` method names and I/O shapes depend on exactly how the
  `.ts` file was exported — verify against your specific model before wiring
  up the calls in `loadModel()`.
- Feedback Iterations > 2–3 will likely blow your real-time budget on CPU
  inference — that parameter is the first thing to cap once you're
  profiling on real hardware.
