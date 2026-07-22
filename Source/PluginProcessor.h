#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

#include "Params.h"
#include "RaveModel.h"
#include "LatentEngine.h"
#include "RingBuffer.h"

/**
    RAVE Hallucinator processor.

    Design decisions worth reading before touching this file:

    Stereo handling
    ---------------
    Stereo channels are NOT run through the model independently. Input is
    summed to mono before encode, and the mono hallucinated output is spread
    back to stereo with a small fixed decorrelation (see applyStereoWidth()).
    Reasons: (1) real RAVE checkpoints are near-universally trained on mono
    audio, so an "independent per-channel" mode would mean running two
    separate encode/decode/feedback chains per block for a benefit the model
    was never trained to provide; (2) this is already the single heaviest
    part of the real-time budget (a torchscript inference call per RAVE
    frame, times up to 1 + 2*FeedbackIterations calls) - doubling it for
    stereo independence risks blowing the real-time budget before Feedback
    Iterations even gets used. Mono-through-model plus a cheap decorrelation
    step gives a usable stereo image at roughly half the inference cost.

    Threading / real-time-safety
    -----------------------------
    Inference runs inline on the audio thread inside processBlock(), not on
    a worker thread. A "proper" design would hand frames to a background
    inference thread and double-buffer the result, but that adds its own
    synchronisation complexity and was out of scope to get right without a
    real trained model available in this environment to profile against
    (see Resources/MODEL_INTERFACE.md). This is the sharpest edge in this
    codebase: if your model + FeedbackIterations setting can't finish within
    one block's real-time budget, you will hear dropouts, not a graceful
    fallback. cpuLoadFraction() reports a running estimate of how close to
    that ceiling the plugin is running, so at least the problem is visible
    rather than silent.

    Sample-rate mismatch
    --------------------
    If the model's own sample rate (as reported by RaveModel) differs from
    the host's, this plugin does NOT resample. The RAVE "ratio" is applied
    directly in host samples, meaning a mismatch pitches/time-warps the
    hallucinated content by the mismatch ratio. This was a deliberate scope
    cut: a resampling path is easy to get subtly wrong, and there was no
    real model + host-rate combination available to validate one against.
    The editor surfaces a warning when rates disagree; the fix is to run
    the host at the model's native rate.
*/
class HallucinatorAudioProcessor : public juce::AudioProcessor
{
public:
    HallucinatorAudioProcessor();
    ~HallucinatorAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HallucinatorRAVE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /** Loads a model from disk; updates latency reporting on success.
        Safe to call from the message thread (editor) while audio is
        stopped or running - it does not touch audio-thread state until
        the swap at the end. */
    bool loadModel(const juce::File& modelFile, juce::String& errorMessage);
    bool isModelLoaded() const { return raveModel.isLoaded(); }
    juce::String getLoadedModelPath() const { return loadedModelPath; }
    juce::String getLastModelError() const { return lastModelError; }
    int getModelSampleRate() const { return raveModel.getSampleRate(); }
    int getModelRatio() const { return raveModel.getRatio(); }

    /** Smoothed estimate of (time spent in inference) / (real-time budget
        for that many samples). >1 means the plugin is falling behind. */
    float getCpuLoadFraction() const { return cpuLoadFraction.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

private:
    torch::Tensor processFrame(const torch::Tensor& inputFrame,
                                bool freezeOn,
                                float exaggerationAmount,
                                float priorMixAmount,
                                float noiseAmount,
                                int feedbackIterations);

    void applyStereoWidth(const float* wetMono, float* wetL, float* wetR, int numSamples);

    void resizeBuffersForRatio(int ratio);

    RaveModel raveModel;
    LatentEngine latentEngine;
    juce::String loadedModelPath;
    juce::String lastModelError;

    double hostSampleRate = 44100.0;
    int currentRatio = 2048;
    int crossfadeLength = 32;

    RingBuffer inputRing;
    RingBuffer outputRing;
    RingBuffer dryDelayL, dryDelayR;

    std::vector<float> prevDecodedTail;
    bool havePrevTail = false;

    // Stereo decorrelation allpass state (right channel only).
    float allpassDelayLine[64] = {};
    int allpassWritePos = 0;
    static constexpr int allpassDelaySamples = 17;
    static constexpr float allpassGain = 0.35f;

    std::atomic<float> cpuLoadFraction { 0.0f };

    // Cached raw parameter pointers (APVTS guarantees these stay valid for
    // the processor's lifetime).
    std::atomic<float>* pDryWet = nullptr;
    std::atomic<float>* pNoiseAmount = nullptr;
    std::atomic<float>* pExaggeration = nullptr;
    std::atomic<float>* pFeedbackIters = nullptr;
    std::atomic<float>* pPriorMix = nullptr;
    std::atomic<float>* pFreezeLatent = nullptr;
    std::atomic<float>* pSeed = nullptr;
    std::atomic<float>* pFreezeSeed = nullptr;

    bool latentIsFrozen = false;
    torch::Tensor frozenLatent;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HallucinatorAudioProcessor)
};
