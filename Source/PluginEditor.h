#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class HallucinatorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit HallucinatorAudioProcessorEditor(HallucinatorAudioProcessor&);
    ~HallucinatorAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshModelStatus();
    void chooseModelFile();

    HallucinatorAudioProcessor& processor;

    juce::Slider dryWetSlider, noiseSlider, exaggerationSlider, priorMixSlider, seedSlider;
    juce::Label dryWetLabel, noiseLabel, exaggerationLabel, priorMixLabel, seedLabel, feedbackLabel;
    juce::Slider feedbackSlider;
    juce::ToggleButton freezeLatentButton { "Freeze Latent" };
    juce::ToggleButton freezeSeedButton { "Freeze Seed" };

    juce::TextButton loadModelButton { "Load RAVE Model..." };
    juce::Label modelStatusLabel;
    juce::Label cpuLoadLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> dryWetAttachment, noiseAttachment, exaggerationAttachment,
        feedbackAttachment, priorMixAttachment, seedAttachment;
    std::unique_ptr<ButtonAttachment> freezeLatentAttachment, freezeSeedAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HallucinatorAudioProcessorEditor)
};
