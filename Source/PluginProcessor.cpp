#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int minCrossfadeLength = 8;
    constexpr int maxCrossfadeLength = 256;

    int chooseCrossfadeLength(int ratio)
    {
        return juce::jlimit(minCrossfadeLength, maxCrossfadeLength, ratio / 16);
    }
}

HallucinatorAudioProcessor::HallucinatorAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", Params::createLayout())
{
    pDryWet        = apvts.getRawParameterValue(Params::dryWet);
    pNoiseAmount   = apvts.getRawParameterValue(Params::noiseAmount);
    pExaggeration  = apvts.getRawParameterValue(Params::exaggeration);
    pFeedbackIters = apvts.getRawParameterValue(Params::feedbackIters);
    pPriorMix      = apvts.getRawParameterValue(Params::priorMix);
    pFreezeLatent  = apvts.getRawParameterValue(Params::freezeLatent);
    pSeed          = apvts.getRawParameterValue(Params::seed);
    pFreezeSeed    = apvts.getRawParameterValue(Params::freezeSeed);
    pChunkSize     = apvts.getRawParameterValue(Params::chunkSize);

    resizeBuffersForRatio(currentRatio);

    // Configurable model path: an env var default, so the plugin is usable
    // headlessly / in a fixed studio setup without clicking through a file
    // picker every time. The editor's file picker always takes precedence
    // once the user has chosen something (see setStateInformation).
    juce::File autoLoadPath;
    bool haveAutoLoadPath = false;

    if (auto* envPath = std::getenv("HALLUCINATOR_RAVE_MODEL"))
    {
        autoLoadPath = juce::File(juce::String(envPath));
        haveAutoLoadPath = true;
    }
    else
    {
        // Otherwise, a model bundled alongside the plugin (see
        // findBundledDefaultModel()) loads with no user action at all -
        // this is what makes a dist/ package containing both files "unzip
        // and go" rather than "unzip, then separately go find and load a
        // model".
        auto bundled = findBundledDefaultModel();
        if (bundled.existsAsFile())
        {
            autoLoadPath = bundled;
            haveAutoLoadPath = true;
        }
    }

    // Loaded synchronously here in the constructor: deserializing a real
    // checkpoint takes a couple of seconds (torch::jit::load() plus
    // RaveModel::load()'s sanity round trip), which is well within a host's
    // plugin-scan tolerance now that model loading is fast and reliable.
    // An earlier version did this on a background thread to avoid a
    // suspected slow-construction scan hang, but that hang turned out to be
    // an unrelated Windows DLL problem (torch 2.4.0's fbgemm.dll) - and the
    // async path had a real downside: the model often wasn't loaded yet by
    // the time the editor opened, so the plugin came up with no model and
    // forced a manual file-picker selection. Synchronous load guarantees
    // the bundled model is in place before the host queries latency or the
    // editor first paints. On failure, lastModelError is set and surfaced
    // in the editor; processBlock() stays pure passthrough until a model
    // loads.
    if (haveAutoLoadPath)
    {
        juce::String err;
        loadModel(autoLoadPath, err);
    }
}

juce::File HallucinatorAudioProcessor::findBundledDefaultModel(const juce::File& pluginBinary)
{
    // VST3 bundle layout is <Name>.vst3/Contents/<arch>-<os>/<binary>, on
    // every platform JUCE targets here (including macOS, where the actual
    // Mach-O binary sits under Contents/MacOS/ rather than a plain OS
    // "bundle" API return value) - so three levels up from the binary is
    // always the bundle root, regardless of platform.
    auto bundleRoot = pluginBinary.getParentDirectory().getParentDirectory().getParentDirectory();
    return bundleRoot.getChildFile("Contents").getChildFile("Resources").getChildFile("default_rave_model.ts");
}

juce::File HallucinatorAudioProcessor::findBundledDefaultModel()
{
    return findBundledDefaultModel(juce::File::getSpecialLocation(juce::File::currentExecutableFile));
}

void HallucinatorAudioProcessor::resizeBuffersForRatio(int ratio)
{
    currentRatio = ratio;
    crossfadeLength = chooseCrossfadeLength(ratio);

    const int ringCapacity = ratio * 4 + 8192;
    inputRing.setSize(ringCapacity);
    outputRing.setSize(ringCapacity);
    dryDelayL.setSize(ringCapacity);
    dryDelayR.setSize(ringCapacity);

    // Pre-load the output ring and the dry delay lines with `ratio` samples
    // of silence. This is what makes the declared setLatencySamples(ratio)
    // true: without it, a push-then-pop of the same block within a single
    // processBlock call would return what was just written (zero delay), so
    // the dry path wouldn't actually be delayed to match the wet path, and
    // Dry/Wet blending would smear instead of staying phase-aligned.
    const std::vector<float> zeros((size_t) ratio, 0.0f);
    outputRing.push(zeros.data(), ratio);
    dryDelayL.push(zeros.data(), ratio);
    dryDelayR.push(zeros.data(), ratio);

    prevDecodedTail.assign((size_t) crossfadeLength, 0.0f);
    havePrevTail = false;
    chunkFramePos = 0;
}

