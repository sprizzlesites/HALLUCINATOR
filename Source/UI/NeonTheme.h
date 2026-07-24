#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace neon
{
    // "Chrome & neon" design (hardware-render PDR): a polished silver
    // faceplate floating on a black void, magenta neon rim light, cyan
    // neon around the screen, dark inset wells for the fader bank and
    // display. Neon pink + cyan — the Sprizzle! logo colours.

    inline const juce::Colour voidBg        { 0xff000000 };
    inline const juce::Colour chromeHi      { 0xffd4d8e2 };
    inline const juce::Colour chromeMid     { 0xff9aa0ae };
    inline const juce::Colour chromeLo      { 0xff5d626e };
    inline const juce::Colour chromeText    { 0xff30343e };  // labels on chrome

    inline const juce::Colour inset         { 0xff0c0c14 };  // dark wells
    inline const juce::Colour screenBg      { 0xff120e1e };  // purple-black glass

    inline const juce::Colour neonPink      { 0xffff5fd2 };
    inline const juce::Colour neonMagenta   { 0xffd94ff0 };
    inline const juce::Colour neonViolet    { 0xff8f6bff };
    inline const juce::Colour neonCyan      { 0xff5fe0ff };
    inline const juce::Colour neonCyanDim   { 0xff1e6b80 };

    inline const juce::Colour textBright    { 0xffeef2fa };
    inline const juce::Colour textDim       { 0xff8b91a8 };

    inline juce::Colour accent()    { return neonPink; }
    inline juce::Colour accentDim() { return neonMagenta.withAlpha (0.55f); }

    // All four harmony strips share the pink->cyan neon language; the pairs
    // alternate hue so the bank reads like the reference render.
    inline const juce::Colour voiceColours[4] { neonPink, neonCyan, neonMagenta, neonViolet };

    // Font construction compatible with both JUCE 8 (FontOptions) and
    // JUCE 7 (used for the MinGW cross-build, where JUCE 8 is unsupported).
    inline juce::Font makeFont (float height, int styleFlags = juce::Font::plain)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::Font (juce::FontOptions (height, styleFlags));
       #else
        return juce::Font (height, styleFlags);
       #endif
    }

    inline float stringWidth (const juce::Font& font, const juce::String& text)
    {
       #if JUCE_MAJOR_VERSION >= 8
        return juce::GlyphArrangement::getStringWidth (font, text);
       #else
        return font.getStringWidthFloat (text);
       #endif
    }

    // ---- drawing helpers ---------------------------------------------------

    inline void glowRoundedRect (juce::Graphics& g, juce::Rectangle<float> r,
                                 float corner, juce::Colour glow, float intensity = 1.0f,
                                 int layers = 5)
    {
        for (int i = layers; i >= 1; --i)
        {
            g.setColour (glow.withAlpha (0.05f * intensity * (float) (layers + 1 - i)));
            g.drawRoundedRectangle (r.expanded ((float) i * 1.6f), corner + (float) i, 2.6f);
        }
        g.setColour (glow.withAlpha (0.9f * intensity));
        g.drawRoundedRectangle (r, corner, 1.4f);
    }

    // Dark inset well with a soft neon border.
    inline void drawInsetPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner,
                                juce::Colour glow, float glowIntensity = 0.8f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f).expanded (1.0f), corner + 1.0f);

        juce::ColourGradient grad (inset.brighter (0.35f), r.getX(), r.getY(),
                                   inset, r.getX(), r.getY() + r.getHeight() * 0.35f, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, corner);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (1.0f), corner - 1.0f, 2.0f);

        glowRoundedRect (g, r, corner, glow, glowIntensity, 4);
    }

    // Metallic-black faceplate: dark brushed-metal gradient (a touch darker
    // than the inset fader/EQ panels) with fine streaks and a subtle top
    // sheen. The knobs/buttons keep their own chrome — this is only the
    // background plate.
    inline const juce::Colour plateHi  { 0xff14161b };
    inline const juce::Colour plateMid { 0xff0a0b0f };
    inline const juce::Colour plateLo  { 0xff040406 };

    inline juce::Image makeChromePlate (int w, int h)
    {
        juce::Image img (juce::Image::ARGB, juce::jmax (8, w), juce::jmax (8, h), true);
        juce::Graphics g (img);

        juce::ColourGradient base (plateHi, 0.0f, 0.0f, plateLo, 0.0f, (float) h, false);
        base.addColour (0.10, plateHi.brighter (0.10f));
        base.addColour (0.45, plateMid);
        base.addColour (0.80, plateLo.brighter (0.06f));
        g.setGradientFill (base);
        g.fillAll();

        juce::Random rng (0xc0ffee);
        for (int y = 0; y < h; ++y)
        {
            const float a = 0.010f + 0.030f * rng.nextFloat();
            g.setColour (rng.nextBool()
                             ? juce::Colours::white.withAlpha (a)
                             : juce::Colours::black.withAlpha (a));
            const int x0 = rng.nextInt (juce::jmax (1, w / 3));
            g.fillRect (x0, y, w - x0 - rng.nextInt (juce::jmax (1, w / 3)), 1);
        }

        juce::ColourGradient shine (juce::Colours::white.withAlpha (0.14f), 0.0f, 0.0f,
                                    juce::Colours::transparentWhite, 0.0f, (float) h * 0.10f,
                                    false);
        g.setGradientFill (shine);
        g.fillRect (0, 0, w, (int) ((float) h * 0.10f));
        return img;
    }

    // Trims fully-transparent padding from a logo so it can be laid out by
    // its visible artwork. (The image itself is untouched.)
    inline juce::Image cropTransparentBorder (const juce::Image& src)
    {
        if (! src.isValid() || ! src.hasAlphaChannel())
            return src;

        juce::Image::BitmapData bd (src, juce::Image::BitmapData::readOnly);
        int minX = src.getWidth(), minY = src.getHeight(), maxX = -1, maxY = -1;

        for (int y = 0; y < src.getHeight(); ++y)
            for (int x = 0; x < src.getWidth(); ++x)
                if (bd.getPixelColour (x, y).getAlpha() > 8)
                {
                    minX = juce::jmin (minX, x);
                    minY = juce::jmin (minY, y);
                    maxX = juce::jmax (maxX, x);
                    maxY = juce::jmax (maxY, y);
                }

        if (maxX < minX)
            return src;

        return src.getClippedImage ({ minX, minY, maxX - minX + 1, maxY - minY + 1 });
    }
}
