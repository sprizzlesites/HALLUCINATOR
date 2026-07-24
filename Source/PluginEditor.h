#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/NeonLookAndFeel.h"

/** The audio-view "screen": a dark-glass oscilloscope ringed by a neon-tube
    border. Its own Component so it can repaint on the display timer and clip
    the neon bloom to its rounded bounds (the "keep the bloom inside the
    bounds" rule from the design system). */
class ScopeView : public juce::Component
{
public:
    explicit ScopeView(HallucinatorAudioProcessor&);
    void paint(juce::Graphics&) override;

    /** Slow breathing multiplier on the border glow, driven by the editor. */
    float glowIntensity = 1.0f;

private:
    HallucinatorAudioProcessor& processor;
    float scope[HallucinatorAudioProcessor::visSize] = {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeView)
};

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
    void styleKnob(juce::Slider&, juce::Label&, const juce::String&, bool glowPink);
    void styleToggle(juce::TextButton&, bool glowPink);

    HallucinatorAudioProcessor& processor;

    neon::NeonLookAndFeel lookAndFeel;

    ScopeView scope;

    juce::Slider dryWetSlider, noiseSlider, exaggerationSlider, priorMixSlider, seedSlider;
    juce::Label dryWetLabel, noiseLabel, exaggerationLabel, priorMixLabel, seedLabel, feedbackLabel;
    juce::Slider feedbackSlider;
    juce::Slider chunkSlider;
    juce::Label chunkLabel;

    juce::TextButton freezeLatentButton { "FREEZE LATENT" };
    juce::TextButton freezeSeedButton { "FREEZE SEED" };

    juce::TextButton loadModelButton { "LOAD MODEL" };
    juce::Label modelStatusLabel;
    juce::Label cpuLoadLabel;

    juce::Image logo;
    juce::Image plate;
    int plateW = 0, plateH = 0;
    float glowPhase = 0.0f;
    int labelTick = 0;

    std::unique_ptr<juce::FileChooser> fileChooser;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> dryWetAttachment, noiseAttachment, exaggerationAttachment,
        feedbackAttachment, priorMixAttachment, seedAttachment, chunkAttachment;
    std::unique_ptr<ButtonAttachment> freezeLatentAttachment, freezeSeedAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HallucinatorAudioProcessorEditor)
};
