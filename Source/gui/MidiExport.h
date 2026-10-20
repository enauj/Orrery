#pragma once

#include "Params.h"
#include "PluginProcessor.h"
#include "Theme.h"

namespace modlanes
{

// Renders one loop of a lane (blocks or points, with depth/offset/bipolar/invert
// and Blend) into a MIDI file of CC events, for dragging into the arrangement.
// LFO modulation is not included, since it does not repeat with the loop.
inline juce::File exportLaneToMidi (ModLanesProcessor& proc, int lane)
{
    const auto value = [&] (const char* name) { return proc.apvts.getRawParameterValue (params::lane (lane, name))->load(); };

    const int steps = juce::jlimit (1, kMaxSteps, (int) value ("steps"));
    const bool synced = value ("sync") >= 0.5f;
    const double stepBeats = params::stepBeats ((int) value ("rate"), (int) value ("fit"), steps, proc.barLength.load());
    const double phaseSteps = value ("phase") * steps;
    const double bpm = proc.hostBpm.load();
    const double hz = juce::jmax (0.01f, value ("hz"));
    const double loopBeats = synced ? steps * stepBeats : steps / hz * bpm / 60.0;

    const bool points = value ("points") >= 0.5f;
    const bool bipolar = value ("bipolar") >= 0.5f, invert = value ("invert") >= 0.5f;
    const float depth = value ("depth"), offset = value ("offset"), blend = value ("blend");
    const int channel = (int) value ("ch"), controller = (int) value ("cc");
    const auto pattern = proc.getPattern (lane);
    const auto noModulation = [] (const Block& b) { return unmodulated (b); };

    constexpr int ticksPerQuarter = 960;
    constexpr int ticksPerSample = 10; // ~1/96 beat resolution
    const int totalTicks = juce::jmax (ticksPerQuarter / 4, juce::roundToInt (loopBeats * ticksPerQuarter));

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::textMetaEvent (3, "Orrery Orbit " + juce::String (lane + 1)), 0.0);
    sequence.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / bpm)), 0.0);

    int last = -1;
    for (int tick = 0; tick < totalTicks; tick += ticksPerSample)
    {
        const double beat = (double) tick / ticksPerQuarter;
        const double pos = std::fmod ((synced ? beat / stepBeats : beat * 60.0 / bpm * hz) + phaseSteps, (double) steps);

        float v = points ? evalPoints (pattern.envelope, steps, pos) : laneValueAt (pattern, steps, pos, blend, noModulation);
        if (invert)
            v = -v;

        const float out = juce::jlimit (0.0f, 1.0f, bipolar ? 0.5f + 0.5f * (depth * v + offset) : depth * v + offset);
        const int cc = juce::roundToInt (out * 127.0f);
        if (cc != last)
        {
            sequence.addEvent (juce::MidiMessage::controllerEvent (channel, controller, cc), (double) tick);
            last = cc;
        }
    }

    sequence.addEvent (juce::MidiMessage::endOfTrack(), (double) totalTicks);
    sequence.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (sequence);

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Orrery");
    dir.createDirectory();
    auto file = dir.getChildFile ("Orrery Orbit " + juce::String (lane + 1) + " CC" + juce::String (controller) + ".mid");
    file.deleteFile();

    if (juce::FileOutputStream stream (file); stream.openedOk())
        midi.writeTo (stream);

    return file;
}

// Drag handle that exports the lane as a MIDI file when dragged out of the plugin.
class MidiDragHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragHandle (ModLanesProcessor& p, int laneIndex) : proc (p), lane (laneIndex)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag into the arrangement to export one loop of this orbit as MIDI CC");
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto colour = theme::laneColour (lane);
        g.setColour (isMouseOver() ? colour.withAlpha (0.25f) : theme::panelLight);
        g.fillRoundedRectangle (r, 4.0f);

        // A tiny clip with a curve in it
        auto clip = r.reduced (6.0f, 5.0f);
        juce::Path curve;
        curve.startNewSubPath (clip.getX(), clip.getBottom());
        curve.quadraticTo (clip.getX() + clip.getWidth() * 0.3f, clip.getY() - 2.0f, clip.getCentreX(), clip.getCentreY());
        curve.quadraticTo (clip.getX() + clip.getWidth() * 0.75f, clip.getBottom() + 2.0f, clip.getRight(), clip.getY());
        g.setColour (isMouseOver() ? juce::Colours::white : colour);
        g.strokePath (curve, juce::PathStrokeType (1.5f));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4)
            return;

        dragging = true;
        const auto file = exportLaneToMidi (proc, lane);
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

private:
    ModLanesProcessor& proc;
    const int lane;
    bool dragging = false;
};

} // namespace modlanes
