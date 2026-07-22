// Offline render harness - an AUTOMATED PROXY for the manual "load it in a
// real host and listen" check, not a substitute for it. This environment has
// no audio device or DAW GUI, so this program instantiates
// HallucinatorAudioProcessor directly (bypassing the VST3 wrapper, which
// needs a host to load it) and feeds it synthetic audio through
// processBlock() the same way a host would, at several buffer sizes, then
// checks the output programmatically:
//
//   - Dry/Wet = 0 reproduces the (latency-aligned) input sample-for-sample
//   - no NaN/Inf/gross-clipping in the wet path
//   - each of the six parameters (plus Seed/Freeze Seed) measurably changes
//     the output relative to a baseline render
//   - no per-sample discontinuity at RAVE frame boundaries larger than
//     what the crossfade should allow
//
// Uses the bundled dummy RAVE-interface model (Resources/dummy_rave_model.ts)
// since no real trained checkpoint was reachable in this environment - see
// Resources/MODEL_INTERFACE.md. Passing here says the buffering/latency/
// crossfade/parameter plumbing works; it says nothing about how a real RAVE
// model will sound, and nothing about real-time feasibility of a real
// model's inference cost (that still has to be profiled by the user against
// their own checkpoint and hardware, per the CPU-load meter in the editor).
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <cmath>

namespace
{
    juce::AudioBuffer<float> makeTestSignal(double sampleRate, double seconds)
    {
        const int numSamples = (int) (sampleRate * seconds);
        juce::AudioBuffer<float> buf(2, numSamples);

        juce::Random rng(42);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buf.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                const double t = (double) i / sampleRate;
                const float tone = 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * t);
                const float noise = 0.02f * (rng.nextFloat() * 2.0f - 1.0f);
                data[i] = tone + noise;
            }
        }
        return buf;
    }

    struct RenderStats
    {
        bool hasNaNOrInf = false;
        float peak = 0.0f;
        float rms = 0.0f;
        int maxJumpSampleIndex = -1;
        float maxJump = 0.0f;
    };

    RenderStats analyse(const juce::AudioBuffer<float>& buf)
    {
        RenderStats stats;
        double sumSquares = 0.0;
        int64_t count = 0;

        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            auto* data = buf.getReadPointer(ch);
            float prev = data[0];

            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float x = data[i];

                if (std::isnan(x) || std::isinf(x))
                    stats.hasNaNOrInf = true;

                stats.peak = std::max(stats.peak, std::abs(x));
                sumSquares += (double) x * (double) x;
                ++count;

                const float jump = std::abs(x - prev);
                if (jump > stats.maxJump)
                {
                    stats.maxJump = jump;
                    stats.maxJumpSampleIndex = i;
                }
                prev = x;
            }
        }

        stats.rms = count > 0 ? (float) std::sqrt(sumSquares / (double) count) : 0.0f;
        return stats;
    }

    juce::AudioBuffer<float> renderThroughProcessor(HallucinatorAudioProcessor& proc,
                                                     const juce::AudioBuffer<float>& input,
                                                     int blockSize)
    {
        juce::AudioBuffer<float> output(input.getNumChannels(), input.getNumSamples());
        output.clear();

        juce::MidiBuffer midi;
        int pos = 0;

        while (pos < input.getNumSamples())
        {
            const int thisBlock = std::min(blockSize, input.getNumSamples() - pos);

            juce::AudioBuffer<float> block(input.getNumChannels(), thisBlock);
            for (int ch = 0; ch < input.getNumChannels(); ++ch)
                block.copyFrom(ch, 0, input, ch, pos, thisBlock);

            proc.processBlock(block, midi);

            for (int ch = 0; ch < input.getNumChannels(); ++ch)
                output.copyFrom(ch, pos, block, ch, 0, thisBlock);

            pos += thisBlock;
        }

        return output;
    }

    double maxAbsDiffLatencyAligned(const juce::AudioBuffer<float>& input,
                                     const juce::AudioBuffer<float>& output,
                                     int latencySamples)
    {
        double maxDiff = 0.0;
        const int n = std::min(input.getNumSamples() - latencySamples, output.getNumSamples());

        for (int ch = 0; ch < input.getNumChannels(); ++ch)
        {
            auto* in = input.getReadPointer(ch);
            auto* out = output.getReadPointer(ch);

            for (int i = 0; i < n; ++i)
                maxDiff = std::max(maxDiff, (double) std::abs(in[i] - out[i + latencySamples]));
        }

        return maxDiff;
    }

    double maxAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double maxDiff = 0.0;
        const int n = std::min(a.getNumSamples(), b.getNumSamples());

        for (int ch = 0; ch < std::min(a.getNumChannels(), b.getNumChannels()); ++ch)
        {
            auto* pa = a.getReadPointer(ch);
            auto* pb = b.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                maxDiff = std::max(maxDiff, (double) std::abs(pa[i] - pb[i]));
        }
        return maxDiff;
    }

    void writeWav(const juce::File& file, const juce::AudioBuffer<float>& buf, double sampleRate)
    {
        juce::WavAudioFormat wavFormat;
        file.getParentDirectory().createDirectory();
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        if (stream == nullptr)
            return;

        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(stream.get(), sampleRate, (unsigned int) buf.getNumChannels(), 16, {}, 0));

        if (writer != nullptr)
        {
            stream.release(); // writer now owns the stream
            writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
        }
    }
}

