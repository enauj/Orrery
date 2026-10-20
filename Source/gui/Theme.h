#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

#include "Shapes.h"

namespace modlanes::theme
{

const juce::Colour bg { 0xff15171c };
const juce::Colour panel { 0xff1d2027 };
const juce::Colour panelLight { 0xff272b34 };
const juce::Colour gridLine { 0xff2e333d };
const juce::Colour gridStrong { 0xff414856 };
const juce::Colour text { 0xffd8dce4 };
const juce::Colour textDim { 0xff8a91a0 };
const juce::Colour gold { 0xffc4a96f };   // washed-out gold for ornaments and global controls
const juce::Colour accent = gold;

inline juce::Colour laneColour (int lane)
{
    static const juce::uint32 colours[] = { 0xff4fc3f7, 0xffffb74d, 0xffe57373 };
    return juce::Colour (colours[lane % 3]);
}

inline juce::Colour lfoColour (int lfo)
{
    static const juce::uint32 colours[] = { 0xffce93d8, 0xff80cbc4, 0xfffff176 };
    return juce::Colour (colours[lfo % 3]);
}


// Which renderer the window uses. Cached images must match it: CPU images for the
// software renderer, native (GPU) images for Direct2D, or every copy gets converted.
inline bool& useGpuRenderer()
{
    static bool gpu = false;
    return gpu;
}

inline const juce::ImageType& cacheImageType()
{
    static const juce::SoftwareImageType software;
    static const juce::NativeImageType native;
    return useGpuRenderer() ? static_cast<const juce::ImageType&> (native) : software;
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// Previous ornament style (kept to switch back to): a washed-out gold diamond
// lattice with small studs where the lines cross, like old tilework.
inline void paintLattice (juce::Graphics& g, juce::Rectangle<float> area, float corner = 5.0f)
{
    if (area.getWidth() < 8.0f || area.getHeight() < 8.0f)
        return;

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    g.reduceClipRegion (clip);

    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.fillRoundedRectangle (area, corner);

    constexpr float spacing = 14.0f;
    g.setColour (gold.withAlpha (0.1f));
    for (float d = -area.getHeight(); d < area.getWidth() + area.getHeight(); d += spacing)
    {
        g.drawLine (area.getX() + d, area.getBottom(), area.getX() + d + area.getHeight(), area.getY(), 1.0f);
        g.drawLine (area.getX() + d, area.getY(), area.getX() + d + area.getHeight(), area.getBottom(), 1.0f);
    }

    // Studs where the lines cross (every half spacing vertically, alternating offsets).
    g.setColour (gold.withAlpha (0.2f));
    int row = 0;
    for (float y = area.getBottom(); y > area.getY() - spacing; y -= spacing * 0.5f, ++row)
        for (float x = area.getX() + (row % 2 ? spacing * 0.5f : 0.0f); x < area.getRight() + spacing; x += spacing)
            g.fillEllipse (x - 1.2f, y - 1.2f, 2.4f, 2.4f);

    g.setColour (gold.withAlpha (0.16f));
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
}

// Panel background with a thin gold frame and small corner ornaments.
inline void paintPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner = 6.0f)
{
    g.setColour (panel);
    g.fillRoundedRectangle (r, corner);

    g.setColour (gold.withAlpha (0.14f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

    // Inner corner brackets with a tiny diamond
    const auto inner = r.reduced (4.0f);
    const float len = 9.0f;
    g.setColour (gold.withAlpha (0.35f));
    for (int i = 0; i < 4; ++i)
    {
        const bool right = i == 1 || i == 2, bottom = i >= 2;
        const float x = right ? inner.getRight() : inner.getX();
        const float y = bottom ? inner.getBottom() : inner.getY();
        const float dx = right ? -1.0f : 1.0f, dy = bottom ? -1.0f : 1.0f;
        g.drawLine (x, y, x + dx * len, y, 1.0f);
        g.drawLine (x, y, x, y + dy * len, 1.0f);

        juce::Path diamond;
        const float cx = x + dx * 3.5f, cy = y + dy * 3.5f;
        diamond.addQuadrilateral (cx, cy - 1.6f, cx + 1.6f, cy, cx, cy + 1.6f, cx - 1.6f, cy);
        g.fillPath (diamond);
    }
}

// One large astrolabe-like ornament (concentric rings, radial lines, ticks, an
// off-centre ecliptic ring and a star) coming in from the left side of the
// window. Rendered once per window size; the background and every empty panel
// area show the part of it that lies underneath them, so it reads as one figure.
inline juce::Image renderAstrolabe (int width, int height, float scale)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) width * scale)),
                       juce::jmax (1, juce::roundToInt ((float) height * scale)), true, cacheImageType());
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));

    // Centred just outside the bottom-left corner, so the rings arc up through the window.
    const float h = (float) height;
    const juce::Point<float> c (0.02f * (float) width, 1.04f * h);
    const float R = 0.62f * h;
    const auto ink = [] (float alpha) { return gold.withAlpha (juce::jmin (1.0f, alpha * 1.7f)); };
    const auto ring = [&] (float radius, float alpha, float thickness)
    {
        g.setColour (ink (alpha));
        g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, thickness);
    };
    const auto at = [&] (float radius, float degrees)
    {
        const float a = juce::degreesToRadians (degrees);
        return juce::Point<float> (c.x + radius * std::cos (a), c.y + radius * std::sin (a));
    };

    // Concentric rings of varying weight
    const struct { float r, alpha, thickness; } rings[] = {
        { 0.10f, 0.30f, 1.2f }, { 0.16f, 0.18f, 1.0f }, { 0.30f, 0.22f, 1.0f }, { 0.44f, 0.14f, 1.0f },
        { 0.58f, 0.26f, 1.4f }, { 0.62f, 0.12f, 1.0f }, { 0.84f, 0.22f, 1.2f }, { 0.90f, 0.22f, 1.2f },
        { 1.10f, 0.14f, 1.0f }, { 1.38f, 0.10f, 1.0f }, { 1.70f, 0.07f, 1.0f },
    };
    for (const auto& r : rings)
        ring (r.r * R, r.alpha, r.thickness);

    // Dashed ring
    {
        juce::Path circle;
        circle.addEllipse (c.x - 0.72f * R, c.y - 0.72f * R, 1.44f * R, 1.44f * R);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 7.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, circle, dashes, 2);
        g.setColour (ink (0.16f));
        g.fillPath (dashed);
    }

    // Radial lines: fine every 10 degrees, long ones every 30
    for (int d = 0; d < 360; d += 10)
    {
        const bool major = d % 30 == 0;
        g.setColour (ink (major ? 0.2f : 0.09f));
        g.drawLine ({ at ((major ? 0.10f : 0.30f) * R, (float) d), at ((major ? 1.38f : 0.84f) * R, (float) d) }, major ? 1.1f : 0.8f);
    }

    // Degree ticks between the two outer scale rings
    for (int d = 0; d < 360; d += 2)
    {
        const float inner = d % 10 == 0 ? 0.84f : 0.865f;
        g.setColour (ink (d % 10 == 0 ? 0.3f : 0.16f));
        g.drawLine ({ at (inner * R, (float) d), at (0.90f * R, (float) d) }, 1.0f);
    }

    // Off-centre "ecliptic" ring
    {
        const auto e = c.translated (0.13f * R, -0.07f * R);
        const float er = 0.52f * R;
        g.setColour (ink (0.2f));
        g.drawEllipse (e.x - er, e.y - er, er * 2.0f, er * 2.0f, 1.2f);
    }

    // Hexagram inscribed in the 0.58 ring
    for (int k = 0; k < 2; ++k)
    {
        juce::Path triangle;
        for (int i = 0; i < 3; ++i)
        {
            const auto p = at (0.58f * R, -90.0f + (float) k * 60.0f + (float) i * 120.0f);
            if (i == 0) triangle.startNewSubPath (p); else triangle.lineTo (p);
        }
        triangle.closeSubPath();
        g.setColour (ink (0.13f));
        g.strokePath (triangle, juce::PathStrokeType (1.0f));
    }

    // "Planets": small circles sitting on rings
    const struct { float r, degrees, size; } planets[] = {
        { 0.30f, -35.0f, 5.0f }, { 0.44f, 20.0f, 4.0f }, { 0.58f, -62.0f, 7.0f },
        { 0.72f, 8.0f, 4.5f }, { 1.10f, -18.0f, 6.0f }, { 1.38f, 12.0f, 5.0f },
    };
    for (const auto& p : planets)
    {
        const auto pos = at (p.r * R, p.degrees);
        g.setColour (bg);
        g.fillEllipse (juce::Rectangle<float> (p.size * 2.0f, p.size * 2.0f).withCentre (pos));
        g.setColour (ink (0.35f));
        g.drawEllipse (juce::Rectangle<float> (p.size * 2.0f, p.size * 2.0f).withCentre (pos), 1.0f);
        g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (pos));
    }

    // Fade out with distance from the centre.
    {
        juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
        const float cx = c.x * scale, cy = c.y * scale, reach = 1.9f * R * scale;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const float d = std::hypot ((float) x - cx, (float) y - cy) / reach;
                const auto p = pixels.getPixelColour (x, y);
                if (p.getAlpha() > 0)
                    pixels.setPixelColour (x, y, p.withMultipliedAlpha (0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, 1.0f - d)));
            }
    }

    return image;
}

