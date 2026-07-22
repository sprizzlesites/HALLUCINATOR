#include "Params.h"

juce::AudioProcessorValueTreeState::ParameterLayout Params::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { dryWet, 1 }, "Dry/Wet",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { noiseAmount, 1 }, "Noise Amount",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { exaggeration, 1 }, "Exaggeration",
        juce::NormalisableRange<float> { 1.0f, 8.0f }, 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { feedbackIters, 1 }, "Feedback Iterations",
        0, maxFeedbackIterations, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { priorMix, 1 }, "Prior Mix",
        juce::NormalisableRange<float> { 0.0f, 1.0f }, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { freezeLatent, 1 }, "Freeze Latent", false));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { seed, 1 }, "Seed", 0, 999999, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { freezeSeed, 1 }, "Freeze Seed", false));

    return { params.begin(), params.end() };
}
