#pragma once

#include "LiveState.h"
#include "Params.h"
#include "Theme.h"

#include <numeric>

namespace modlanes
{

// The orbits as planets: each circles once per loop, so polymeters and
// polyrhythms become visible. Shows when the orbits naturally line up again and
// can set the Alignment to that.
class OrreryView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit OrreryView (ModLanesProcessor& p) : proc (p)
    {
        setOpaque (true);
    }

    // Loop length of one orbit in 1/48 quarter notes (exact for triplets), or 0 when
    // it does not repeat on the beat grid (free rate, trigger and follower modes, off).
    long long loopTicks (int lane) const
    {
        const auto value = [&] (const char* name) { return proc.apvts.getRawParameterValue (params::lane (lane, name))->load(); };
        if (value ("on") < 0.5f || value ("sync") < 0.5f || (int) value ("mode") != params::Sequence)
            return 0;

        const int steps = juce::jlimit (1, kMaxSteps, (int) value ("steps"));
        const double beats = params::stepBeats ((int) value ("rate"), (int) value ("fit"), steps, proc.barLength.load()) * steps;
        return juce::jmax (1LL, (long long) std::llround (beats * 48.0));
    }

    // Bars until all synced orbits are back at their start together (0 = nothing to align).
    double naturalAlignmentBars() const
    {
        long long period = 0;
        for (int i = 0; i < kNumLanes; ++i)
            if (const auto ticks = loopTicks (i); ticks > 0)
                period = period == 0 ? ticks : std::lcm (period, ticks);

        return period == 0 ? 0.0 : (double) period / (proc.barLength.load() * 48.0);
    }

    void refresh (bool live)
    {
        // Everything repaints when the text or an orbit's on/off state changes; otherwise
        // only the planets that moved by about a degree, around their old and new spots.
        juce::int64 key = live ? 1 : 0;
        for (int i = 0; i < kNumLanes; ++i)
            key = key * 2 + (proc.apvts.getRawParameterValue (params::lane (i, "on"))->load() >= 0.5f ? 1 : 0);
        key = key * 1000 + juce::roundToInt (naturalAlignmentBars() * 8.0);
        key = key * 20 + (int) proc.apvts.getRawParameterValue (params::masterLength)->load();

        if (key != shownKey)
        {
            shownKey = key;
            liveView = live;
            for (int i = 0; i < kNumLanes; ++i)
                shownDegrees[(size_t) i] = degrees (i);
            repaint();
            return;
        }

        for (int i = 0; i < kNumLanes; ++i)
            if (const int d = degrees (i); d != shownDegrees[(size_t) i])
            {
                const auto before = planetArea (i, shownDegrees[(size_t) i]);
                shownDegrees[(size_t) i] = d;
                repaint (before.getUnion (planetArea (i, d)));
            }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg);
        theme::paintPanel (g, getLocalBounds().toFloat());

        auto area = getLocalBounds().reduced (10, 8);
        g.setColour (theme::gold.withAlpha (0.8f));
        g.setFont (theme::font (10.5f, true));
        g.drawText ("ORRERY", area.removeFromTop (14), juce::Justification::centredLeft);

        textArea = area.removeFromBottom (30);
        const auto [centre, outer] = layout();

        // Alignment marker at the top
        g.setColour (theme::gold.withAlpha (0.25f));
        g.drawLine (centre.x, centre.y, centre.x, centre.y - outer - 2.0f, 1.0f);
        juce::Path marker;
        marker.addQuadrilateral (centre.x, centre.y - outer - 6.0f, centre.x + 3.0f, centre.y - outer - 3.0f,
                                 centre.x, centre.y - outer, centre.x - 3.0f, centre.y - outer - 3.0f);
        g.setColour (theme::gold.withAlpha (0.7f));
        g.fillPath (marker);