bool HallucinatorAudioProcessor::loadModel(const juce::File& modelFile, juce::String& errorMessage)
{
    RaveModel newModel;

    if (! newModel.load(modelFile, errorMessage))
    {
        lastModelError = errorMessage;
        return false;
    }

    installLoadedModel(std::move(newModel), modelFile);
    return true;
}

void HallucinatorAudioProcessor::installLoadedModel(RaveModel&& model, const juce::File& modelFile)
{
    raveModel = std::move(model);
    loadedModelPath = modelFile.getFullPathName();
    lastModelError.clear();

    resizeBuffersForRatio(raveModel.getRatio());
    latentEngine.prepare(raveModel.getLatentSize());

    const bool deterministic = pFreezeSeed != nullptr && pFreezeSeed->load() > 0.5f;
    const int seedValue = pSeed != nullptr ? (int) pSeed->load() : 0;
    latentEngine.reseedRng(seedValue, deterministic);

    latentIsFrozen = false;

    setLatencySamples(currentRatio);
}

void HallucinatorAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    hostSampleRate = sampleRate;

    resizeBuffersForRatio(currentRatio);

    if (raveModel.isLoaded())
    {
        // Real streaming RAVE exports carry internal cached-convolution
        // state across encode()/decode() calls (see Resources/
        // MODEL_INTERFACE.md's "Encode / decode contract"). Reseeding the
        // RNG alone isn't enough for Freeze Seed reproducibility across two
        // prepareToPlay() calls if that leftover state survives between
        // them - invisible with the stateless dummy test model, but real
        // and confirmed via CI against the actual VCTK checkpoint.
        // Reloading from disk resets it deterministically (same file, same
        // starting state every time), matching what "prepare to play again"
        // should mean.
        juce::String reloadError;
        RaveModel reloaded;
        if (reloaded.load(juce::File(loadedModelPath), reloadError))
            raveModel = std::move(reloaded);

        latentEngine.prepare(raveModel.getLatentSize());

        const bool deterministic = pFreezeSeed != nullptr && pFreezeSeed->load() > 0.5f;
        const int seedValue = pSeed != nullptr ? (int) pSeed->load() : 0;
        latentEngine.reseedRng(seedValue, deterministic);

        setLatencySamples(currentRatio);
    }
    else
    {
        setLatencySamples(0);
    }

    latentIsFrozen = false;
    std::fill(std::begin(allpassDelayLine), std::end(allpassDelayLine), 0.0f);
    allpassWritePos = 0;
}

void HallucinatorAudioProcessor::releaseResources()
{
}

bool HallucinatorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

torch::Tensor HallucinatorAudioProcessor::processFrame(const torch::Tensor& inputFrame,
                                                        bool freezeOn,
                                                        float exaggerationAmount,
                                                        float priorMixAmount,
                                                        float noiseAmount,
                                                        int feedbackIterations)
{
    torch::Tensor base;

    if (freezeOn)
    {
        if (! latentIsFrozen)
        {
            base = raveModel.encode(inputFrame);
            frozenLatent = base.clone();
            latentIsFrozen = true;
        }
        else
        {
            base = frozenLatent;
        }
    }
    else
    {
        base = raveModel.encode(inputFrame);
        latentIsFrozen = false;
        latentEngine.updateRunningMean(base);
    }

    auto z = latentEngine.applyExaggeration(base, exaggerationAmount);
    z = latentEngine.applyPriorMix(z, priorMixAmount);
    z = latentEngine.addNoise(z, noiseAmount);

    for (int i = 0; i < feedbackIterations; ++i)
    {
        auto y = raveModel.decode(z);
        z = raveModel.encode(y);
    }

    return raveModel.decode(z);
}

void HallucinatorAudioProcessor::applyStereoWidth(const float* wetMono, float* wetL, float* wetR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float in = wetMono[i];
        wetL[i] = in;

        // Simple Schroeder allpass on the right channel only: same magnitude
        // spectrum, decorrelated phase, cheap enough to run per-sample.
        const float delayed = allpassDelayLine[(size_t) allpassWritePos];
        const float apOut = -allpassGain * in + delayed;
        allpassDelayLine[(size_t) allpassWritePos] = in + allpassGain * apOut;
        allpassWritePos = (allpassWritePos + 1) % allpassDelaySamples;

        wetR[i] = apOut;
    }
}

void HallucinatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (! raveModel.isLoaded())
        return; // pure passthrough: leave buffer untouched (Dry/Wet is moot with no wet path)

    const float dryWet     = pDryWet->load();
    const float noiseAmt   = pNoiseAmount->load();
    const float exagg      = pExaggeration->load();
    const int feedbackIt   = juce::jlimit(0, Params::maxFeedbackIterations, (int) pFeedbackIters->load());
    const float priorMixAmt = pPriorMix->load();
    const bool freezeOn    = pFreezeLatent->load() > 0.5f;
    const int chunkFrames  = juce::jlimit(Params::minChunkFrames, Params::maxChunkFrames,
                                          pChunkSize != nullptr ? (int) pChunkSize->load() : 1);

    const auto blockStartTime = juce::Time::getHighResolutionTicks();

    // --- dry sum-to-mono + push into delay lines --------------------------
    std::vector<float> monoDry((size_t) numSamples);
    auto* left  = buffer.getReadPointer(0);
    auto* right = numChannels > 1 ? buffer.getReadPointer(1) : left;

    for (int i = 0; i < numSamples; ++i)
        monoDry[(size_t) i] = 0.5f * (left[i] + right[i]);

    dryDelayL.push(left, numSamples);
    dryDelayR.push(right, numSamples);

    inputRing.push(monoDry.data(), numSamples);

    // --- run as many full RAVE frames as have accumulated ------------------
    std::vector<float> frameBuf((size_t) currentRatio);

    while (inputRing.availableToRead() >= currentRatio)
    {
        inputRing.pop(frameBuf.data(), currentRatio);

        auto inputTensor = torch::from_blob(frameBuf.data(), { 1, 1, currentRatio }, torch::kFloat32).clone();

        torch::Tensor decoded;
        try
        {
            decoded = processFrame(inputTensor, freezeOn, exagg, priorMixAmt, noiseAmt, feedbackIt);
        }
        catch (const std::exception&)
        {
            // Inference failure mid-stream: fall back to silence for this
            // frame rather than propagating audio-thread exceptions.
            decoded = torch::zeros({ 1, 1, currentRatio });
        }

        decoded = decoded.contiguous();
        const float* decodedData = decoded.data_ptr<float>();

        std::vector<float> outFrame(decodedData, decodedData + currentRatio);

        // Crossfade-smooth this frame's leading edge against the previous
        // frame's tail only at a Chunk Size boundary (chunkFramePos == 0).
        // Within a chunk, frames concatenate raw so more of the model's own
        // (manipulated) output comes through - that's what the Chunk Size
        // knob controls. chunkFrames == 1 makes every frame a boundary,
        // i.e. the original always-smoothed behaviour.
        const bool atChunkBoundary = (chunkFramePos == 0);

        if (havePrevTail && atChunkBoundary)
        {
            for (int i = 0; i < crossfadeLength && i < currentRatio; ++i)
            {
                const float t = (float) i / (float) crossfadeLength;
                const float w = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t); // raised cosine 0->1
                outFrame[(size_t) i] = prevDecodedTail[(size_t) i] * (1.0f - w) + outFrame[(size_t) i] * w;
            }
        }

        for (int i = 0; i < crossfadeLength && i < currentRatio; ++i)
            prevDecodedTail[(size_t) i] = decodedData[currentRatio - crossfadeLength + i];
        havePrevTail = true;
        chunkFramePos = (chunkFramePos + 1) % chunkFrames;

        outputRing.push(outFrame.data(), currentRatio);
    }

    const auto blockEndTime = juce::Time::getHighResolutionTicks();
    const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds(blockEndTime - blockStartTime);
    const double budgetSeconds = (double) numSamples / hostSampleRate;
    const float instantLoad = budgetSeconds > 0.0 ? (float) (elapsedSeconds / budgetSeconds) : 0.0f;
    const float smoothed = cpuLoadFraction.load(std::memory_order_relaxed) * 0.9f + instantLoad * 0.1f;
    cpuLoadFraction.store(smoothed, std::memory_order_relaxed);

    // --- pop aligned wet + delayed-dry, mix, write back --------------------
    std::vector<float> wetMono((size_t) numSamples);
    std::vector<float> wetL((size_t) numSamples), wetR((size_t) numSamples);
    std::vector<float> delayedDryL((size_t) numSamples), delayedDryR((size_t) numSamples);

    if (outputRing.availableToRead() >= numSamples)
        outputRing.pop(wetMono.data(), numSamples);
    else
        std::fill(wetMono.begin(), wetMono.end(), 0.0f);

    dryDelayL.pop(delayedDryL.data(), numSamples);
    dryDelayR.pop(delayedDryR.data(), numSamples);

    applyStereoWidth(wetMono.data(), wetL.data(), wetR.data(), numSamples);

    auto* outL = buffer.getWritePointer(0);
    auto* outR = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mixedL = delayedDryL[(size_t) i] * (1.0f - dryWet) + wetL[(size_t) i] * dryWet;
        outL[i] = mixedL;

        if (outR != nullptr)
        {
            const float mixedR = delayedDryR[(size_t) i] * (1.0f - dryWet) + wetR[(size_t) i] * dryWet;
            outR[i] = mixedR;
        }
    }
}

juce::AudioProcessorEditor* HallucinatorAudioProcessor::createEditor()
{
    return new HallucinatorAudioProcessorEditor(*this);
}

void HallucinatorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();

    if (xml != nullptr)
    {
        xml->setAttribute("hallucinatorModelPath", loadedModelPath);
        copyXmlToBinary(*xml, destData);
    }
}

void HallucinatorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    auto modelPath = xml->getStringAttribute("hallucinatorModelPath");
    if (modelPath.isNotEmpty())
    {
        juce::String err;
        loadModel(juce::File(modelPath), err);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HallucinatorAudioProcessor();
}