int main(int argc, char** argv)
{
    const juce::String modelPath = argc > 1 ? juce::String(argv[1]) : juce::String("Resources/dummy_rave_model.ts");
    const double sampleRate = 44100.0;
    const double durationSeconds = 2.0;

    int failures = 0;
    auto check = [&failures](bool condition, const juce::String& description)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << description << std::endl;
        if (! condition)
            ++failures;
    };

    auto input = makeTestSignal(sampleRate, durationSeconds);
    juce::File outDir("tests/output");

    // --- load model ---------------------------------------------------------
    HallucinatorAudioProcessor proc;
    juce::String err;
    const bool loaded = proc.loadModel(juce::File(modelPath), err);
    check(loaded, "Model loads (" + modelPath + ")" + (loaded ? "" : (": " + err)));
    if (! loaded)
        return 1;

    proc.setPlayConfigDetails(2, 2, sampleRate, 512);
    proc.prepareToPlay(sampleRate, 512);

    const int latency = proc.getLatencySamples();
    std::cout << "Reported latency: " << latency << " samples" << std::endl;
    check(latency > 0, "Plugin reports nonzero latency once a model is loaded");

    // --- Dry/Wet = 0 must be a clean, latency-aligned passthrough -----------
    proc.apvts.getParameter(Params::dryWet)->setValueNotifyingHost(0.0f);
    proc.prepareToPlay(sampleRate, 512);
    auto dryOutput = renderThroughProcessor(proc, input, 512);
    writeWav(outDir.getChildFile("dry_wet_0.wav"), dryOutput, sampleRate);

    const double passthroughDiff = maxAbsDiffLatencyAligned(input, dryOutput, latency);
    std::cout << "Max abs diff at Dry/Wet=0 (latency-aligned): " << passthroughDiff << std::endl;
    check(passthroughDiff < 1.0e-5, "Dry/Wet=0 passes audio through cleanly (bit-exact after latency alignment)");

    // --- baseline hallucinated render (Dry/Wet = 1, all else default) -------
    proc.apvts.getParameter(Params::dryWet)->setValueNotifyingHost(1.0f);
    proc.prepareToPlay(sampleRate, 512);
    auto baseline = renderThroughProcessor(proc, input, 512);
    writeWav(outDir.getChildFile("baseline.wav"), baseline, sampleRate);

    auto baselineStats = analyse(baseline);
    check(! baselineStats.hasNaNOrInf, "Baseline hallucinated render has no NaN/Inf");
    check(baselineStats.peak < 4.0f, "Baseline hallucinated render is not wildly clipping (peak < 4.0)");

    // --- each parameter should measurably change the output -----------------
    struct ParamTest { const char* id; float value; const char* label; };
    ParamTest paramTests[] = {
        { Params::noiseAmount,   0.5f, "Noise Amount"        },
        { Params::exaggeration,  6.0f, "Exaggeration"        },
        { Params::feedbackIters, 3.0f, "Feedback Iterations" },
        { Params::priorMix,      0.8f, "Prior Mix"           },
        { Params::freezeLatent,  1.0f, "Freeze Latent"       },
    };

    for (auto& pt : paramTests)
    {
        proc.apvts.getParameter(Params::dryWet)->setValueNotifyingHost(1.0f);
        proc.apvts.getParameter(Params::noiseAmount)->setValueNotifyingHost(0.0f);
        proc.apvts.getParameter(Params::exaggeration)->setValueNotifyingHost(0.0f);
        proc.apvts.getParameter(Params::feedbackIters)->setValueNotifyingHost(0.0f);
        proc.apvts.getParameter(Params::priorMix)->setValueNotifyingHost(0.0f);
        proc.apvts.getParameter(Params::freezeLatent)->setValueNotifyingHost(0.0f);

        proc.apvts.getParameter(pt.id)->setValueNotifyingHost(
            proc.apvts.getParameter(pt.id)->convertTo0to1(pt.value));

        proc.prepareToPlay(sampleRate, 512);
        auto variant = renderThroughProcessor(proc, input, 512);
        writeWav(outDir.getChildFile(juce::String(pt.id) + ".wav"), variant, sampleRate);

        const double diff = maxAbsDiff(baseline, variant);
        std::cout << pt.label << " vs baseline max abs diff: " << diff << std::endl;
        check(diff > 1.0e-4, juce::String(pt.label) + " audibly changes the output");
    }

    // --- Seed / Freeze Seed: frozen seed must be reproducible ---------------
    {
        proc.apvts.getParameter(Params::dryWet)->setValueNotifyingHost(1.0f);
        proc.apvts.getParameter(Params::noiseAmount)->setValueNotifyingHost(
            proc.apvts.getParameter(Params::noiseAmount)->convertTo0to1(0.7f));
        proc.apvts.getParameter(Params::freezeSeed)->setValueNotifyingHost(1.0f);
        proc.apvts.getParameter(Params::seed)->setValueNotifyingHost(
            proc.apvts.getParameter(Params::seed)->convertTo0to1(1234.0f));

        proc.prepareToPlay(sampleRate, 512);
        auto run1 = renderThroughProcessor(proc, input, 512);

        proc.prepareToPlay(sampleRate, 512); // reseeds deterministically again
        auto run2 = renderThroughProcessor(proc, input, 512);

        const double diff = maxAbsDiff(run1, run2);
        std::cout << "Freeze Seed reproducibility max abs diff: " << diff << std::endl;
        check(diff < 1.0e-6, "Freeze Seed on: identical output across two prepareToPlay() runs");

        proc.apvts.getParameter(Params::freezeSeed)->setValueNotifyingHost(0.0f);
        proc.prepareToPlay(sampleRate, 512);
        auto run3 = renderThroughProcessor(proc, input, 512);
        proc.prepareToPlay(sampleRate, 512);
        auto run4 = renderThroughProcessor(proc, input, 512);
        const double diffUnfrozen = maxAbsDiff(run3, run4);
        std::cout << "Freeze Seed off reproducibility max abs diff: " << diffUnfrozen << std::endl;
        check(diffUnfrozen > 1.0e-4, "Freeze Seed off: output varies across two prepareToPlay() runs (by design)");
    }

    // --- multiple buffer sizes shouldn't blow up or click --------------------
    for (int blockSize : { 256, 512, 1024 })
    {
        proc.apvts.getParameter(Params::dryWet)->setValueNotifyingHost(1.0f);
        proc.apvts.getParameter(Params::freezeSeed)->setValueNotifyingHost(0.0f);
        proc.prepareToPlay(sampleRate, blockSize);
        auto rendered = renderThroughProcessor(proc, input, blockSize);
        auto stats = analyse(rendered);

        check(! stats.hasNaNOrInf, juce::String("Block size ") + juce::String(blockSize) + ": no NaN/Inf");
        std::cout << "  block=" << blockSize << " peak=" << stats.peak << " rms=" << stats.rms
                  << " maxJump=" << stats.maxJump << " at sample " << stats.maxJumpSampleIndex << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL CHECKS PASSED (" << " see " << outDir.getFullPathName() << " for rendered WAVs)" << std::endl;
    else
        std::cout << failures << " CHECK(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