// Cached astrolabe for the window `top` (UI thread only), at the display's scale.
struct Astrolabe
{
    const juce::Image& image;
    float scale;
};

inline Astrolabe astrolabe (juce::Component& top)
{
    static juce::Image cache;
    static int cachedWidth = 0, cachedHeight = 0;
    static float cachedScale = 0.0f;

    float scale = 1.0f;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top.getScreenBounds()))
        scale = (float) display->scale;

    if (top.getWidth() != cachedWidth || top.getHeight() != cachedHeight || scale != cachedScale)
    {
        cachedWidth = top.getWidth();
        cachedHeight = top.getHeight();
        cachedScale = scale;
        cache = renderAstrolabe (cachedWidth, cachedHeight, scale);
    }
    return { cache, cachedScale };
}

// Copies an image cached at screen resolution. The transform maps its pixels 1:1 onto
// the screen, but often at half-pixel positions (e.g. 75% interface scale on a 200%
// display), where JUCE would resample every pixel smoothly; a plain copy looks the same
// and is several times cheaper.
inline void drawCachedImage (juce::Graphics& g, const juce::Image& image, const juce::AffineTransform& transform)
{
    juce::Graphics::ScopedSaveState save (g);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (image, transform);
}

// Draws the part of the astrolabe that lies under `owner` (in owner coordinates).
inline void drawAstrolabeUnder (juce::Graphics& g, juce::Component& owner, float opacity)
{
    auto* top = owner.getTopLevelComponent();
    const auto origin = owner.getLocalPoint (top, juce::Point<float>());
    const float unitsPerPixel = owner.getLocalPoint (top, juce::Point<float> (1.0f, 0.0f)).x - origin.x;
    const auto a = astrolabe (*top);
    juce::Graphics::ScopedSaveState save (g);
    g.setOpacity (opacity);
    drawCachedImage (g, a.image, juce::AffineTransform::scale (unitsPerPixel / a.scale).translated (origin));
}

