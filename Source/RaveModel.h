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
      3. The acids-ircam "nn~"-style convention as originally guessed from
         documentation: get_methods() -> List[str] and
         get_method_params(name) -> List[int]. Verified against the real
         acids-ircam VCTK checkpoint (via CI - see
         tools/dump_model_interface.py) to NOT match: that export has no
         get_methods()/get_method_params() scripted methods at all. Kept
         only in case some other real export does use it.
      4. What the real VCTK checkpoint actually does (see tools/
         dump_model_interface.py's CI output for how this was found): the
         loaded module is a thin "Combined" wrapper whose own forward()
         just delegates to a "_rave" submodule's forward() (== decode(
         encode(x))) - encode()/decode() themselves only exist as callable
         methods on that submodule, not on the wrapper (torch::jit::Module
         ::get_method() finds them there even though Python's dir() doesn't
         list them - a limitation of Python's introspection, not the
         module). Metadata comes from encode_params/decode_params buffer
         tensors on that submodule ([in_channels, in_ratio, out_channels,
         out_ratio], the same 4 numbers tier 3 hoped to get from a
         get_method_params() call), and sample rate from a "sampling_rate"
         buffer/attribute on the same submodule. This checkpoint's decode()
         also returns 2 channels, not 1 - see decode()'s doc comment.

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

    /**
        latent: [1, latentSize, 1] -> audio: [1, 1, ratio] float32.

        If the underlying model's decode() itself returns more than one
        channel (confirmed true of the real VCTK checkpoint - see
        tools/dump_model_interface.py's CI output, decode_params =
        [8, 2048, 2, 1], i.e. 2 output channels), only channel 0 is kept,
        to preserve this fixed [1, 1, ratio] contract rather than reworking
        the whole plugin to be stereo-latent-aware.
    */
    torch::Tensor decode(const torch::Tensor& latent);

    /** Empirically measures the model's internal algorithmic latency (in
        samples) - the delay between an input sample and when its influence
        first appears in the output. A real streaming RAVE export has
        substantial internal latency (causal-conv cache + PQMF group delay)
        that is NOT captured by `ratio` alone; a stateless model reports ~0.

        Method: feed warmup silence (to establish the model's silence floor),
        then a sustained loud broadband burst, and detect the sample at which
        the output energy first rises above that floor. Returns 0 if it can't
        be determined (safe fallback - the caller then compensates only the
        frame buffering, as before).

        WARNING: this advances the model's streaming state (it runs many
        encode/decode calls), so call it on a throwaway instance, or reload
        the model afterwards before real processing (the plugin measures on a
        separate temporary RaveModel for exactly this reason). */
    int measureInternalLatency();

    /** The onset-detection core of measureInternalLatency(), factored out so
        it can be unit-tested against a synthetic response with a known delay
        (see tests/offline_render_test.cpp). Given the concatenated decoded
        output of the warmup-silence-then-loud probe, returns the delay (in
        samples) between the input step (at sample `inputStepSample`) and the
        point where the output energy first rises above the silence floor
        measured over [0, inputStepSample). Returns 0 if not detectable. */
    static int detectResponseOnset(const std::vector<float>& output, int inputStepSample);

private:
    bool tryLoadSidecarJson(const juce::File& modelFile);
    bool tryPurposeBuiltInterface();
    bool tryNnTildeInterface();
    bool tryCombinedWrapperInterface();

    torch::jit::script::Module module;

    // The module encode()/decode() are actually called on. Equal to
    // `module` unless tryCombinedWrapperInterface() found that encode/
    // decode only exist on a submodule (see its doc comment / tier 4
    // above), in which case this points at that submodule instead.
    torch::jit::script::Module encodeDecodeModule;

    bool loaded = false;

    int sampleRate = 44100;
    int ratio = 2048;
    int latentSize = 16;
};
