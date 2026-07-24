// Offline UI preview: instantiate the plugin editor headlessly, paint it to a
// PNG, and write it to tests/output/. Not shipped - a dev aid so the neon UI
// can be eyeballed without a DAW or a display. Renders on JUCE's software
// path, so it needs no X server.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    HallucinatorAudioProcessor processor;

    // Push a little audio through so the scope shows a waveform rather than a
    // flat line (the passthrough path feeds the visualiser even with no model
    // loaded). A couple of octaves summed makes a lively trace.
    {
        const int blockSize = 512;
        processor.prepareToPlay(48000.0, blockSize);
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        for (int b = 0; b < 4; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.6f * (float) (std::sin(phase) + 0.4 * std::sin(phase * 3.0));
                buf.setSample(0, i, s);
                buf.setSample(1, i, s);
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
            }
            processor.processBlock(buf, midi);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    const int w = 840, h = 600;
    editor->setBounds(0, 0, w, h); // triggers resized()

    juce::Image img(juce::Image::ARGB, w, h, true);
    {
        juce::Graphics g(img);
        editor->paintEntireComponent(g, true);
    }

    juce::File out(argc > 1 ? juce::String(argv[1])
                            : juce::String("tests/output/editor_preview.png"));
    out.getParentDirectory().createDirectory();
    out.deleteFile();

    juce::FileOutputStream os(out);
    if (os.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream(img, os);
    }

    juce::Logger::writeToLog("Wrote " + out.getFullPathName());
    return 0;
}
