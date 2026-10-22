#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Palette.h"

// Shared drawing for the rock-amp look: black tolex background with an amber glow, leather panels
// with bronze bevels and brass corner screws, and engraved title plates. Every page uses these, so
// the look has one owner.
namespace rockstyle
{
// Tolex grain, generated once and tiled. A software image: plain memory, so this static can be freed
// safely when the plugin unloads (a native Direct2D image outliving the renderer hangs the host).
inline const juce::Image& tolexTexture()
{
    static const juce::Image texture = []
    {
        constexpr int size = 128;
        juce::Image img (juce::Image::ARGB, size, size, true, juce::SoftwareImageType());
        juce::Graphics g (img);
        juce::Random rng (0x70e5);
        for (int i = 0; i < 1400; ++i)
        {
            const float x = rng.nextFloat() * size, y = rng.nextFloat() * size;
            const bool light = rng.nextBool();
            g.setColour ((light ? juce::Colours::white : juce::Colours::black).withAlpha (light ? 0.035f : 0.12f));
            g.fillRect (x, y, 1.0f + rng.nextFloat() * 1.5f, 1.0f);
        }
        return img;
    }();
    return texture;
}

inline void paintBackground (juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::ColourGradient base (palette::trench.brighter (0.15f), area.getCentreX(), area.getY(),
                               palette::abyss, area.getCentreX(), area.getBottom(), false);
    g.setGradientFill (base);
    g.fillRect (area);
    g.setTiledImageFill (tolexTexture(), 0, 0, 1.0f);
    g.fillRect (area);
    // Warm light from above, like the glow of the tubes in the logo.
    juce::ColourGradient glow (palette::biolume.withAlpha (0.10f), area.getCentreX(), area.getY(),
                               juce::Colours::transparentBlack, area.getCentreX(), area.getY() + area.getHeight() * 0.6f, true);
    g.setGradientFill (glow);
    g.fillRect (area);
}

inline void paintScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle)
{
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    juce::ColourGradient head (palette::brassLight, r.getX(), r.getY(), palette::brassDark, r.getRight(), r.getBottom(), false);
    g.setGradientFill (head);
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 0.8f);
    juce::Path slot;
    slot.addRectangle (-radius * 0.75f, -radius * 0.14f, radius * 1.5f, radius * 0.28f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillPath (slot, juce::AffineTransform::rotation (slotAngle).translated (centre));
}

// A leather panel with a bronze bevel, a brass strip in the accent colour and four corner screws.
inline void paintPanel (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour accent, float scale = 1.0f, bool screws = true)
{
    const float corner = 7.0f * scale;
    // Drop shadow.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (area.translated (0.0f, 2.0f * scale), corner);

    juce::ColourGradient leather (palette::panel.brighter (0.12f), area.getX(), area.getY(),
                                  palette::panel.darker (0.35f), area.getX(), area.getBottom(), false);
    g.setGradientFill (leather);
    g.fillRoundedRectangle (area, corner);
    g.setTiledImageFill (tolexTexture(), 0, 0, 0.6f);
    g.fillRoundedRectangle (area, corner);

    // Bevel: dark outer line, bronze inner line.
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRoundedRectangle (area, corner, 1.5f);
    g.setColour (palette::panelEdge);
    g.drawRoundedRectangle (area.reduced (1.5f), corner - 1.0f, 1.0f);

    // Accent strip: a thin brass bar lit in the section's colour.
    const auto strip = area.withHeight (3.0f * scale).reduced (16.0f * scale, 0.0f).translated (0.0f, 1.5f);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRoundedRectangle (strip.expanded (0.0f, 2.0f * scale), 2.0f * scale);
    juce::ColourGradient bar (accent.brighter (0.4f), strip.getCentreX(), strip.getY(), accent.darker (0.3f), strip.getCentreX(), strip.getBottom(), false);
    g.setGradientFill (bar);
    g.fillRoundedRectangle (strip, 1.5f * scale);

    if (screws)
    {
        const float inset = 7.0f * scale, radius = 2.6f * scale;
        int k = 0;
        for (auto p : { area.getTopLeft().translated (inset, inset), area.getTopRight().translated (-inset, inset),
                        area.getBottomLeft().translated (inset, -inset), area.getBottomRight().translated (-inset, -inset) })
            paintScrew (g, p, radius, 0.4f + 0.9f * (float) k++);
    }
}

// Engraved cream lettering with a dark shadow, slightly spaced like stamped metal.
inline void paintTitle (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float fontSize,
                        juce::Justification justification = juce::Justification::centredLeft)
{
    const auto font = juce::Font (juce::FontOptions (fontSize, juce::Font::bold)).withExtraKerningFactor (0.08f);
    g.setFont (font);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawText (text, area.translated (0, 1), justification);
    g.setColour (palette::text);
    g.drawText (text, area, justification);
}
} // namespace rockstyle
