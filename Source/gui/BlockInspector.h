#pragma once

#include "LiveState.h"
#include "MapContext.h"
#include "Params.h"
#include "PluginProcessor.h"
#include "Theme.h"

namespace modlanes
{

// Edits the selected block: module type, the common shape controls
// (amplitude, cycles, fades) and the module's own parameters.
class BlockInspector : public juce::Component
{
public:
    BlockInspector()
    {
        for (int t = 0; t < kNumBlockTypes; ++t)
            typeBox.addItem (getTypeInfo ((BlockType) t).name, t + 1);

        typeBox.onChange = [this]
        {
            const auto type = (BlockType) (typeBox.getSelectedId() - 1);
            if (index < 0 || type == block.type)
                return;

            block = block.withType (type);
            refreshControls();
            pushEdit();
        };
        addAndMakeVisible (typeBox);

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto& knob = knobs[(size_t) i];
            knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
            knob.setColour (juce::Slider::textBoxTextColourId, theme::textDim);
            knob.onValueChange = [this, i] { setSlotValue (i, (float) knobs[(size_t) i].getValue()); pushEdit(); };
            addAndMakeVisible (knob);
            addAndMakeVisible (labels[(size_t) i]);
        }

        formulaEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
        formulaEditor.setColour (juce::TextEditor::backgroundColourId, theme::panel.darker (0.3f));
        formulaEditor.setColour (juce::TextEditor::textColourId, theme::text);
        formulaEditor.setTextToShowWhenEmpty ("e.g. sin(2*pi*x)*exp(-4*t)", theme::textDim);
        formulaEditor.setTooltip ("t = 0..1 over the block, x = phase in the cycle, c = cycle number, a / b = knobs.\n"
                                  "+ - * / % ^, sin cos tan abs sqrt exp log floor fract sign tri saw sqr noise, min max pow.\n"
                                  "The result is clipped to -1..1 by the Clip knob.");
        formulaEditor.onTextChange = [this] { applyFormula(); };
        addChildComponent (formulaEditor);

