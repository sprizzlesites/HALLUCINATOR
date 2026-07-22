#include "PluginEditor.h"

namespace
{
    void setupSlider(juce::Slider& slider, juce::Label& label, const juce::String& text,
                      juce::Component& parent)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        parent.addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.attachToComponent(&slider, false);
        parent.addAndMakeVisible(label);
    }
}

HallucinatorAudioProcessorEditor::HallucinatorAudioProcessorEditor(HallucinatorAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setupSlider(dryWetSlider, dryWetLabel, "Dry/Wet", *this);
    setupSlider(noiseSlider, noiseLabel, "Noise Amount", *this);
    setupSlider(exaggerationSlider, exaggerationLabel, "Exaggeration", *this);
    setupSlider(feedbackSlider, feedbackLabel, "Feedback Iters", *this);
    setupSlider(priorMixSlider, priorMixLabel, "Prior Mix", *this);
    setupSlider(seedSlider, seedLabel, "Seed", *this);

    addAndMakeVisible(freezeLatentButton);
    addAndMakeVisible(freezeSeedButton);

    addAndMakeVisible(loadModelButton);
    loadModelButton.onClick = [this] { chooseModelFile(); };

    modelStatusLabel.setJustificationType(juce::Justification::centredLeft);
    modelStatusLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    addAndMakeVisible(modelStatusLabel);

    cpuLoadLabel.setJustificationType(juce::Justification::centredRight);
    cpuLoadLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    addAndMakeVisible(cpuLoadLabel);

    auto& apvts = processor.apvts;
    dryWetAttachment       = std::make_unique<SliderAttachment>(apvts, Params::dryWet, dryWetSlider);
    noiseAttachment        = std::make_unique<SliderAttachment>(apvts, Params::noiseAmount, noiseSlider);
    exaggerationAttachment = std::make_unique<SliderAttachment>(apvts, Params::exaggeration, exaggerationSlider);
    feedbackAttachment     = std::make_unique<SliderAttachment>(apvts, Params::feedbackIters, feedbackSlider);
    priorMixAttachment     = std::make_unique<SliderAttachment>(apvts, Params::priorMix, priorMixSlider);
    seedAttachment         = std::make_unique<SliderAttachment>(apvts, Params::seed, seedSlider);
    freezeLatentAttachment = std::make_unique<ButtonAttachment>(apvts, Params::freezeLatent, freezeLatentButton);
    freezeSeedAttachment   = std::make_unique<ButtonAttachment>(apvts, Params::freezeSeed, freezeSeedButton);

    refreshModelStatus();
    startTimerHz(4);

    setResizable(false, false);
    setSize(640, 360);
}

HallucinatorAudioProcessorEditor::~HallucinatorAudioProcessorEditor()
{
    stopTimer();
}

void HallucinatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    g.drawFittedText("RAVE Hallucinator", getLocalBounds().removeFromTop(40),
                      juce::Justification::centred, 1);
}

void HallucinatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(40); // title

    auto topRow = area.removeFromTop(30);
    loadModelButton.setBounds(topRow.removeFromLeft(180));
    modelStatusLabel.setBounds(topRow.reduced(4, 0));

    area.removeFromTop(10);

    auto knobRow = area.removeFromTop(140);
    const int knobWidth = knobRow.getWidth() / 6;
    dryWetSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 20));
    noiseSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 20));
    exaggerationSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 20));
    feedbackSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 20));
    priorMixSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 20));
    seedSlider.setBounds(knobRow.reduced(6, 20));

    area.removeFromTop(10);

    auto toggleRow = area.removeFromTop(30);
    freezeLatentButton.setBounds(toggleRow.removeFromLeft(area.getWidth() / 2));
    freezeSeedButton.setBounds(toggleRow);

    area.removeFromTop(10);
    cpuLoadLabel.setBounds(area.removeFromTop(24));
}

void HallucinatorAudioProcessorEditor::timerCallback()
{
    const float load = processor.getCpuLoadFraction();
    cpuLoadLabel.setText("Inference load: " + juce::String(load * 100.0f, 1) + "% of real-time budget",
                          juce::dontSendNotification);

    if (load > 0.9f)
        cpuLoadLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    else
        cpuLoadLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
}

void HallucinatorAudioProcessorEditor::refreshModelStatus()
{
    if (processor.isModelLoaded())
    {
        juce::String msg = "Loaded: " + juce::File(processor.getLoadedModelPath()).getFileName()
                            + juce::String::formatted(" (ratio=%d, sr=%d)",
                                                       processor.getModelRatio(),
                                                       processor.getModelSampleRate());

        if (processor.getModelSampleRate() != (int) processor.getSampleRate() && processor.getSampleRate() > 0)
        {
            msg += "  [WARNING: host sample rate " + juce::String((int) processor.getSampleRate())
                   + "Hz != model rate - audio will be pitched/time-warped]";
            modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else
        {
            modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
        }

        modelStatusLabel.setText(msg, juce::dontSendNotification);
    }
    else
    {
        auto err = processor.getLastModelError();
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        modelStatusLabel.setText(err.isNotEmpty() ? err : "No model loaded (passthrough only)",
                                  juce::dontSendNotification);
    }
}

void HallucinatorAudioProcessorEditor::chooseModelFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select a torchscript RAVE model (.ts)",
        juce::File(),
        "*.ts");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (! file.existsAsFile())
            return;

        juce::String errorMessage;
        if (! processor.loadModel(file, errorMessage))
            juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                         "Failed to load model", errorMessage);

        refreshModelStatus();
    });
}
