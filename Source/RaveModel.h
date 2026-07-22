#pragma once

#include <torch/script.h>
#include <juce_core/juce_core.h>
#include <optional>

/**
    Loads a torchscript-exported RAVE (or RAVE-compatible) model and exposes
    its encode/decode methods plus the metadata the plugin needs to buffer
    audio correctly: how many audio samples correspond to one latent frame
    ("ratio"), how many channels the latent vector has, and what sample rate
    the model itself was trained at.

    Interface HALLUCINATOR looks for on the loaded torch::jit::script::Module,
    in priority order (first one that succeeds wins) - see
    Resources/MODEL_INTERFACE.md for the full rationale and a worked example:

      1. A sidecar JSON file "<model>.rave.json" next to the .ts file,
         containing {"sample_rate": N, "latent_size": N, "ratio": N}.
         Always wins when present - the explicit escape hatch for any model
         whose scripted methods don't match either convention below.
      2. Three purpose-built scripted methods: get_sample_rate() -> int,
         get_latent_size() -> int, get_ratio() -> int. This is what
         tools/make_dummy_rave_model.cpp implements.
      3. The acids-ircam "nn~"-style convention used by real RAVE exports:
         get_methods() -> List[str] and get_method_params(name) ->
         List[int] returning [in_channels, in_ratio, out_channels, out_ratio]
         for a named method. latent_size = out_channels of "encode",
         ratio = out_ratio of "encode". This is best-effort: it was not
         possible to verify it against a real exported checkpoint in this
         environment (no network access to fetch one), so treat it as a
         starting hypothesis, and prefer the JSON sidecar if it disagrees
         with what you know about your model.

    Encode/decode are called under torch::NoGradGuard and the module is put
    into eval() mode on load, so inference itself is deterministic (no
    dropout, no batchnorm running-stats updates). Any randomness in the
    plugin's output comes only from the noise / prior-mix / feedback layers
    applied to the latent outside of this class.
*/
class RaveModel
{
public:
    RaveModel() = default;

    bool load(const juce::File& modelFile, juce::String& errorMessage);
    bool isLoaded() const noexcept { return loaded; }

    int getSampleRate() const noexcept { return sampleRate; }
    int getRatio() const noexcept { return ratio; }
    int getLatentSize() const noexcept { return latentSize; }

    /** audio: [1, 1, ratio] float32 -> latent: [1, latentSize, 1] */
    torch::Tensor encode(const torch::Tensor& audio);

    /** latent: [1, latentSize, 1] -> audio: [1, 1, ratio] float32 */
    torch::Tensor decode(const torch::Tensor& latent);

private:
    bool tryLoadSidecarJson(const juce::File& modelFile);
    bool tryPurposeBuiltInterface();
    bool tryNnTildeInterface();

    torch::jit::script::Module module;
    bool loaded = false;

    int sampleRate = 44100;
    int ratio = 2048;
    int latentSize = 16;
};
