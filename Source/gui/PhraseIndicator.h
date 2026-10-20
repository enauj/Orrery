#pragma once

#include "Params.h"
#include "PluginProcessor.h"
#include "Theme.h"

namespace modlanes
{

// Elektron-style position LEDs: one square per bar of the master length (4 when
// it is off). The current bar's square flashes on every beat, brightest on the downbeat.
class PhraseIndicator : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PhraseIndicator (ModLanesProcessor& p) : proc (p)
    {
        setTooltip ("Position in the pattern: one square per bar of the Alignment, flashing on each beat");
    }

    // Repaints only when what it shows changes (the beat flash fades in 10 levels).
    void refresh()
    {
        const bool running = proc.transportRunning.load();
        const double ppq = proc.playPpq.load(), beatLen = proc.beatLength.load();
        const int beats = (int) std::floor (ppq / beatLen);
        const int fade = juce::jlimit (0, 9, (int) ((ppq / beatLen - beats) * 10.0));
        const int key = running ? (beats * 10 + fade) * 32 + getBars() : -getBars();
        if (key != shownKey)
        {
            shownKey = key;
            repaint();
        }
    }

    int getBars() const
    {
        const int master = (int) proc.apvts.getRawParameterValue (params::masterLength)->load();
        return master > 0 ? master : 4;
    }

    void paint (juce::Graphics& g) override
    {
        const int bars = getBars();
        const bool running = proc.transportRunning.load();
        const double ppq = proc.playPpq.load();
        const double barLen = proc.barLength.load(), beatLen = proc.beatLength.load();

        const double barsElapsed = std::floor (ppq / barLen);
        const int bar = (int) (barsElapsed - bars * std::floor (barsElapsed / bars));
        const double inBar = ppq - barsElapsed * barLen;
        const int beat = (int) (inBar / beatLen);
        const float beatFrac = (float) (inBar / beatLen - beat);

        // Squares in groups of four (like bar lines), stacked in two rows above 8 bars,
        // with the bar.beat readout on the right.
        auto area = getLocalBounds().toFloat();
        auto label = area.removeFromRight (34.0f);
        area.removeFromRight (6.0f);

        const int rows = bars > 8 ? 2 : 1;
        const int perRow = (bars + rows - 1) / rows;
        const int groups = (perRow + 3) / 4;
        const float gap = 3.0f, groupGap = 10.0f, rowGap = 3.0f;
        const float size = juce::jmin ((area.getHeight() - rowGap * (float) (rows - 1)) / (float) rows,
                                       (area.getWidth() - gap * (float) (perRow - groups) - groupGap * (float) (groups - 1)) / (float) perRow);
        const float rowWidth = size * (float) perRow + gap * (float) (perRow - groups) + groupGap * (float) (groups - 1);
        const auto block = area.removeFromRight (rowWidth).withSizeKeepingCentre (rowWidth, size * (float) rows + rowGap * (float) (rows - 1));

        for (int i = 0; i < bars; ++i)
        {
            const int row = i / perRow, column = i % perRow;
            const float x = block.getX() + (float) column * (size + gap) + (float) (column / 4) * (groupGap - gap);
            const float y = block.getY() + (float) row * (size + rowGap);
            const auto r = juce::Rectangle<float> (x, y, size, size).reduced (1.0f);

            g.setColour (theme::panelLight);
            g.fillRoundedRectangle (r, 2.5f);

            if (running && i == bar)
            {
                // Flash on each beat and fade until the next one; the downbeat is brighter.
                const float intensity = (beat == 0 ? 1.0f : 0.7f) * (1.0f - 0.75f * beatFrac);
                g.setColour (theme::gold.withAlpha (0.15f + 0.85f * intensity));
                g.fillRoundedRectangle (r, 2.5f);
                g.setColour (theme::gold.withAlpha (0.25f * intensity));
                g.fillRoundedRectangle (r.expanded (1.5f), 3.5f);
            }
            else if (running && i < bar)
            {
                g.setColour (theme::gold.withAlpha (0.14f));
                g.fillRoundedRectangle (r, 2.5f);
            }

            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);
        }

        g.setColour (running ? theme::text : theme::textDim);
        g.setFont (theme::font (11.0f, true));
        g.drawText (running ? juce::String (bar + 1) + "." + juce::String (beat + 1) : juce::String ("-.-"),
                    label, juce::Justification::centred);
    }

private:
    ModLanesProcessor& proc;
    int shownKey = 0;
};

} // namespace modlanes
