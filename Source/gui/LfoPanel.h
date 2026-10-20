#pragma once

#include "MapContext.h"
#include "Params.h"
#include "PluginProcessor.h"
#include "Theme.h"

namespace modlanes
{

// A global LFO. Click Map, then click a lane's Depth / Offset / Rate or a
// block to add a modulation route; each route has its own amount.
class LfoPanel : public juce::Component
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    LfoPanel (ModLanesProcessor& p, int lfoIndex, MapContext& context)
        : proc (p), index (lfoIndex), mapContext (context),
          colour (theme::lfoColour (lfoIndex)),
          syncParam (p.apvts.getRawParameterValue (params::lfo (lfoIndex, "sync"))),
          shapeParam (p.apvts.getRawParameterValue (params::lfo (lfoIndex, "shape"))),
          onParam (p.apvts.getRawParameterValue (params::lfo (lfoIndex, "on"))),
          skewParam (p.apvts.getRawParameterValue (params::lfo (lfoIndex, "skew"))),
          scope (*this)
    {
        auto& apvts = p.apvts;
        const auto id = [this] (const char* name) { return params::lfo (index, name); };

        shape.addItemList (params::lfoShapeNames, 1);
        addAndMakeVisible (shape);

        sync.setColour (juce::ToggleButton::tickColourId, colour);
        sync.setTooltip ("Sync: Rate steps through note values. Off: free rate in Hz.");
        addAndMakeVisible (sync);

        onButton.setColour (juce::ToggleButton::tickColourId, colour);
        onButton.setTooltip ("Switch this LFO off: its routes stop modulating and it costs nothing");
        addAndMakeVisible (onButton);
        addAndMakeVisible (scope);

        // Synced and free rate share one knob position; only one is visible.
        for (auto* knob : { &div, &hz })
        {
            theme::setupKnob (*knob, colour);
            addAndMakeVisible (*knob);
        }
        theme::setupCaption (rateLabel, "Rate");
        addAndMakeVisible (rateLabel);

        const std::pair<juce::Slider*, juce::Label*> extras[] = { { &phaseKnob, &phaseLabel }, { &skewKnob, &skewLabel }, { &smoothKnob, &smoothLabel } };
        const char* extraNames[] = { "Phase", "Skew", "Smooth" };
        for (int k = 0; k < 3; ++k)
        {
            theme::setupKnob (*extras[k].first, colour);
            theme::setupCaption (*extras[k].second, extraNames[k]);
            addAndMakeVisible (*extras[k].first);
            addAndMakeVisible (*extras[k].second);
        }
        phaseKnob.setTooltip ("Start point of the cycle");
        skewKnob.setTooltip ("Bends the cycle: pulse width for Square, tilt for Triangle, asymmetry for Sine and the others");
        smoothKnob.setTooltip ("Slew: rounds off jumps (Square, S&H, Stairs), up to about 250 ms");

        uniButton.setColour (juce::ToggleButton::tickColourId, colour);
        uniButton.setTooltip ("Unipolar: 0..1 instead of -1..1, so the LFO only pushes its targets one way");
        addAndMakeVisible (uniButton);

        mapButton.setColour (juce::TextButton::buttonOnColourId, colour.withAlpha (0.45f));
        mapButton.setTooltip ("Click, then click an orbit's Depth, Offset or Rate, a block on a grid (then pick a parameter), "
                              "or a knob of the selected module in the inspector. Esc cancels.");
        mapButton.onClick = [this] { if (onMapClicked) onMapClicked (index); };
        addAndMakeVisible (mapButton);

        for (int k = 0; k < kMaxRoutes; ++k)
        {
            auto& row = rows[(size_t) k];
            row.name.setFont (theme::font (11.0f));
            row.name.setColour (juce::Label::textColourId, theme::text);

            theme::setupBar (row.amount, colour);
            row.amount.setRange (-1.0, 1.0, 0.0);
            row.amount.setDoubleClickReturnValue (true, 0.5);
            row.amount.textFromValueFunction = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%"; };
            row.amount.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-0123456789.").getDoubleValue() / 100.0; };
            row.amount.onValueChange = [this, k]
            {
                routing.routes[(size_t) k].amount = (float) rows[(size_t) k].amount.getValue();
                commit();
            };

            row.remove.setTooltip ("Remove this route");
            row.remove.onClick = [this, k]
            {
                routing.remove (k);
                commit();
                reloadRoutes();
            };

            addChildComponent (row.name);
            addChildComponent (row.amount);
            addChildComponent (row.remove);
        }

        shapeAttachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, id ("shape"), shape);
        divAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, id ("div"), div);
        syncAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, id ("sync"), sync);
        onAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, id ("on"), onButton);
        uniAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, id ("uni"), uniButton);
        phaseAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, id ("phase"), phaseKnob);
        skewAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, id ("skew"), skewKnob);
        smoothAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, id ("smooth"), smoothKnob);
        phaseKnob.setDoubleClickReturnValue (true, 0.0);
        skewKnob.setDoubleClickReturnValue (true, 0.5);
        smoothKnob.setDoubleClickReturnValue (true, 0.0);
        for (auto* knob : { &phaseKnob, &smoothKnob })
            knob->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        skewKnob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        hzAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, id ("hz"), hz);

        hz.textFromValueFunction = [] (double v) { return juce::String (v, 2) + " Hz"; };
        hz.updateText();

        reloadRoutes();
        refresh (true);
    }

    std::function<void (int lfo)> onMapClicked;

    void refresh (bool live)
    {
        const bool liveChanged = live != liveView;
        liveView = live;

        const bool synced = syncParam->load() >= 0.5f;
        div.setVisible (synced);
        hz.setVisible (! synced);

        const bool mapping = mapContext.activeLfo == index;
        if (mapButton.getToggleState() != mapping)
        {
            mapButton.setToggleState (mapping, juce::dontSendNotification);
            mapButton.setButtonText (mapping ? "Click target" : "Map");
        }

        if (proc.getRoutingVersion() != seenRoutingVersion || proc.getPatternVersion() != seenPatternVersion)
            reloadRoutes();

        // Dim the controls of a switched-off LFO.
        const bool on = onParam->load() >= 0.5f;
        for (auto* c : std::initializer_list<juce::Component*> { &shape, &div, &hz, &rateLabel, &sync, &mapButton, &uniButton,
                                                                 &phaseKnob, &skewKnob, &smoothKnob, &phaseLabel, &skewLabel, &smoothLabel })
            c->setAlpha (on ? 1.0f : 0.4f);

        // The wave only repaints when the dot moves by a pixel or the shape changes.
        const int dotX = liveView && on ? juce::roundToInt (proc.lfoPhase[index].load() * (float) scope.getWidth()) : -1;
        const int key = ((dotX * 64 + (int) shapeParam->load()) * 128 + juce::roundToInt (skewParam->load() * 100.0f)) * 2 + (on ? 1 : 0);
        if (key != scopeKey || liveChanged)
        {
            scopeKey = key;
            scope.repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        theme::paintPanel (g, getLocalBounds().toFloat());

        g.setColour (colour);
        g.setFont (theme::font (11.0f, true));
        g.drawText ("LFO " + juce::String (index + 1), getLocalBounds().reduced (12, 8).removeFromTop (22),
                    juce::Justification::centredLeft);

        if (routing.numRoutes == 0)
        {
            g.setColour (theme::textDim);
            g.setFont (theme::font (11.0f));
            g.drawFittedText ("No targets yet. Click Map, then click\nan orbit's Depth, Offset or Rate, or a block.",
                              routesArea, juce::Justification::topLeft, 3);
        }

        // Texture below the last route (or the hint).
        theme::paintHatch (g, *this, routesArea.withTrimmedTop (routing.numRoutes > 0 ? routing.numRoutes * 23 : 38).toFloat());
    }

    // One cycle of the shape as a static picture, with a dot at the current phase.
    void paintWave (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.fillAll (theme::panel);
        g.setColour (theme::panel.darker (0.3f));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (theme::gridLine);
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        const auto wave = area.reduced (6.0f, 6.0f);
        const int shapeIndex = (int) shapeParam->load();
        const float skew = skewParam->load();
        const bool random = shapeIndex == 5 || shapeIndex == 6;
        const auto valueAt = [&] (float phase)
        {
            // Random shapes have no fixed cycle; show three representative ones.
            if (random)
            {
                const float x = phase * 3.0f;
                return evalGlobalLfo (shapeIndex, x - std::floor (x), (std::uint32_t) x + 5U, skew);
            }
            return evalGlobalLfo (shapeIndex, phase, 0, skew);
        };
        const auto toPoint = [&] (float phase, float v) { return juce::Point<float> (wave.getX() + phase * wave.getWidth(),
                                                                                     wave.getCentreY() - v * wave.getHeight() * 0.5f); };

        juce::Path path;
        const int n = (int) wave.getWidth();
        for (int i = 0; i <= n; ++i)
        {
            const float phase = std::min (0.9999f, (float) i / (float) n);
            const auto pt = toPoint (phase, valueAt (phase));
            if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        g.setColour (colour.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.5f));

        if (! liveView || onParam->load() < 0.5f)
            return;

        const float phase = proc.lfoPhase[index].load();
        const float uni = proc.apvts.getRawParameterValue (params::lfo (index, "uni"))->load() >= 0.5f;
        const float output = proc.lfoOutput[index].load();
        const auto dot = toPoint (phase, random ? (uni ? output * 2.0f - 1.0f : output) : valueAt (phase));
        g.setColour (colour.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (22);
        top.removeFromLeft (50);
        onButton.setBounds (top.removeFromLeft (40));
        mapButton.setBounds (top.removeFromRight (84));
        top.removeFromRight (6);
        sync.setBounds (top.removeFromRight (52));

        r.removeFromTop (6);
        scopeArea = r.removeFromTop (44);
        scope.setBounds (scopeArea);

        r.removeFromTop (6);
        auto shapeRow = r.removeFromTop (22);
        uniButton.setBounds (shapeRow.removeFromRight (46));
        shapeRow.removeFromRight (6);
        shape.setBounds (shapeRow);

        r.removeFromTop (4);
        auto knobs = r.removeFromTop (54);
        const int w = knobs.getWidth() / 4;
        const auto place = [&knobs, w] (juce::Slider& s, juce::Label& l)
        {
            auto cell = knobs.removeFromLeft (w);
            l.setBounds (cell.removeFromBottom (14));
            s.setBounds (cell);
        };
        place (div, rateLabel);
        hz.setBounds (div.getBounds());
        place (phaseKnob, phaseLabel);
        place (skewKnob, skewLabel);
        place (smoothKnob, smoothLabel);

        r.removeFromTop (8);
        routesArea = r;
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (20);
            r.removeFromTop (3);
            row.remove.setBounds (line.removeFromRight (20));
            line.removeFromRight (4);
            row.amount.setBounds (line.removeFromRight (64));
            line.removeFromRight (4);
            row.name.setBounds (line);
        }
    }

private:
    void reloadRoutes()
    {
        seenRoutingVersion = proc.getRoutingVersion();
        seenPatternVersion = proc.getPatternVersion();
        routing = proc.getRouting (index);

        for (int k = 0; k < kMaxRoutes; ++k)
        {
            auto& row = rows[(size_t) k];
            const bool used = k < routing.numRoutes;
            row.name.setVisible (used);
            row.amount.setVisible (used);
            row.remove.setVisible (used);

            if (used)
            {
                const auto& route = routing.routes[(size_t) k];
                row.name.setText (describe (route), juce::dontSendNotification);
                row.name.setColour (juce::Label::textColourId, theme::laneColour (route.lane).brighter (0.2f));
                row.amount.setValue (route.amount, juce::dontSendNotification);
                row.amount.updateText();
            }
        }

        repaint();
    }

    juce::String describe (const ModRoute& route) const
    {
        const juce::String laneName = "O" + juce::String (route.lane + 1);

        switch (route.target)
        {
            case ModTarget::LaneDepth:  return laneName + " Depth";
            case ModTarget::LaneOffset: return laneName + " Offset";
            case ModTarget::LaneRate:   return laneName + " Rate";
            case ModTarget::BlockAmp:
            case ModTarget::BlockParam:
            {
                const auto pattern = proc.getPattern (route.lane);
                const int b = pattern.findById (route.blockId);
                if (b < 0)
                    return laneName + " (missing)";

                const auto& block = pattern.blocks[(size_t) b];
                return laneName + " " + getTypeInfo (block.type).shortName + " @" + juce::String (block.start + 1) + " "
                     + (route.target == ModTarget::BlockAmp ? juce::String ("Amp") : juce::String (modParamName (block.type, route.param)));
            }
            case ModTarget::None: break;
        }

        return {};
    }

    void commit()
    {
        seenRoutingVersion = proc.setRouting (index, routing);
    }

    ModLanesProcessor& proc;
    const int index;
    MapContext& mapContext;
    const juce::Colour colour;
    std::atomic<float>* syncParam;

    std::atomic<float>* shapeParam;

    std::atomic<float>* onParam;

    // The wave display as its own opaque component, so it repaints on its own.
    struct Scope : public juce::Component
    {
        explicit Scope (LfoPanel& p) : panel (p) { setOpaque (true); }
        void paint (juce::Graphics& g) override { panel.paintWave (g, getLocalBounds().toFloat()); }
        LfoPanel& panel;
    };

    std::atomic<float>* skewParam;

    juce::ComboBox shape;
    juce::ToggleButton onButton { "On" }, uniButton { "Uni" };
    juce::Slider phaseKnob, skewKnob, smoothKnob;
    juce::Label phaseLabel, skewLabel, smoothLabel;
    Scope scope;
    int scopeKey = -2;
    juce::ToggleButton sync { "Sync" };
    juce::Slider div, hz;
    juce::Label rateLabel;
    juce::TextButton mapButton { "Map" };

    struct RouteRow
    {
        juce::Label name;
        juce::Slider amount;
        juce::TextButton remove { "x" };
    };
    std::array<RouteRow, kMaxRoutes> rows;

    LfoRouting routing;
    bool liveView = true;
    int seenRoutingVersion = -1, seenPatternVersion = -1;

    juce::Rectangle<int> scopeArea, routesArea;

    std::unique_ptr<APVTS::ComboBoxAttachment> shapeAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> syncAttachment, onAttachment, uniAttachment;
    std::unique_ptr<APVTS::SliderAttachment> phaseAttachment, skewAttachment, smoothAttachment;
    std::unique_ptr<APVTS::SliderAttachment> divAttachment, hzAttachment;
};

} // namespace modlanes
