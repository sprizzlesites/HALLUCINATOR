#pragma once

#include <torch/script.h>
#include <random>

/**
    Pure latent-domain operations shared by every processed frame:
      - a running per-dimension mean (EMA) used by Exaggeration
      - Gaussian noise / prior sampling, using a generator that is either
        reseeded deterministically at prepareToPlay() (Freeze Seed on) or
        reseeded from system entropy at prepareToPlay() (Freeze Seed off,
        the default - hallucinations vary between playbacks intentionally)
      - Freeze Latent's capture-on-rising-edge state

    Feedback Iterations is NOT implemented here because it needs to call
    back into the model's encode/decode - see PluginProcessor::processFrame().
*/
class LatentEngine
{
public:
    void prepare(int latentSizeIn);

    /** Called once per prepareToPlay(). If deterministic is false, mixes in
        real system entropy so behaviour differs between playbacks. */
    void reseedRng(int seedValue, bool deterministic);

    void reset();

    /** EMA update of the running per-dimension mean. Call once per frame
        with the freshly encoded (pre-manipulation) latent, unless the latent
        is currently frozen (a frozen latent isn't "new" information). */
    void updateRunningMean(const torch::Tensor& latent);

    /** z' = mean + (z - mean) * amount. amount == 1 is the identity. */
    torch::Tensor applyExaggeration(const torch::Tensor& latent, float amount) const;

    /** z + amount * N(0,1), same shape as z. */
    torch::Tensor addNoise(const torch::Tensor& latent, float amount);

    /** lerp(latent, N(0,1), amount). */
    torch::Tensor applyPriorMix(const torch::Tensor& latent, float amount);

private:
    int latentSize = 16;
    torch::Tensor runningMean;
    bool statsInitialised = false;
    static constexpr float emaAlpha = 0.05f;

    at::Generator generator;
};
