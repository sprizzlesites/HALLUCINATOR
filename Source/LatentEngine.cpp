#include "LatentEngine.h"
#include <ATen/core/Generator.h>
#include <ATen/CPUGeneratorImpl.h>

void LatentEngine::prepare(int latentSizeIn)
{
    latentSize = latentSizeIn;
    generator = at::detail::createCPUGenerator();
    reset();
}

void LatentEngine::reseedRng(int seedValue, bool deterministic)
{
    uint64_t actualSeed;

    if (deterministic)
    {
        actualSeed = (uint64_t) seedValue;
    }
    else
    {
        // Reseed from system entropy so hallucinations vary between
        // playbacks by default - this is the intended default behaviour,
        // not a bug. The user-facing Seed value is folded in too so it
        // still has some influence even when Freeze Seed is off.
        std::random_device rd;
        uint64_t entropy = (uint64_t) rd() ^ ((uint64_t) rd() << 32);
        actualSeed = entropy ^ (uint64_t) (uint32_t) seedValue;
    }

    generator.set_current_seed(actualSeed);
}

void LatentEngine::reset()
{
    statsInitialised = false;
}

void LatentEngine::updateRunningMean(const torch::Tensor& latent)
{
    if (! statsInitialised)
    {
        runningMean = latent.clone();
        statsInitialised = true;
        return;
    }

    runningMean = runningMean * (1.0f - emaAlpha) + latent * emaAlpha;
}

torch::Tensor LatentEngine::applyExaggeration(const torch::Tensor& latent, float amount) const
{
    if (! statsInitialised)
        return latent;

    return runningMean + (latent - runningMean) * amount;
}

torch::Tensor LatentEngine::addNoise(const torch::Tensor& latent, float amount)
{
    if (amount <= 0.0f)
        return latent;

    auto noise = torch::randn(latent.sizes(), generator, torch::TensorOptions().dtype(latent.dtype()));
    return latent + noise * amount;
}

torch::Tensor LatentEngine::applyPriorMix(const torch::Tensor& latent, float amount)
{
    if (amount <= 0.0f)
        return latent;

    auto prior = torch::randn(latent.sizes(), generator, torch::TensorOptions().dtype(latent.dtype()));

    if (amount >= 1.0f)
        return prior;

    return latent * (1.0f - amount) + prior * amount;
}
