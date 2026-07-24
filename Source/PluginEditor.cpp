#include "PluginEditor.h"
#include "BinaryData.h"

//==============================================================================
// ScopeView - the audio-view screen
//==============================================================================
ScopeView::ScopeView(HallucinatorAudioProcessor& p) : processor(p)
{
    setInterceptsMouseClicks(false, false);
}

void ScopeView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float corner = 8.0f;

    // Dark purple-black display glass with a soft top-down sheen.
    {
        juce::ColourGradient glass(neon::screenBg.brighter(0.12f), r.getX(), r.getY(),
                                   neon::screenBg.darker(0.35f), r.getX(), r.getBottom(), false);
        g.setGradientFill(glass);
        g.fillRoundedRectangle(r, corner);
    }

    auto plot = r.reduced(10.0f);
    const float midY = plot.getCentreY();

    // Faint centre line + a couple of graticule rows.
    g.setColour(neon::neonCyan.withAlpha(0.10f));
    for (int k = -2; k <= 2; ++k)
    {
        const float y = midY + (float) k * plot.getHeight() * 0.20f;
        g.drawHorizontalLine((int) y, plot.getX(), plot.getRight());
    }

    // Waveform from the processor's lock-free snapshot (oldest -> newest).
    processor.getVisualisationSnapshot(scope);

    juce::Path wave;
    const int n = HallucinatorAudioProcessor::visSize;
    const float amp = plot.getHeight() * 0.46f;
    for (int i = 0; i < n; ++i)
    {
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) (n - 1);
        const float y = midY - juce::jlimit(-1.0f, 1.0f, scope[i]) * amp;
        if (i == 0) wave.startNewSubPath(x, y);
        else        wave.lineTo(x, y);
    }

    // Neon-tube trace: soft cyan bloom under a bright near-white core.
    g.setColour(neon::neonCyan.withAlpha(0.22f));
    g.strokePath(wave, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(neon::neonCyan);
    g.strokePath(wave, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.strokePath(wave, juce::PathStrokeType(0.7f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    // "OUTPUT" tag, top-left inside the glass.
    g.setColour(neon::neonCyan.withAlpha(0.6f));
    g.setFont(neon::makeFont(10.5f, juce::Font::bold));
    g.drawText("OUTPUT", r.reduced(10.0f).removeFromTop(14.0f).toNearestInt(),
               juce::Justification::topLeft);

    // Neon-tube border, bloom kept inside the component bounds so the rounded
    // corners stay round (design rule #1). Reduce by the bloom margin first.
    neon::glowRoundedRect(g, r.reduced(6.0f), corner - 3.0f, neon::neonCyan, glowIntensity, 4);
}

//==============================================================================
// Editor
//==============================================================================
void HallucinatorAudioProcessorEditor::styleKnob(juce::Slider& slider, juce::Label& label,
                                                 const juce::String& text, bool glowPink)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 18);
    slider.setColour(juce::Slider::textBoxTextColourId, neon::textBright);
    slider.getProperties().set("glowPink", glowPink);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(neon::makeFont(12.0f, juce::Font::bold));
    // Cross-colour the label against the knob (pink knob -> cyan label).
    label.setColour(juce::Label::textColourId, glowPink ? neon::neonCyan : neon::neonPink);
    label.attachToComponent(&slider, false);
    addAndMakeVisible(label);
}

void HallucinatorAudioProcessorEditor::styleToggle(juce::TextButton& b, bool glowPink)
{
    b.setClickingTogglesState(true);
    auto& props = b.getProperties();
    props.set("neonBlack", true);
    props.set("neonPink", glowPink);
    b.setColour(juce::TextButton::textColourOffId, (glowPink ? neon::neonPink : neon::neonCyan).withAlpha(0.85f));
    b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(b);
}

HallucinatorAudioProcessorEditor::HallucinatorAudioProcessorEditor(HallucinatorAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), scope(p)
{
    setLookAndFeel(&lookAndFeel);

    logo = juce::ImageCache::getFromMemory(BinaryData::SprizzleLogo_png, BinaryData::SprizzleLogo_pngSize);
    logo = neon::cropTransparentBorder(logo);

    addAndMakeVisible(scope);

    styleKnob(dryWetSlider,       dryWetLabel,       "DRY / WET",     true);
    styleKnob(noiseSlider,        noiseLabel,        "NOISE",         false);
    styleKnob(exaggerationSlider, exaggerationLabel, "EXAGGERATION",  true);
    styleKnob(feedbackSlider,     feedbackLabel,     "FEEDBACK",      false);
    styleKnob(priorMixSlider,     priorMixLabel,     "PRIOR MIX",     true);
    styleKnob(chunkSlider,        chunkLabel,        "CHUNK SIZE",    false);
    styleKnob(seedSlider,         seedLabel,         "SEED",          true);

    styleToggle(freezeLatentButton, true);
    styleToggle(freezeSeedButton, false);

    loadModelButton.getProperties().set("neonBlack", true);
    loadModelButton.getProperties().set("neonPink", true);
    loadModelButton.setColour(juce::TextButton::textColourOffId, neon::neonPink);
    loadModelButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(loadModelButton);
    loadModelButton.onClick = [this] { chooseModelFile(); };

    modelStatusLabel.setJustificationType(juce::Justification::centredLeft);
    modelStatusLabel.setFont(neon::makeFont(12.0f));
    addAndMakeVisible(modelStatusLabel);

    cpuLoadLabel.setJustificationType(juce::Justification::centredLeft);
    cpuLoadLabel.setFont(neon::makeFont(12.0f));
    addAndMakeVisible(cpuLoadLabel);

    auto& apvts = processor.apvts;
    dryWetAttachment       = std::make_unique<SliderAttachment>(apvts, Params::dryWet, dryWetSlider);
    noiseAttachment        = std::make_unique<SliderAttachment>(apvts, Params::noiseAmount, noiseSlider);
    exaggerationAttachment = std::make_unique<SliderAttachment>(apvts, Params::exaggeration, exaggerationSlider);
    feedbackAttachment     = std::make_unique<SliderAttachment>(apvts, Params::feedbackIters, feedbackSlider);
    priorMixAttachment     = std::make_unique<SliderAttachment>(apvts, Params::priorMix, priorMixSlider);
    seedAttachment         = std::make_unique<SliderAttachment>(apvts, Params::seed, seedSlider);
    chunkAttachment        = std::make_unique<SliderAttachment>(apvts, Params::chunkSize, chunkSlider);
    freezeLatentAttachment = std::make_unique<ButtonAttachment>(apvts, Params::freezeLatent, freezeLatentButton);
    freezeSeedAttachment   = std::make_unique<ButtonAttachment>(apvts, Params::freezeSeed, freezeSeedButton);

    // Set AFTER the attachments: SliderParameterAttachment installs its own
    // textFromValueFunction (driven by the parameter's text), so overriding it
    // here is what actually trims the continuous knobs to 2 decimal places.
    for (auto* s : { &dryWetSlider, &noiseSlider, &exaggerationSlider, &priorMixSlider })
    {
        s->textFromValueFunction = [](double v) { return juce::String(v, 2); };
        s->updateText(); // refresh the already-shown text with the new format
    }

    refreshModelStatus();
    startTimerHz(30);

    setResizable(false, false);
    setSize(840, 600);
}

HallucinatorAudioProcessorEditor::~HallucinatorAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void HallucinatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // The void.
    g.fillAll(neon::voidBg);

    // Metallic-black faceplate inset from the edges, cached at the current size.
    auto plateArea = bounds.reduced(12.0f);
    const int pw = (int) plateArea.getWidth();
    const int ph = (int) plateArea.getHeight();
    if (plate.isNull() || plateW != pw || plateH != ph)
    {
        plate = neon::makeChromePlate(pw, ph);
        plateW = pw;
        plateH = ph;
    }

    juce::Path plateClip;
    plateClip.addRoundedRectangle(plateArea, 16.0f);
    g.saveState();
    g.reduceClipRegion(plateClip);
    g.drawImageAt(plate, (int) plateArea.getX(), (int) plateArea.getY());
    g.restoreState();

    // Pink neon rim light around the faceplate (breathing).
    neon::glowRoundedRect(g, plateArea.reduced(2.0f), 15.0f, neon::neonPink, 0.55f * glowPhase, 5);

    // Title, top-left on the plate.
    auto header = plateArea.reduced(20.0f).removeFromTop(30.0f);
    g.setFont(neon::makeFont(22.0f, juce::Font::bold));
    g.setColour(neon::neonPink);
    g.drawText("RAVE", header.removeFromLeft(64.0f), juce::Justification::centredLeft);
    g.setColour(neon::neonCyan);
    g.drawText("HALLUCINATOR", header, juce::Justification::centredLeft);

    // Sprizzle logo, bottom-right of the faceplate - placed as-is (transparent).
    if (logo.isValid())
    {
        const float maxW = 132.0f, maxH = 42.0f;
        const float scale = juce::jmin(maxW / (float) logo.getWidth(),
                                       maxH / (float) logo.getHeight());
        const float lw = (float) logo.getWidth() * scale;
        const float lh = (float) logo.getHeight() * scale;
        const float lx = plateArea.getRight() - 20.0f - lw;
        const float ly = plateArea.getBottom() - 16.0f - lh;
        g.drawImage(logo, juce::Rectangle<float>(lx, ly, lw, lh),
                    juce::RectanglePlacement::centred, false);
    }
}

void HallucinatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(12).reduced(20);
    area.removeFromTop(34); // title

    // Header: load button + model status.
    auto headerRow = area.removeFromTop(30);
    loadModelButton.setBounds(headerRow.removeFromLeft(150));
    headerRow.removeFromLeft(10);
    modelStatusLabel.setBounds(headerRow);

    area.removeFromTop(12);

    // Audio-view screen.
    scope.setBounds(area.removeFromTop(150));

    area.removeFromTop(28); // room for the knob labels above the row

    // Knob bank (7 across).
    auto knobRow = area.removeFromTop(132);
    const int knobWidth = knobRow.getWidth() / 7;
    dryWetSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 0));
    noiseSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 0));
    exaggerationSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 0));
    feedbackSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 0));
    priorMixSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 0));
    chunkSlider.setBounds(knobRow.removeFromLeft(knobWidth).reduced(6, 0));
    seedSlider.setBounds(knobRow.reduced(6, 0));

    area.removeFromTop(14);

    // Toggle row.
    auto toggleRow = area.removeFromTop(30);
    const int half = toggleRow.getWidth() / 2;
    freezeLatentButton.setBounds(toggleRow.removeFromLeft(half).reduced(6, 0));
    freezeSeedButton.setBounds(toggleRow.reduced(6, 0));

    area.removeFromTop(10);
    // CPU load, bottom-left (logo occupies bottom-right, drawn in paint()).
    cpuLoadLabel.setBounds(area.removeFromTop(22).removeFromLeft(area.getWidth() - 150));
}

