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

    static constexpr int maxFeedbackIterations = 8;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