// Empty panel space: a dark inset showing the part of the astrolabe that lies
// beneath it. `owner` is the component being painted (to find the window position).
inline void paintHatch (juce::Graphics& g, juce::Component& owner, juce::Rectangle<float> area, float corner = 5.0f)
{
    if (area.getWidth() < 8.0f || area.getHeight() < 8.0f)
        return;

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    g.reduceClipRegion (clip);

    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRoundedRectangle (area, corner);

    drawAstrolabeUnder (g, owner, 1.0f);

    g.setColour (gold.withAlpha (0.16f));
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
}

// Fine grain tile for the window background.
inline juce::Image makeGrainTile()
{
    juce::Image tile (juce::Image::ARGB, 96, 96, true, cacheImageType());
    juce::Random random (1234);
    for (int y = 0; y < tile.getHeight(); ++y)
        for (int x = 0; x < tile.getWidth(); ++x)
        {
            const float n = random.nextFloat();
            tile.setPixelAt (x, y, n > 0.5f ? juce::Colours::white.withAlpha ((n - 0.5f) * 0.05f)
                                            : juce::Colours::black.withAlpha ((0.5f - n) * 0.12f));
        }
    return tile;
}

// Polyline of a block's output across `area`, with 0 on the vertical centre.
inline juce::Path makeBlockCurve (const Block& b, juce::Rectangle<float> area, int resolution)
{
    juce::Path path;
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;

    for (int i = 0; i <= resolution; ++i)
    {
        const float t = (float) i / (float) resolution;
        const float v = juce::jlimit (-1.0f, 1.0f, blockValue (b, std::min (t, 0.9999f)));
        const float x = area.getX() + t * area.getWidth();
        const float y = mid - v * half;

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    return path;
}

//==============================================================================
// Slider properties read by the LookAndFeel to draw live LFO modulation.
inline const juce::Identifier kModPos { "modPos" }, kModColour { "modColour" };

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, bg);
        setColour (ComboBox::backgroundColourId, panelLight);
        setColour (ComboBox::outlineColourId, gridStrong);
        setColour (ComboBox::textColourId, text);
        setColour (ComboBox::arrowColourId, textDim);
        setColour (PopupMenu::backgroundColourId, panel);
        setColour (PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
        setColour (PopupMenu::textColourId, text);
        setColour (PopupMenu::highlightedTextColourId, text);
        setColour (Label::textColourId, text);
        setColour (Slider::textBoxTextColourId, text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::rotarySliderFillColourId, accent);
        setColour (Slider::trackColourId, accent.withAlpha (0.35f));
        setColour (ToggleButton::textColourId, text);
        setColour (ToggleButton::tickColourId, accent);
        setColour (TooltipWindow::backgroundColourId, panelLight);
        setColour (TooltipWindow::textColourId, text);
        setColour (TextButton::buttonColourId, panelLight);
        setColour (TextButton::buttonOnColourId, accent.withAlpha (0.4f));
        setColour (TextButton::textColourOffId, text);
        setColour (TextButton::textColourOnId, text);
        setColour (ComboBox::outlineColourId, gridLine);
        setColour (BubbleComponent::backgroundColourId, panelLight);
        setColour (BubbleComponent::outlineColourId, gridStrong);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (11.5f, true); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        using namespace juce;
        const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const PathStrokeType stroke (3.0f, PathStrokeType::curved, PathStrokeType::rounded);

        Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (gridStrong);
        g.strokePath (track, stroke);

        // Bipolar ranges fill from the zero point.
        float from = startAngle;
        if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
            from = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

        Path arc;
        arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (slider.findColour (Slider::rotarySliderFillColourId));
        g.strokePath (arc, stroke);

        const auto tip = c.getPointOnCircumference (radius - 5.0f, angle);
        g.setColour (text);
        g.drawLine ({ c.getPointOnCircumference (radius * 0.2f, angle), tip }, 2.0f);

        // Live LFO modulation (see setModulation): an inner arc from the set value to
        // where the LFO pushes it right now, with a dot at the pushed position.
        const auto& props = slider.getProperties();
        if (const auto* mod = props.getVarPointer (kModPos))
        {
            const float modAngle = startAngle + (float) (double) *mod * (endAngle - startAngle);
            const auto colour = Colour ((uint32) (int64) props[kModColour]);
            const float outer = radius - 5.0f;

            Path modArc;
            modArc.addCentredArc (c.x, c.y, outer, outer, 0.0f, jmin (angle, modAngle), jmax (angle, modAngle), true);
            g.setColour (colour);
            g.strokePath (modArc, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (c.getPointOnCircumference (outer, modAngle)));
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearBar)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }

        const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
        g.setColour (panelLight);
        g.fillRoundedRectangle (r, 3.0f);

        // Bipolar ranges fill from zero.
        float from = r.getX();
        if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
            from = (float) x + (float) slider.valueToProportionOfLength (0.0) * (float) width;

        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (from, sliderPos), r.getY(),
                                                                           juce::jmax (from, sliderPos), r.getBottom()), 3.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto r = button.getLocalBounds().toFloat().reduced (1.0f);
        const auto onColour = button.findColour (juce::ToggleButton::tickColourId);
        const bool on = button.getToggleState();

        g.setColour (on ? onColour.withAlpha (0.25f) : panelLight.brighter (highlighted ? 0.08f : 0.0f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? onColour : gridStrong);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

        g.setColour (on ? text : textDim);
        g.setFont (font (12.0f));
        g.drawText (button.getButtonText(), r, juce::Justification::centred);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }
    juce::Font getSliderPopupFont (juce::Slider&) override { return font (12.0f); }
};

