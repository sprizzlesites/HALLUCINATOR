#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Central registry of parameter IDs and the APVTS layout describing them.
namespace Params
{
    static constexpr const char* dryWet        = "dryWet";
    static constexpr const char* noiseAmount    = "noiseAmount";
    static constexpr const char* exaggeration   = "exaggeration";
    static constexpr const char* feedbackIters  = "feedbackIterations";
    static constexpr const char* priorMix       = "priorMix";
    static constexpr const char* freezeLatent   = "freezeLatent";
    static constexpr const char* seed           = "seed";
    static constexpr const char* freezeSeed     = "freezeSeed";
    static constexpr const char* chunkSize      = "chunkSize";

    static constexpr int maxFeedbackIterations = 8;

    // Chunk Size is expressed in RAVE frames (each `ratio` audio samples).
    // 1 = the model's output is crossfade-smoothed every frame (smoothest,
    // the default = original behaviour); larger values let the model
    // reinterpret that many frames as one continuous chunk before the
    // smoothing crossfade re-anchors, so more of its raw manipulated output
    // comes through (grittier / more textured).
    static constexpr int minChunkFrames = 1;
    static constexpr int maxChunkFrames = 16;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