        for (int i = 0; i < kNumLanes; ++i)
        {
            const float radius = orbitRadius (i, outer);
            const bool on = proc.apvts.getRawParameterValue (params::lane (i, "on"))->load() >= 0.5f;
            const auto colour = theme::laneColour (i).withMultipliedAlpha (on ? 1.0f : 0.3f);

            g.setColour (colour.withAlpha (0.28f * colour.getFloatAlpha()));
            g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

            const auto planet = planetCentre (i, (float) shownDegrees[(size_t) i]);
            const float planetSize = 7.0f + (float) i;
            g.setColour (theme::bg);
            g.fillEllipse (juce::Rectangle<float> (planetSize + 3.0f, planetSize + 3.0f).withCentre (planet));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (planetSize, planetSize).withCentre (planet));
        }

        g.setColour (theme::gold.withAlpha (0.8f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (centre));

        // Alignment text
        const double natural = naturalAlignmentBars();
        const int alignment = (int) proc.apvts.getRawParameterValue (params::masterLength)->load();
        juce::String line1, line2;
        if (natural <= 0.0)
            line1 = "no synced orbits";
        else if (natural > 999.0)
            line1 = "realign: never (>999 bars)";
        else
            line1 = "realign every " + formatBars (natural);

        canApply = natural >= 1.0 && natural <= 16.0 && std::abs (natural - std::round (natural)) < 1.0e-6
                && alignment != (int) std::round (natural);
        line2 = alignment > 0 ? "Alignment: " + formatBars (alignment) + (canApply ? "  (click: set)" : "")
                              : (canApply ? "click to set Alignment" : "Alignment off");

        g.setColour (theme::text.withAlpha (0.85f));
        g.setFont (theme::font (10.5f));
        g.drawText (line1, textArea.removeFromTop (15), juce::Justification::centred);
        g.setColour (canApply ? theme::gold : theme::textDim);
        g.drawText (line2, textArea, juce::Justification::centred);

        juce::String tip = "Each orbit circles once per loop. ";
        for (int i = 0; i < kNumLanes; ++i)
            if (const auto ticks = loopTicks (i); ticks > 0)
                tip << "Orbit " << (i + 1) << ": " << formatBars ((double) ticks / (proc.barLength.load() * 48.0)) << ". ";
        setTooltip (tip + "Different step counts or Fit settings make polymeters and polyrhythms.");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! canApply || e.y < getHeight() - 40)
            return;

        if (auto* param = proc.apvts.getParameter (params::masterLength))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) std::round (naturalAlignmentBars())));
            param->endChangeGesture();
        }
    }

private:
    // Circle of the planets and the radius of each orbit on it.
    std::pair<juce::Point<float>, float> layout() const
    {
        auto area = getLocalBounds().reduced (10, 8);
        area.removeFromTop (14);
        area.removeFromBottom (30);
        return { area.toFloat().getCentre(), (float) juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 4.0f };
    }

    static float orbitRadius (int lane, float outer) { return outer * (0.42f + 0.29f * (float) lane); }

    int degrees (int lane) const { return juce::roundToInt (planetAngle (lane) / juce::MathConstants<float>::twoPi * 360.0f); }

    juce::Point<float> planetCentre (int lane, float deg) const
    {
        const auto [centre, outer] = layout();
        const float radius = orbitRadius (lane, outer), angle = juce::degreesToRadians (deg);
        return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
    }

    juce::Rectangle<int> planetArea (int lane, int deg) const
    {
        const float size = 7.0f + (float) lane + 3.0f;
        return juce::Rectangle<float> (size, size).withCentre (planetCentre (lane, (float) deg)).getSmallestIntegerContainer().expanded (2);
    }

    // Clockwise from the top (the alignment point); idle orbits rest at their phase.
    float planetAngle (int lane) const
    {
        const int steps = juce::jlimit (1, kMaxSteps, (int) proc.apvts.getRawParameterValue (params::lane (lane, "steps"))->load());
        const float position = liveView ? livePosition (proc, lane, steps, true) : -1.0f;
        const float turn = position >= 0.0f ? position / (float) steps
                                            : proc.apvts.getRawParameterValue (params::lane (lane, "phase"))->load();
        return turn * juce::MathConstants<float>::twoPi;
    }

    static juce::String formatBars (double bars)
    {
        const double rounded = std::round (bars);
        if (std::abs (bars - rounded) < 1.0e-6)
            return juce::String ((int) rounded) + (rounded == 1.0 ? " bar" : " bars");
        return juce::String (bars, 2) + " bars";
    }

    ModLanesProcessor& proc;
    juce::Rectangle<int> textArea;
    juce::int64 shownKey = -1;
    std::array<int, kNumLanes> shownDegrees {};
    bool liveView = true;
    bool canApply = false;
};

} // namespace modlanes