void HallucinatorAudioProcessorEditor::timerCallback()
{
    // Breathing glow for the rim + scope border.
    glowPhase = 0.75f + 0.25f * std::sin((float) juce::Time::getMillisecondCounter() * 0.0016f);
    scope.glowIntensity = glowPhase;
    scope.repaint();

    // Rim light breathes on the plate - repaint just the plate border region
    // cheaply by repainting the whole window at 30Hz is fine for a plugin UI.
    repaint();

    // Labels only need ~4Hz; throttle so we're not re-formatting strings 30x/s.
    if (++labelTick < 8)
        return;
    labelTick = 0;

    const float load = processor.getCpuLoadFraction();
    cpuLoadLabel.setText("Inference load: " + juce::String(load * 100.0f, 1) + "% of RT budget",
                          juce::dontSendNotification);
    cpuLoadLabel.setColour(juce::Label::textColourId,
                           load > 0.9f ? neon::neonPink : neon::neonCyan.withAlpha(0.8f));

    refreshModelStatus();
}

void HallucinatorAudioProcessorEditor::refreshModelStatus()
{
    if (processor.isModelLoaded())
    {
        juce::String msg = "Loaded: " + juce::File(processor.getLoadedModelPath()).getFileName()
                            + juce::String::formatted("  (ratio=%d, sr=%d)",
                                                       processor.getModelRatio(),
                                                       processor.getModelSampleRate());

        if (processor.getModelSampleRate() != (int) processor.getSampleRate() && processor.getSampleRate() > 0)
        {
            msg += "  [host " + juce::String((int) processor.getSampleRate())
                   + "Hz != model rate - pitched/time-warped]";
            modelStatusLabel.setColour(juce::Label::textColourId, neon::neonPink);
        }
        else
        {
            modelStatusLabel.setColour(juce::Label::textColourId, neon::neonCyan.withAlpha(0.85f));
        }

        modelStatusLabel.setText(msg, juce::dontSendNotification);
    }
    else
    {
        auto err = processor.getLastModelError();
        modelStatusLabel.setColour(juce::Label::textColourId, neon::neonPink);
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