// Small helpers for consistent controls. Value popups for knobs are enabled by
// the editor so they render inside the plugin window rather than as desktop windows.
// Shows where an LFO pushes this knob right now (value in slider units), or clears
// it with std::nullopt. Repaints only when the shown position actually moves.
inline void setModulation (juce::Slider& s, std::optional<double> value, juce::Colour colour = {})
{
    auto& props = s.getProperties();
    if (! value)
    {
        if (props.contains (kModPos))
        {
            props.remove (kModPos);
            props.remove (kModColour);
            s.repaint();
        }
        return;
    }

    const double pos = juce::jlimit (0.0, 1.0, s.valueToProportionOfLength (juce::jlimit (s.getMinimum(), s.getMaximum(), *value)));
    const auto* old = props.getVarPointer (kModPos);
    const auto argb = (juce::int64) colour.getARGB();
    if (old != nullptr && std::abs ((double) *old - pos) < 0.002 && (juce::int64) props[kModColour] == argb)
        return;

    props.set (kModPos, pos);
    props.set (kModColour, argb);
    s.repaint();
}

inline void setupKnob (juce::Slider& s, juce::Colour colour)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::rotarySliderFillColourId, colour);
}

inline void setupBar (juce::Slider& s, juce::Colour colour)
{
    s.setSliderStyle (juce::Slider::LinearBar);
    s.setColour (juce::Slider::trackColourId, colour.withAlpha (0.3f));
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

inline void setupCaption (juce::Label& l, const juce::String& textToShow)
{
    l.setText (textToShow, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setColour (juce::Label::textColourId, textDim);
    l.setFont (font (11.0f));
}

} // namespace modlanes::theme