        examplesButton.setTooltip ("Insert an example formula");
        examplesButton.onClick = [this]
        {
            juce::PopupMenu menu;
            int id = 1;
            for (const auto& example : formula::examples)
                menu.addItem (id++, juce::String (example.name) + "     " + example.text);

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&examplesButton),
                                [safe = juce::Component::SafePointer<BlockInspector> (this)] (int result)
                                {
                                    if (safe != nullptr && result > 0)
                                        safe->formulaEditor.setText (formula::examples[result - 1].text, true);
                                });
        };
        addChildComponent (examplesButton);

        clear();
    }

    void showBlock (int laneIndex, int blockIndex, const Block& b)
    {
        lane = laneIndex;
        index = blockIndex;
        block = b;
        refreshControls();
        mapOverlay.repaint();
    }

    void clear()
    {
        lane = index = -1;
        refreshControls();
    }

    std::function<void (int lane, int index, const Block&)> onEdit;

    // The preview handles its own right-click (Steps menu).
    juce::Rectangle<int> getPreviewArea() const { return previewArea; }

    // LFO mapping onto the knobs. Called once by the editor.
    void attach (ModLanesProcessor& processor, MapContext& context)
    {
        proc = &processor;
        mapContext = &context;
        liveMod.emplace (processor);
        addAndMakeVisible (mapOverlay);
    }

    // Called by the editor timer and when map mode changes.
    void refreshModulation()
    {
        if (proc != nullptr && proc->getRoutingVersion() != seenRoutingVersion)
        {
            seenRoutingVersion = proc->getRoutingVersion();
            mapOverlay.repaint();
        }
    }

    void mapStateChanged() { mapOverlay.repaint(); }

    // Per display frame: shows on each modulated knob where the LFOs push it now.
    void animateModulation (bool live)
    {
        if (! liveMod)
            return;

        liveMod->update();
        Block modulated = block;
        float ampMod = 0.0f;
        int strongest = 0;
        const bool any = live && index >= 0 && liveMod->modulate (lane, modulated, ampMod, strongest);

        for (int slot = 0; slot < kNumSlots; ++slot)
        {
            float total = 0.0f;
            int lfo = 0;
            if (const auto route = mapOverlay.routeFor (slot); any && route.target != ModTarget::None && liveMod->sum (route, total, lfo))
            {
                const double value = slot == Amount ? block.amount + ampMod : modulatedSlotValue (modulated, slot);
                theme::setModulation (knobs[(size_t) slot], value, theme::lfoColour (lfo));
            }
            else
            {
                theme::setModulation (knobs[(size_t) slot], std::nullopt);
            }
        }
    }

    int getLane() const { return lane; }
    int getBlockId() const { return index >= 0 ? block.id : 0; }

    void paint (juce::Graphics& g) override
    {
        theme::paintPanel (g, getLocalBounds().toFloat());

        auto header = getLocalBounds().reduced (12, 8).removeFromTop (22);
        g.setFont (theme::font (11.0f, true));
        g.setColour (theme::textDim);
        g.drawText ("MODULE", header, juce::Justification::centredLeft);

        if (index < 0)
        {
            g.setFont (theme::font (12.0f));
            g.drawFittedText ("Select a block in an orbit to edit it.\n\n"
                              "On the grid: drag the body to move, the edges to resize.\n"
                              "Centre dot = amplitude, top triangles = fade in / out,\n"
                              "bottom pill = cycles (Shift for fine). Double-click a handle to reset.\n"
                              "Double-click an empty step to add, Delete to remove, Ctrl+D to duplicate.",
                              getLocalBounds().reduced (16, 40), juce::Justification::topLeft, 8);
            theme::paintHatch (g, *this, getLocalBounds().reduced (12, 10).withTrimmedTop (138).toFloat());
            return;
        }

        g.setColour (theme::laneColour (lane));
        g.drawText ("Orbit " + juce::String (lane + 1) + "  |  steps " + juce::String (block.start + 1)
                        + "-" + juce::String (block.start + block.length),
                    header.withTrimmedLeft (60), juce::Justification::centredLeft);

        // Preview of the block's shape
        const auto colour = theme::laneColour (juce::jmax (0, lane));
        const auto area = previewArea.toFloat();
        g.setColour (theme::panel.darker (0.3f));
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (theme::gridLine);
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        const auto curveArea = area.reduced (4.0f, 6.0f);

        if (block.type == BlockType::Steps)
        {
            // Bar editor: one bar per step, drawn from the bottom (or centre when bipolar).
            const int count = stepCount();
            const bool bipolar = block.p[1] >= 0.5f;
            const float w = curveArea.getWidth() / (float) count;
            const float base = bipolar ? curveArea.getCentreY() : curveArea.getBottom();

            for (int i = 0; i < count; ++i)
            {
                const float v = block.stepValues[(size_t) i];
                const float y = bipolar ? curveArea.getCentreY() - (v * 2.0f - 1.0f) * curveArea.getHeight() * 0.5f
                                        : curveArea.getBottom() - v * curveArea.getHeight();
                const auto bar = juce::Rectangle<float>::leftTopRightBottom (curveArea.getX() + (float) i * w + 1.5f, juce::jmin (y, base),
                                                                            curveArea.getX() + (float) (i + 1) * w - 1.5f, juce::jmax (y, base));
                g.setColour (colour.withAlpha (i == hoveredStepBar ? 0.55f : 0.32f));
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (colour);
                g.fillRect (bar.getX(), y - 1.0f, bar.getWidth(), 2.0f);
            }

            g.setColour (theme::textDim);
            g.setFont (theme::font (9.5f));
            g.drawText ("draw values  -  right-click for more", area.reduced (6.0f, 3.0f), juce::Justification::topRight);
        }

        const auto curve = theme::makeBlockCurve (block, curveArea, juce::jmax ((int) area.getWidth(), (int) (block.cycles * 32.0f)));
        auto fill = curve;
        fill.lineTo (curveArea.getRight(), curveArea.getCentreY());
        fill.lineTo (curveArea.getX(), curveArea.getCentreY());
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (1.8f));

        // Texture on the knob slots this module does not use.
        const int used = FirstParam + getTypeInfo (block.type).numParams;
        if (used < kNumSlots)
            theme::paintHatch (g, *this, knobRow.withTrimmedLeft (used * slotWidth).toFloat().reduced (4.0f, 2.0f));
    }

    //==============================================================================
    // Steps module: draw values with the mouse in the preview.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! editingSteps (e))
            return;

        if (e.mods.isPopupMenu())
            showStepsMenu();
        else
            drawStep (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (editingSteps (e) && ! e.mods.isPopupMenu())
            drawStep (e);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int bar = editingSteps (e) ? stepIndexAt (e.position.x) : -1;
        if (bar != hoveredStepBar)
        {
            hoveredStepBar = bar;
            repaint (previewArea);
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoveredStepBar = -1;
        repaint (previewArea);
    }

    void resized() override
    {
        mapOverlay.setBounds (getLocalBounds());
        auto r = getLocalBounds().reduced (12, 8);
        r.removeFromTop (26);

        auto top = r.removeFromTop (60);
        auto left = top.removeFromLeft (150);
        typeBox.setBounds (left.removeFromTop (24));
        top.removeFromLeft (10);

        // The Formula module gets an edit line under its preview.
        if (block.type == BlockType::Formula && index >= 0)
        {
            auto line = top.removeFromBottom (24);
            top.removeFromBottom (4);
            examplesButton.setBounds (line.removeFromRight (78));
            line.removeFromRight (4);
            formulaEditor.setBounds (line);
        }
        previewArea = top;

        r.removeFromTop (8);
        knobRow = r;
        slotWidth = r.getWidth() / kNumSlots;
        const int w = slotWidth;
        for (int i = 0; i < kNumSlots; ++i)
        {
            auto column = r.removeFromLeft (w);
            labels[(size_t) i].setBounds (column.removeFromTop (14));
            knobs[(size_t) i].setBounds (column);
        }
    }

private:
    // The first slots are common to every module, the rest are the module's parameters.
    enum Slot { Amount = 0, Cycles, FadeIn, FadeOut, Note, FirstParam };
    static constexpr int kNumSlots = FirstParam + kNumBlockParams;

    float slotValue (int slot) const { return modulatedSlotValue (block, slot); }

    static float modulatedSlotValue (const Block& block, int slot)
    {
        switch (slot)
        {
            case Amount:  return block.amount;
            case Cycles:  return block.cycles;
            case FadeIn:  return block.fadeIn;
            case FadeOut: return block.fadeOut;
            case Note:    return (float) block.note;
            default:      return block.p[(size_t) (slot - FirstParam)];
        }
    }

    void setSlotValue (int slot, float v)
    {
        switch (slot)
        {
            case Amount:  block.amount = v; break;
            case Cycles:  block.cycles = v; break;
            case FadeIn:  block.fadeIn = v; break;
            case FadeOut: block.fadeOut = v; break;
            case Note:    block.note = juce::roundToInt (v); break;
            default:      block.p[(size_t) (slot - FirstParam)] = v; break;
        }
    }

    void refreshControls()
    {
        const bool visible = index >= 0;
        typeBox.setVisible (visible);

        const bool isFormula = visible && block.type == BlockType::Formula;
        formulaEditor.setVisible (isFormula);
        examplesButton.setVisible (isFormula);
        if (isFormula && formulaEditor.getText() != juce::String (block.formulaText.data()))
            formulaEditor.setText (block.formulaText.data(), false);
        resized();

        const auto& info = getTypeInfo (block.type);
        const auto colour = theme::laneColour (juce::jmax (0, lane));

        if (visible)
            typeBox.setSelectedId ((int) block.type + 1, juce::dontSendNotification);

        for (int i = 0; i < kNumSlots; ++i)
        {
            auto& knob = knobs[(size_t) i];
            auto& label = labels[(size_t) i];
            const bool used = visible && (i < FirstParam || i - FirstParam < info.numParams);
            knob.setVisible (used);
            label.setVisible (used);

            if (! used)
                continue;

            knob.setColour (juce::Slider::rotarySliderFillColourId, colour);

            if (i == Amount)
            {
                configure (knob, label, "Amplitude", -1.0, 1.0, 0.0, 1.0);
                knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
            }
            else if (i == Cycles)
            {
                const bool integer = info.integerCycles;
                const char* name = cyclesName (block.type);
                configure (knob, label, name, minCycles (block.type), kMaxCycles, integer ? 1.0 : 0.25, info.defaultCycles);
                knob.setSkewFactorFromMidPoint (4.0);
                knob.textFromValueFunction = [integer] (double v) { return juce::String (v, integer ? 0 : 2); };
            }
            else if (i == FadeIn || i == FadeOut)
            {
                configure (knob, label, i == FadeIn ? "Fade In" : "Fade Out", 0.0, 1.0, 0.0, 0.0);
                knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
            }
            else if (i == Note)
            {
                configure (knob, label, "Trig Note", -1.0, 127.0, 1.0, -1.0);
                knob.textFromValueFunction = [] (double v) { return params::noteName (juce::roundToInt (v)); };
                knob.setTooltip ("MIDI Trigger mode: the note that plays this block (Any = round-robin for unmapped notes)");
            }
            else
            {
                const auto& param = info.params[(size_t) (i - FirstParam)];
                configure (knob, label, param.name, param.min, param.max, param.stepped ? 1.0 : 0.0, param.def);

                const juce::String name (param.name);
                if (name == "Table")
                    knob.textFromValueFunction = [] (double v) { return juce::String (Wavetables::name ((int) v)); };
                else if (name == "Direction")
                    knob.textFromValueFunction = [] (double v) { const char* n[] = { "Up", "Down", "Up-Down" }; return juce::String (n[juce::jlimit (0, 2, (int) v)]); };
                else if (name == "Clip")
                    knob.textFromValueFunction = [] (double v) { const char* n[] = { "Hard", "Soft", "Fold" }; return juce::String (n[juce::jlimit (0, 2, (int) v)]); };
                else if (name == "Unipolar")
                    knob.textFromValueFunction = [] (double v) { return v >= 0.5 ? juce::String ("Unipolar") : juce::String ("Bipolar"); };
                else
                    knob.textFromValueFunction = [stepped = param.stepped] (double v) { return juce::String (v, stepped ? 0 : 2); };
            }

            knob.setValue (slotValue (i), juce::dontSendNotification);
            knob.updateText();
        }

        repaint();
    }

    static void configure (juce::Slider& knob, juce::Label& label, const char* name,
                           double min, double max, double interval, double def)
    {
        theme::setupCaption (label, name);
        knob.setRange (min, max, interval);
        knob.setSkewFactor (1.0);
        knob.setDoubleClickReturnValue (true, def);
    }

    int stepCount() const { return juce::jlimit (1, kMaxSteps, juce::roundToInt (block.cycles)); }

    bool editingSteps (const juce::MouseEvent& e) const
    {
        return index >= 0 && block.type == BlockType::Steps && previewArea.contains (e.getPosition());
    }

    int stepIndexAt (float x) const
    {
        const auto area = previewArea.toFloat().reduced (4.0f, 6.0f);
        return juce::jlimit (0, stepCount() - 1, (int) ((x - area.getX()) / area.getWidth() * (float) stepCount()));
    }

    void drawStep (const juce::MouseEvent& e)
    {
        const auto area = previewArea.toFloat().reduced (4.0f, 6.0f);
        const float v = juce::jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight());
        block.stepValues[(size_t) stepIndexAt (e.position.x)] = v;
        pushEdit();
    }

    void showStepsMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Randomize");
        menu.addItem (2, "Ramp up");
        menu.addItem (3, "Ramp down");
        menu.addItem (4, "Invert");
        menu.addItem (5, "All to 50%");

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safe = juce::Component::SafePointer<BlockInspector> (this)] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;

                                auto& values = safe->block.stepValues;
                                const int count = safe->stepCount();
                                juce::Random random;
                                for (int i = 0; i < count; ++i)
                                {
                                    const float position = count > 1 ? (float) i / (float) (count - 1) : 0.0f;
                                    auto& v = values[(size_t) i];
                                    if (result == 1)      v = random.nextFloat();
                                    else if (result == 2) v = position;
                                    else if (result == 3) v = 1.0f - position;
                                    else if (result == 4) v = 1.0f - v;
                                    else if (result == 5) v = 0.5f;
                                }
                                safe->pushEdit();
                            });
    }

    static const char* cyclesName (BlockType t) { return cyclesLabel (t); }

    // Recompiles the formula as it is typed; invalid text keeps the last good
    // version playing and marks the field red.
    void applyFormula()
    {
        if (index < 0 || block.type != BlockType::Formula)
            return;

        auto candidate = block;
        const char* error = nullptr;
        const bool ok = candidate.setFormula (formulaEditor.getText().substring (0, formula::kMaxText - 1).toRawUTF8(), &error);

        formulaEditor.setColour (juce::TextEditor::outlineColourId, ok ? theme::gridStrong : juce::Colour (0xffe57373));
        formulaEditor.setColour (juce::TextEditor::focusedOutlineColourId, ok ? theme::laneColour (juce::jmax (0, lane)) : juce::Colour (0xffe57373));
        formulaEditor.setTooltip (ok ? juce::String ("Formula OK") : "Error: " + juce::String (error));
        formulaEditor.repaint();

        if (ok)
        {
            block = candidate;
            pushEdit();
        }
    }

    void pushEdit()
    {
        if (index >= 0 && onEdit)
            onEdit (lane, index, block);
        repaint();
    }

    int lane = -1, index = -1;
    Block block;
    juce::Rectangle<int> previewArea, knobRow;
    int slotWidth = 0;
    int hoveredStepBar = -1;

    // Map targets for the knobs (map mode) and LFO dots on modulated knobs.
    struct MapOverlay : public juce::Component
    {
        explicit MapOverlay (BlockInspector& o) : owner (o) { setInterceptsMouseClicks (true, false); }

        // Route for a knob slot, or None if that slot cannot be modulated.
        ModRoute routeFor (int slot) const
        {
            ModRoute route { ModTarget::None, owner.lane, owner.block.id, 0.5f };
            const int numParams = getTypeInfo (owner.block.type).numParams;
            if (slot == Amount)                    route.target = ModTarget::BlockAmp;
            else if (slot == Cycles)               { route.target = ModTarget::BlockParam; route.param = kModCycles; }
            else if (slot == FadeIn)               { route.target = ModTarget::BlockParam; route.param = kModFadeIn; }
            else if (slot == FadeOut)              { route.target = ModTarget::BlockParam; route.param = kModFadeOut; }
            else if (slot >= FirstParam && slot - FirstParam < numParams) { route.target = ModTarget::BlockParam; route.param = slot - FirstParam; }
            return route;
        }

        juce::Rectangle<int> slotArea (int slot) const
        {
            return owner.knobs[(size_t) slot].getBounds().getUnion (owner.labels[(size_t) slot].getBounds()).expanded (2);
        }

        bool mapping() const { return owner.mapContext != nullptr && owner.mapContext->isMapping() && owner.index >= 0; }

        bool hitTest (int x, int y) override
        {
            if (! mapping())
                return false;
            for (int slot = 0; slot < kNumSlots; ++slot)
                if (routeFor (slot).target != ModTarget::None && slotArea (slot).contains (x, y))
                    return true;
            return false;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int slot = 0; slot < kNumSlots; ++slot)
                if (const auto route = routeFor (slot); route.target != ModTarget::None && slotArea (slot).contains (e.getPosition()))
                {
                    if (owner.mapContext->pick)
                        owner.mapContext->pick (route);
                    return;
                }
        }

        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        void paint (juce::Graphics& g) override
        {
            if (owner.index < 0 || owner.proc == nullptr)
                return;

            if (mapping())
            {
                const auto colour = theme::lfoColour (owner.mapContext->activeLfo);
                const auto mouse = getMouseXYRelative();
                for (int slot = 0; slot < kNumSlots; ++slot)
                    if (routeFor (slot).target != ModTarget::None)
                    {
                        const auto area = slotArea (slot);
                        const bool over = isMouseOver() && area.contains (mouse);
                        g.setColour (colour.withAlpha (over ? 0.3f : 0.12f));
                        g.fillRoundedRectangle (area.toFloat(), 4.0f);
                        g.setColour (colour);
                        g.drawRoundedRectangle (area.toFloat(), 4.0f, over ? 2.0f : 1.0f);
                    }
                return;
            }

            // Dots on knobs that LFOs modulate.
            for (int j = 0; j < kNumLfos; ++j)
            {
                const auto routing = owner.proc->getRouting (j);
                for (int r = 0; r < routing.numRoutes; ++r)
                    for (int slot = 0; slot < kNumSlots; ++slot)
                        if (const auto target = routeFor (slot); target.target != ModTarget::None && target.sameTarget (routing.routes[(size_t) r]))
                        {
                            const auto area = slotArea (slot);
                            g.setColour (theme::lfoColour (j));
                            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) area.getRight() - 5.0f - 9.0f * (float) j,
                                                                                               (float) area.getY() + 5.0f }));
                        }
            }
        }

        BlockInspector& owner;
    };

    ModLanesProcessor* proc = nullptr;
    MapContext* mapContext = nullptr;
    int seenRoutingVersion = -1;
    std::optional<LiveModulation> liveMod;
    MapOverlay mapOverlay { *this };

    juce::ComboBox typeBox;
    juce::TextEditor formulaEditor;
    juce::TextButton examplesButton { "Examples" };
    std::array<juce::Slider, kNumSlots> knobs;
    std::array<juce::Label, kNumSlots> labels;
};

} // namespace modlanes
