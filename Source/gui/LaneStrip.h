#pragma once

#include "LiveState.h"
#include "MidiExport.h"
#include "Params.h"
#include "StepGrid.h"

namespace modlanes
{

// One lane: mode and output settings on the left, the step grid, and an output meter.
class LaneStrip : public juce::Component
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    LaneStrip (ModLanesProcessor& p, int laneIndex, MapContext& mapContext)
        : grid (p, laneIndex, mapContext), proc (p), lane (laneIndex),
          modeParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "mode"))),
          syncParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "sync"))),
          meter (p, laneIndex), dragHandle (p, laneIndex), overlay (*this, mapContext)
    {
        const auto colour = theme::laneColour (lane);
        auto& apvts = p.apvts;
        const auto id = [this] (const char* name) { return params::lane (lane, name); };
        fitParam = apvts.getRawParameterValue (id ("fit"));

        title.setText ("ORBIT " + juce::String (lane + 1), juce::dontSendNotification);
        title.setFont (theme::font (13.0f, true));
        title.setColour (juce::Label::textColourId, colour);
        addAndMakeVisible (title);

        for (auto* b : { &onButton, &bipolarButton, &invertButton, &syncButton })
        {
            b->setColour (juce::ToggleButton::tickColourId, colour);
            addAndMakeVisible (*b);
        }
        bipolarButton.setTooltip ("Bipolar: 0 sits at CC 64, so inverted envelopes and bipolar LFOs swing both ways.");
        invertButton.setTooltip ("Flip the orbit upside down. With Offset at 100% this turns a follower or trigger into ducking.");
        syncButton.setTooltip ("Sync: Rate steps through note values at the host tempo. Off: Rate is free, in steps per second.");

        // Blocks / Points view switch, bound to the lane's "points" parameter.
        pointsParameter = apvts.getParameter (id ("points"));
        for (auto* b : { &blocksButton, &pointsButton })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.35f));
            b->onClick = [this, b]
            {
                pointsParameter->beginChangeGesture();
                pointsParameter->setValueNotifyingHost (b == &pointsButton ? 1.0f : 0.0f);
                pointsParameter->endChangeGesture();
            };
            addAndMakeVisible (*b);
        }
        blocksButton.setTooltip ("Edit the orbit as module blocks");
        pointsButton.setTooltip ("Edit the orbit as a breakpoint envelope (rendered from the blocks the first time)");
        blocksButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        pointsButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

        mode.addItemList (params::modeNames, 1);
        mode.setTooltip ("Sequence: steps follow the transport. MIDI Trigger: notes play blocks once. "
                         "Audio Trigger: hits in the audio play blocks. Audio Follow: outputs the audio level.");
        source.addItemList (params::sourceNames, 1);
        source.setTooltip ("Which audio the follower listens to");
        for (auto* c : { &mode, &source })
            addAndMakeVisible (*c);

        for (auto* s : { &steps, &channel, &cc })
        {
            theme::setupBar (*s, colour);
            addAndMakeVisible (*s);
        }

        const std::pair<Knob*, const char*> knobs[] = { { &rateSynced, "Rate" }, { &rateFree, "Rate" },
                                                        { &depth, "Depth" }, { &offset, "Offset" }, { &smooth, "Smooth" },
                                                        { &blend, "Blend" }, { &phase, "Phase" }, { &fit, "Fit" },
                                                        { &threshold, "Thresh" }, { &velocity, "Velocity" },
                                                        { &attack, "Attack" }, { &release, "Release" } };
        for (auto [knob, name] : knobs)
        {
            theme::setupKnob (knob->slider, colour);
            theme::setupCaption (knob->label, name);
            addAndMakeVisible (knob->slider);
            addAndMakeVisible (knob->label);
        }
        blend.slider.setTooltip ("Crossfade between neighbouring blocks, in steps. In trigger modes: fade on retrigger and at the end.");
        velocity.slider.setTooltip ("How much note velocity (or hit strength) scales the shape");
        phase.slider.setTooltip ("Shifts where the orbit's loop starts (0-100% of its length)");
        fit.slider.setTooltip ("Squeeze all steps into exactly this many bars, whatever the step count. "
                               "Orbits with different step counts but the same Fit play polyrhythms (e.g. 5 against 4).");

        attachments.add (new APVTS::ButtonAttachment (apvts, id ("on"), onButton));
        attachments.add (new APVTS::ButtonAttachment (apvts, id ("bipolar"), bipolarButton));
        attachments.add (new APVTS::ButtonAttachment (apvts, id ("invert"), invertButton));
        attachments.add (new APVTS::ComboBoxAttachment (apvts, id ("mode"), mode));
        attachments.add (new APVTS::ComboBoxAttachment (apvts, id ("source"), source));
        attachments.add (new APVTS::ButtonAttachment (apvts, id ("sync"), syncButton));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("rate"), rateSynced.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("hz"), rateFree.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("steps"), steps));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("ch"), channel));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("cc"), cc));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("depth"), depth.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("offset"), offset.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("smooth"), smooth.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("blend"), blend.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("phase"), phase.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("fit"), fit.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("thresh"), threshold.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("vel"), velocity.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("attack"), attack.slider));
        attachments.add (new APVTS::SliderAttachment (apvts, id ("release"), release.slider));

        labelBar (steps, "Steps");
        labelBar (channel, "Ch");
        labelBar (cc, "CC");
        // Both rate knobs also show how often the whole lane loops, in Hz.
        const auto loopHz = [this] (double stepsPerSecond)
        {
            const int count = juce::jmax (1, (int) proc.apvts.getRawParameterValue (params::lane (lane, "steps"))->load());
            const double hz = stepsPerSecond / count;
            return juce::String (hz, hz < 0.1 ? 3 : 2) + " Hz";
        };
        rateFree.slider.textFromValueFunction = [loopHz] (double v)
        {
            return juce::String (v, v < 10.0 ? 2 : 1) + " steps/s  (" + loopHz (v) + ")";
        };
        rateSynced.slider.textFromValueFunction = [this, loopHz] (double v)
        {
            const int index = juce::jlimit (0, params::rateNames.size() - 1, juce::roundToInt (v));
            const double stepsPerSecond = proc.hostBpm.load() / 60.0 / params::kRateBeats[index];
            return params::rateNames[index] + "  (" + loopHz (stepsPerSecond) + ")";
        };
        rateSynced.slider.setTooltip ("Length of one step");
        rateFree.slider.setTooltip ("Free rate in steps per second");
        phase.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        phase.slider.setDoubleClickReturnValue (true, 0.0);

        addAndMakeVisible (grid);
        addAndMakeVisible (meter);
        addAndMakeVisible (dragHandle);
        addAndMakeVisible (overlay);

        updateModeControls();
    }

    // live == false stops the playhead, value dot and meter animation to save CPU.
    void refresh (bool live)
    {
        if (live != liveView)
        {
            liveView = live;
            grid.setLiveView (live);
            grid.repaint();
        }

        if ((int) modeParam->load() != shownMode || (syncParam->load() >= 0.5f) != shownSync)
            updateModeControls();

        if (const bool fitOn = fitParam->load() >= 0.5f; fitOn != shownFit)
        {
            shownFit = fitOn;
            for (auto* k : { &rateSynced, &rateFree })
            {
                k->slider.setAlpha (fitOn ? 0.35f : 1.0f);
                k->label.setText (fitOn ? "(Fit)" : "Rate", juce::dontSendNotification);
            }
        }

        // A switched-off lane fades its controls (the grid shows LANE OFF).
        if (const bool on = onButton.getToggleState(); on != shownOn)
        {
            shownOn = on;
            for (auto* c : getChildren())
                if (c != &onButton && c != &title && c != &overlay)
                    c->setAlpha (on ? 1.0f : 0.45f);
            grid.repaint();
        }

        const bool points = pointsParameter->getValue() >= 0.5f;
        blocksButton.setToggleState (! points, juce::dontSendNotification);
        pointsButton.setToggleState (points, juce::dontSendNotification);

        grid.poll();

        // The output meter stays live even with Live view off; it only repaints when its value changes.
        meter.refresh();

        if (const int v = proc.getRoutingVersion(); v != seenRoutingVersion)
        {
            seenRoutingVersion = v;
            overlay.repaint();
        }
    }

    // Called at the display's refresh rate: moves the playhead while the orbit
    // runs, plus once when it stops so the playhead disappears.
    void animate()
    {
        const bool active = proc.laneActive[lane].load();
        if (liveView && (active || active != gridWasActive))
            grid.animate();
        gridWasActive = active;

        animateModulation();
    }

    // Shows on Depth, Offset and Rate where the LFOs push them right now.
    void animateModulation()
    {
        grid.animateModulation();
        liveMod.update();

        const auto show = [this] (Knob& knob, ModTarget target, auto&& modulatedValue)
        {
            float total = 0.0f;
            int lfo = 0;
            if (liveView && liveMod.sum ({ target, lane, 0, 0.0f }, total, lfo))
                theme::setModulation (knob.slider, modulatedValue (knob.slider.getValue(), total), theme::lfoColour (lfo));
            else
                theme::setModulation (knob.slider, std::nullopt);
        };

        const auto plus = [] (double v, float m) { return v + m; };
        show (depth, ModTarget::LaneDepth, plus);
        show (offset, ModTarget::LaneOffset, plus);

        // Rate: a speed multiplier of up to +-2 octaves (see processBlock).
        const auto multiplier = [] (float m) { return std::exp2 (2.0 * juce::jlimit (-1.0f, 1.0f, m)); };
        show (rateFree, ModTarget::LaneRate, [&] (double hz, float m) { return hz * multiplier (m); });
        show (rateSynced, ModTarget::LaneRate, [&] (double index, float m)
        {
            // Fractional index of the faster/slower step length, interpolated in octaves.
            const double beats = params::kRateBeats[juce::jlimit (0, 9, (int) index)] / multiplier (m);
            if (beats >= params::kRateBeats[0]) return 0.0;
            for (int i = 0; i < 9; ++i)
                if (beats >= params::kRateBeats[i + 1])
                    return i + std::log2 (params::kRateBeats[i] / beats) / std::log2 (params::kRateBeats[i] / params::kRateBeats[i + 1]);
            return 9.0;
        });
    }

    void mapStateChanged()
    {
        overlay.repaint();
        grid.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::paintPanel (g, r);
        g.setColour (theme::laneColour (lane));
        g.fillRoundedRectangle (r.removeFromLeft (3.0f).reduced (0.0f, 8.0f), 1.5f);

        // Texture on the knob cells that the current mode leaves empty.
        theme::paintHatch (g, *this, emptyKnobArea.toFloat().reduced (4.0f, 6.0f));
    }

    void resized() override
    {

        auto r = getLocalBounds().reduced (10, 8);
        auto header = r.removeFromLeft (400);
        r.removeFromLeft (8);
        auto meterColumn = r.removeFromRight (36);
        dragHandle.setBounds (meterColumn.removeFromTop (26));
        meterColumn.removeFromTop (4);
        meter.setBounds (meterColumn);
        r.removeFromRight (6);
        grid.setBounds (r);

        overlay.setBounds (getLocalBounds().withRight (header.getRight() + 4));
        auto column = header.removeFromLeft (190);
        header.removeFromLeft (10);

        const auto row = [&column]
        {
            auto result = column.removeFromTop (22);
            column.removeFromTop (5);
            return result;
        };
        const auto split = [] (juce::Rectangle<int> area, juce::Component& left, juce::Component& right, float leftShare)
        {
            left.setBounds (area.removeFromLeft (juce::roundToInt ((float) area.getWidth() * leftShare) - 3));
            area.removeFromLeft (6);
            right.setBounds (area);
        };

        auto top = row();
        title.setBounds (top.removeFromLeft (64));
        onButton.setBounds (top.removeFromRight (40));
        top.removeFromRight (6);
        pointsButton.setBounds (top.removeFromRight (top.getWidth() / 2));
        blocksButton.setBounds (top);

        auto modeRow = row();
        if (source.isVisible())
            split (modeRow, mode, source, 0.55f);
        else
            mode.setBounds (modeRow);

        split (row(), steps, syncButton, 0.5f);
        split (row(), channel, cc, 0.5f);
        split (row(), bipolarButton, invertButton, 0.55f);

        // Two rows of four knobs: output knobs on top, mode-specific ones below.
        const int knobRowHeight = header.getHeight() / 2;
        auto upper = header.removeFromTop (knobRowHeight);
        auto lower = header;
        const int w = upper.getWidth() / 4;

        const auto place = [w] (juce::Rectangle<int>& area, Knob& k)
        {
            auto cell = area.removeFromLeft (w);
            k.label.setBounds (cell.removeFromBottom (14));
            k.slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), cell.getHeight()), cell.getHeight()));
        };

        // The synced and free rate knobs share one spot; only one is visible.
        place (upper, rateSynced);
        rateFree.slider.setBounds (rateSynced.slider.getBounds());
        rateFree.label.setBounds (rateSynced.label.getBounds());

        for (auto* k : { &depth, &offset, &smooth })
            place (upper, *k);

        place (lower, blend);
        for (auto* k : contextKnobs())
            place (lower, *k);
        emptyKnobArea = lower;
    }

    StepGrid grid;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;

        void setVisible (bool v) { slider.setVisible (v); label.setVisible (v); }
    };

    std::vector<Knob*> contextKnobs()
    {
        switch (shownMode)
        {
            case params::MidiTrigger:  return { &fit, &velocity };
            case params::AudioTrigger: return { &threshold, &velocity, &release };
            case params::AudioFollow:  return { &threshold, &attack, &release };
            default:                   return { &phase, &fit };
        }
    }

    void updateModeControls()
    {
        shownMode = (int) modeParam->load();
        shownSync = syncParam->load() >= 0.5f;
        rateSynced.setVisible (shownSync);
        rateFree.setVisible (! shownSync);

        for (auto* k : { &phase, &fit, &threshold, &velocity, &attack, &release })
            k->setVisible (false);
        for (auto* k : contextKnobs())
            k->setVisible (true);

        source.setVisible (params::isAudioMode (shownMode));
        release.label.setText (shownMode == params::AudioTrigger ? "Hold" : "Release", juce::dontSendNotification);
        resized();
        repaint();
    }

    static void labelBar (juce::Slider& s, const juce::String& prefix)
    {
        s.textFromValueFunction = [prefix] (double v) { return prefix + " " + juce::String ((int) v); };
        s.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789").getDoubleValue(); };
        s.updateText();
    }

    struct Meter : public juce::Component, public juce::SettableTooltipClient
    {
        Meter (ModLanesProcessor& p, int l)
            : proc (p), lane (l), ccParam (p.apvts.getRawParameterValue (params::lane (l, "cc")))
        {
            setOpaque (true);
        }

        void refresh()
        {
            const int value = juce::roundToInt (proc.laneOutput[lane].load() * 127.0f);
            const bool active = proc.laneActive[lane].load();
            const int controller = (int) ccParam->load();
            if (value != shownValue || active != shownActive || controller != shownController)
            {
                shownValue = value;
                shownActive = active;
                shownController = controller;
                setTooltip ("Output, sent as CC " + juce::String (controller));
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::panel);
            auto r = getLocalBounds().toFloat();
            const float value = (float) shownValue / 127.0f;
            const bool active = shownActive;

            auto label = r.removeFromBottom (14.0f);
            g.setColour (theme::textDim);
            g.setFont (theme::font (10.0f));
            g.drawText (juce::String (juce::roundToInt (value * 127.0f)), label, juce::Justification::centred);

            auto bar = r.reduced (9.0f, 0.0f);
            g.setColour (theme::panelLight);
            g.fillRoundedRectangle (bar, 3.0f);

            const auto colour = theme::laneColour (lane).withAlpha (active ? 0.9f : 0.3f);
            g.setGradientFill (juce::ColourGradient (colour, 0.0f, bar.getBottom(), colour.withAlpha (0.45f), 0.0f, bar.getY(), false));
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * value), 3.0f);
        }

        ModLanesProcessor& proc;
        const int lane;
        std::atomic<float>* ccParam;
        int shownValue = -1, shownController = -1;
        bool shownActive = false;
    };

    // Draws modulation dots on routed controls and, in map mode, turns Depth,
    // Offset and Rate into click targets. Clicks elsewhere pass through.
    struct MapOverlay : public juce::Component
    {
        MapOverlay (LaneStrip& s, MapContext& c) : strip (s), context (c) {}

        std::array<std::pair<ModTarget, juce::Rectangle<int>>, 3> targets() const
        {
            const auto area = [] (const Knob& k) { return k.slider.getBounds().getUnion (k.label.getBounds()).expanded (3); };
            return {{ { ModTarget::LaneDepth, area (strip.depth) },
                      { ModTarget::LaneOffset, area (strip.offset) },
                      { ModTarget::LaneRate, area (strip.shownSync ? strip.rateSynced : strip.rateFree) } }};
        }

        bool hitTest (int x, int y) override
        {
            if (! context.isMapping())
                return false;

            for (const auto& [target, area] : targets())
                if (area.contains (x, y))
                    return true;

            return false;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (const auto& [target, area] : targets())
                if (area.contains (e.getPosition()) && context.pick)
                {
                    context.pick ({ target, strip.lane, 0, 0.5f });
                    return;
                }
        }

        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto all = targets();

            if (context.isMapping())
            {
                const auto colour = theme::lfoColour (context.activeLfo);
                const auto mouse = getMouseXYRelative();

                for (const auto& [target, area] : all)
                {
                    const auto r = area.toFloat();
                    const bool over = isMouseOver() && area.contains (mouse);
                    g.setColour (colour.withAlpha (over ? 0.3f : 0.12f));
                    g.fillRoundedRectangle (r, 4.0f);
                    g.setColour (colour);
                    g.drawRoundedRectangle (r, 4.0f, over ? 2.0f : 1.0f);
                }
                return;
            }

            for (int j = 0; j < kNumLfos; ++j)
            {
                const auto routing = strip.proc.getRouting (j);
                for (int r = 0; r < routing.numRoutes; ++r)
                {
                    const auto& route = routing.routes[(size_t) r];
                    if (route.lane != strip.lane)
                        continue;

                    for (const auto& [target, area] : all)
                        if (target == route.target)
                        {
                            const juce::Point<float> c ((float) area.getRight() - 4.0f - 9.0f * (float) j, (float) area.getY() + 4.0f);
                            g.setColour (theme::lfoColour (j));
                            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
                        }
                }
            }
        }

        LaneStrip& strip;
        MapContext& context;
    };

    ModLanesProcessor& proc;
    const int lane;
    std::atomic<float>* modeParam;
    std::atomic<float>* syncParam;
    juce::RangedAudioParameter* pointsParameter = nullptr;
    int shownMode = -1;
    bool shownSync = true;
    int seenRoutingVersion = -1;

    juce::Label title;
    juce::ToggleButton onButton { "On" }, bipolarButton { "Bipolar" }, invertButton { "Invert" }, syncButton { "Sync" };
    juce::TextButton blocksButton { "Blocks" }, pointsButton { "Points" };
    juce::ComboBox mode, source;
    juce::Slider steps, channel, cc;
    Knob rateSynced, rateFree, depth, offset, smooth, blend, phase, fit, threshold, velocity, attack, release;
    std::atomic<float>* fitParam = nullptr;
    bool shownFit = false;
    Meter meter;
    MidiDragHandle dragHandle;
    juce::Rectangle<int> emptyKnobArea;
    bool liveView = true;
    bool gridWasActive = false;
    bool shownOn = true;
    MapOverlay overlay;
    LiveModulation liveMod { proc };

    // Declared last so they are destroyed before the controls they attach to.
    struct Attachments
    {
        void add (APVTS::SliderAttachment* a)   { sliders.add (a); }
        void add (APVTS::ButtonAttachment* a)   { buttons.add (a); }
        void add (APVTS::ComboBoxAttachment* a) { combos.add (a); }

        juce::OwnedArray<APVTS::SliderAttachment> sliders;
        juce::OwnedArray<APVTS::ButtonAttachment> buttons;
        juce::OwnedArray<APVTS::ComboBoxAttachment> combos;
    };
    Attachments attachments;
};

} // namespace modlanes
