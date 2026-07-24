#pragma once

#include "NeonTheme.h"

namespace neon
{

/** Ultra-polished chrome & neon look-and-feel: radial-specular metal knobs
    on neon under-glow pucks, tall glossy fader caps on bloomed neon slots,
    chrome pill buttons that light neon when engaged. */
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, neon::chromeText);
        setColour (juce::ComboBox::backgroundColourId, neon::inset);
        setColour (juce::ComboBox::textColourId, neon::neonCyan);
        setColour (juce::ComboBox::outlineColourId, neon::chromeLo);
        setColour (juce::ComboBox::arrowColourId, neon::neonPink);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff10101a));
        setColour (juce::PopupMenu::textColourId, neon::textBright);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId,
                   neon::neonMagenta.withAlpha (0.28f));
        setColour (juce::TextButton::textColourOffId, neon::chromeText);
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff10222c));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return neon::makeFont (13.0f, juce::Font::bold);
    }

    //==========================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h)
                          .reduced (3.0f);
        const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto  centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float angle  = rotaryStartAngle
                           + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const bool pinkGlow = (bool) slider.getProperties().getWithDefault ("glowPink", false);
        const auto glowCol = pinkGlow ? neon::neonPink : neon::neonCyan;

        // Neon-tube ring around the knob: a single crisp lit ring with just a
        // thin, tight bloom hugging it (like a glass neon tube), not a wide
        // wash.
        {
            const float gr = radius * 0.99f;
            // Tight bloom: only a couple of close, low-alpha passes.
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (glowCol.withAlpha (0.10f * (float) (4 - i)));
                g.drawEllipse (centre.x - gr, centre.y - gr, gr * 2.0f, gr * 2.0f,
                               1.4f + (float) i * 1.1f);
            }
            // Crisp bright core ring.
            g.setColour (glowCol.brighter (0.35f));
            g.drawEllipse (centre.x - gr, centre.y - gr, gr * 2.0f, gr * 2.0f, 1.8f);
        }

        // Engraved tick ring.
        for (int t = 0; t <= 10; ++t)
        {
            const float ta = rotaryStartAngle
                           + (rotaryEndAngle - rotaryStartAngle) * (float) t / 10.0f;
            const float c = std::cos (ta - juce::MathConstants<float>::halfPi);
            const float s = std::sin (ta - juce::MathConstants<float>::halfPi);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawLine (centre.x + c * (radius - 0.4f) + 0.6f,
                        centre.y + s * (radius - 0.4f) + 0.6f,
                        centre.x + c * radius + 0.6f, centre.y + s * radius + 0.6f, 1.1f);
            g.setColour (neon::chromeText.withAlpha (0.7f));
            g.drawLine (centre.x + c * (radius - 1.4f), centre.y + s * (radius - 1.4f),
                        centre.x + c * radius,          centre.y + s * radius, 1.1f);
        }

        // Neon value arc.
        const float arcR = radius - 4.0f;
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                               rotaryStartAngle, angle, true);
            g.setColour (glowCol.withAlpha (0.16f));
            g.strokePath (arc, juce::PathStrokeType (6.5f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
            g.setColour (glowCol.withAlpha (0.9f));
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Chrome body: deep drop shadow, radial-specular rim, knurls, face.
        const float bodyR = radius * 0.68f;
        auto body = juce::Rectangle<float> (centre.x - bodyR, centre.y - bodyR,
                                            bodyR * 2.0f, bodyR * 2.0f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (body.translated (0.0f, bodyR * 0.12f).expanded (bodyR * 0.06f));

        {
            // Off-centre radial highlight: the polished look.
            juce::ColourGradient rim (juce::Colours::white,
                                      centre.x - bodyR * 0.55f, centre.y - bodyR * 0.65f,
                                      neon::chromeLo.darker (0.45f),
                                      centre.x + bodyR * 0.7f, centre.y + bodyR * 0.9f, true);
            rim.addColour (0.25, neon::chromeHi);
            rim.addColour (0.6, neon::chromeMid.darker (0.1f));
            g.setGradientFill (rim);
            g.fillEllipse (body);
        }

        // Knurled grip band.
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        for (int k = 0; k < 28; ++k)
        {
            const float ka = juce::MathConstants<float>::twoPi * (float) k / 28.0f;
            const float c = std::cos (ka), s = std::sin (ka);
            g.drawLine (centre.x + c * (bodyR * 0.84f), centre.y + s * (bodyR * 0.84f),
                        centre.x + c * (bodyR * 0.99f), centre.y + s * (bodyR * 0.99f), 1.1f);
        }

        // Dark face with a soft radial sheen and glass edge.
        auto face = body.reduced (bodyR * 0.24f);
        {
            juce::ColourGradient faceGrad (juce::Colour (0xff353a46),
                                           face.getX() + face.getWidth() * 0.3f,
                                           face.getY() + face.getHeight() * 0.25f,
                                           juce::Colour (0xff10131a),
                                           face.getRight(), face.getBottom(), true);
            g.setGradientFill (faceGrad);
            g.fillEllipse (face);
        }
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.drawEllipse (face.reduced (0.5f), 1.0f);

        // Specular crescent on the rim, top-left.
        {
            juce::Path spec;
            spec.addCentredArc (centre.x, centre.y, bodyR * 0.92f, bodyR * 0.92f, 0.0f,
                                -2.6f, -1.1f, true);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.strokePath (spec, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }

        // Neon pointer with bloom.
        const float px = std::cos (angle - juce::MathConstants<float>::halfPi);
        const float py = std::sin (angle - juce::MathConstants<float>::halfPi);
        g.setColour (glowCol.withAlpha (0.30f));
        g.drawLine (centre.x + px * bodyR * 0.30f, centre.y + py * bodyR * 0.30f,
                    centre.x + px * (bodyR * 0.92f), centre.y + py * (bodyR * 0.92f), 4.0f);
        g.setColour (glowCol.brighter (0.5f));
        g.drawLine (centre.x + px * bodyR * 0.30f, centre.y + py * bodyR * 0.30f,
                    centre.x + px * (bodyR * 0.92f), centre.y + py * (bodyR * 0.92f), 1.6f);
    }

    //==========================================================================
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider&) override
    {
        if (style != juce::Slider::LinearVertical)
            return;

        const float cx = (float) x + (float) w * 0.5f;
        const float tubeW = 8.0f;
        auto slot = juce::Rectangle<float> (cx - tubeW * 0.5f, (float) y, tubeW, (float) h);
        const float rad = tubeW * 0.5f;   // fully rounded -> a capsule-ended tube

        // Machined channel behind the tube.
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (slot.expanded (3.0f), rad + 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (slot.expanded (3.5f), rad + 3.5f, 1.0f);

        // Neon tube: pink -> cyan gradient, kept, with heavy bloom.
        juce::ColourGradient neon (neon::neonPink, cx, slot.getY(),
                                   neon::neonCyan, cx, slot.getBottom(), false);
        for (int i = 4; i >= 1; --i)
        {
            g.setGradientFill (neon);
            g.setOpacity (0.055f * (float) (5 - i));
            g.fillRoundedRectangle (slot.expanded ((float) i * 2.2f), rad + (float) i * 2.2f);
        }
        g.setOpacity (1.0f);

        // Glowing ring: the tube WALL is the lit part. Fill the capsule with
        // the bright pink->cyan gradient, then punch a black channel down the
        // centre — so the gradient survives only as a ring of light around a
        // dark core, not a solid bar.
        {
            juce::ColourGradient bright (neon::neonPink.brighter (0.35f), cx, slot.getY(),
                                         neon::neonCyan.brighter (0.35f), cx, slot.getBottom(), false);
            g.setGradientFill (bright);
            g.fillRoundedRectangle (slot, rad);
        }

        // Black centre channel (the line down the middle reads black).
        auto core = slot.reduced (2.3f);
        const float coreRad = juce::jmax (0.5f, rad - 2.3f);
        g.setColour (juce::Colour (0xff050507));
        g.fillRoundedRectangle (core, coreRad);
        // Crisp inner edge so the ring stays sharp against the black.
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (core, coreRad, 0.9f);

        // Black cap with a neon (pink->cyan) outline and outline bloom — the
        // fader knob reads as a lit black slab, not chrome.
        auto cap = juce::Rectangle<float> (cx - 15.0f, sliderPos - 22.0f, 30.0f, 44.0f);
        const auto capNeon = neon::neonPink.interpolatedWith (neon::neonCyan, 0.4f);

        // Drop shadow.
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.fillRoundedRectangle (cap.translated (0.0f, 3.0f).expanded (1.0f), 7.0f);

        // Outer neon bloom.
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (capNeon.withAlpha (0.07f * (float) (5 - i)));
            g.drawRoundedRectangle (cap.expanded ((float) i * 1.4f), 7.0f + (float) i, 2.0f);
        }

        // Black body with a subtle top-lit sheen so it still reads 3-D.
        juce::ColourGradient body (juce::Colour (0xff2a2c34), cap.getX(), cap.getY(),
                                   juce::Colour (0xff090a0e), cap.getX(), cap.getBottom(), false);
        body.addColour (0.14, juce::Colour (0xff40434e));
        g.setGradientFill (body);
        g.fillRoundedRectangle (cap, 6.5f);

        // Top highlight strip.
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.fillRoundedRectangle (cap.reduced (4.0f, 0.0f).withY (cap.getY() + 3.0f).withHeight (2.0f), 1.0f);

        // Neon outline + centre grip line in neon.
        g.setColour (capNeon.withAlpha (0.95f));
        g.drawRoundedRectangle (cap, 6.5f, 1.6f);
        g.setColour (capNeon.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (cap.getX() + 5.0f,
                                                        cap.getCentreY() - 1.0f,
                                                        cap.getWidth() - 10.0f, 2.0f), 1.0f);
    }

    //==========================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool isOver, bool isDown) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (2.0f);
        const bool on = button.getToggleState();
        auto& props = button.getProperties();

        // Solid-black body with a neon-tube border (WAVE/EQ, LIVE, AUTO,
        // POP/JAZZ). The border stays lit so the outline always reads; it
        // brightens and blooms when the button is engaged or hovered.
        if ((bool) props.getWithDefault ("neonBlack", false))
        {
            const bool pink   = (bool) props.getWithDefault ("neonPink", true);
            // Buttons flagged "onCyan" glow blue while selected (e.g. LIVE,
            // AUTO), keeping their idle border colour otherwise.
            const bool onCyan = (bool) props.getWithDefault ("onCyan", false);
            const auto neon   = (on && onCyan) ? neon::neonCyan
                                               : (pink ? neon::neonPink : neon::neonCyan);

            // Drop shadow.
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.8f), 6.0f);

            // Solid black face (a hair of lift when engaged so 'on' still reads).
            juce::ColourGradient body (juce::Colour (on ? 0xff181418 : 0xff0d0d0d),
                                       r.getCentreX(), r.getY(),
                                       juce::Colour (0xff040404),
                                       r.getCentreX(), r.getBottom(), false);
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, 5.5f);

            // Neon border with a tight outer bloom when engaged / hovered.
            const float glow = on ? 1.0f : (isOver ? 0.6f : 0.0f);
            if (glow > 0.0f)
                for (int i = 3; i >= 1; --i)
                {
                    g.setColour (neon.withAlpha (0.10f * glow * (float) (4 - i)));
                    g.drawRoundedRectangle (r.expanded ((float) i * 1.3f), 6.0f + (float) i, 1.6f);
                }
            g.setColour (neon.withAlpha (on ? 1.0f : 0.72f));
            g.drawRoundedRectangle (r, 5.5f, on ? 2.0f : 1.4f);

            if (isDown)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.fillRoundedRectangle (r, 5.5f);
            }

            // Up/down toggle arrows (POP/JAZZ), pink — signals this button
            // flips between two states.
            if ((bool) props.getWithDefault ("arrows", false))
            {
                const float ax = r.getRight() - 9.0f;
                const float ay = r.getCentreY();
                const float aw = 5.0f, ah = 3.4f, gap = 1.8f;
                juce::Path up, dn;
                up.addTriangle (ax - aw * 0.5f, ay - gap, ax + aw * 0.5f, ay - gap, ax, ay - gap - ah);
                dn.addTriangle (ax - aw * 0.5f, ay + gap, ax + aw * 0.5f, ay + gap, ax, ay + gap + ah);
                g.setColour (neon::neonPink);
                g.fillPath (up);
                g.fillPath (dn);
            }
            return;
        }

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.8f), 6.0f);

        if (on)
        {
            for (int i = 3; i >= 1; --i)
            {
                g.setColour (neon::neonCyan.withAlpha (0.09f * (float) (4 - i)));
                g.fillRoundedRectangle (r.expanded ((float) i * 1.6f), 7.0f);
            }
            juce::ColourGradient lit (neon::neonCyan.brighter (0.45f), r.getCentreX(), r.getY(),
                                      neon::neonCyan.darker (0.1f), r.getCentreX(), r.getBottom(),
                                      false);
            lit.addColour (0.15, juce::Colours::white.withAlpha (0.9f));
            g.setGradientFill (lit);
        }
        else
        {
            juce::ColourGradient met (neon::chromeHi.brighter (isOver ? 0.2f : 0.05f),
                                      r.getCentreX(), r.getY(),
                                      neon::chromeLo, r.getCentreX(), r.getBottom(), false);
            met.addColour (0.15, juce::Colours::white.withAlpha (0.85f));
            met.addColour (0.5, neon::chromeMid);
            g.setGradientFill (met);
        }
        g.fillRoundedRectangle (r, 5.5f);

        g.setColour (juce::Colours::black.withAlpha (isDown ? 0.4f : 0.3f));
        g.drawRoundedRectangle (r, 5.5f, 1.1f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return neon::makeFont (11.5f, juce::Font::bold);
    }
};

} // namespace neon
