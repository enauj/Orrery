#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <vector>

// Reads the CC data of a MIDI file (e.g. a clip dragged in from the DAW), so it
// can become a Curve block or an orbit's Points envelope.
namespace modlanes::midiimport
{

struct CcCurve
{
    std::vector<double> beats;  // event times in quarter notes, ascending
    std::vector<float> values;  // 0..1
    double lengthBeats = 0.0;
    int controller = -1;
};

inline bool isMidiFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".mid") || path.endsWithIgnoreCase (".midi");
}

// Picks the preferred controller if the file has it, otherwise the one with the most events.
inline bool read (const juce::File& file, int preferredController, CcCurve& out)
{
    juce::FileInputStream stream (file);
    juce::MidiFile midi;
    if (! stream.openedOk() || ! midi.readFrom (stream))
        return false;

    const int ticksPerQuarter = midi.getTimeFormat();
    if (ticksPerQuarter <= 0)
        return false; // SMPTE time is not supported

    std::map<int, int> counts;
    double lastTick = 0.0;
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* event : *midi.getTrack (t))
        {
            lastTick = juce::jmax (lastTick, event->message.getTimeStamp());
            if (event->message.isController())
                ++counts[event->message.getControllerNumber()];
        }

    if (counts.empty())
        return false;

    int controller = counts.begin()->first;
    for (const auto& [cc, count] : counts)
        if (count > counts[controller])
            controller = cc;
    if (counts.count (preferredController) > 0)
        controller = preferredController;

    std::vector<std::pair<double, float>> events;
    for (int t = 0; t < midi.getNumTracks(); ++t)
        for (const auto* event : *midi.getTrack (t))
            if (event->message.isControllerOfType (controller))
                events.push_back ({ event->message.getTimeStamp() / ticksPerQuarter,
                                    (float) event->message.getControllerValue() / 127.0f });

    std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    out = {};
    out.controller = controller;
    for (const auto& [beat, value] : events)
    {
        out.beats.push_back (beat);
        out.values.push_back (value);
    }
    out.lengthBeats = juce::jmax (1.0, std::ceil (juce::jmax (lastTick / ticksPerQuarter, events.back().first) - 1.0e-6));
    return true;
}

// The CC value at a time: each event holds until the next one.
inline float valueAt (const CcCurve& c, double beat)
{
    if (c.beats.empty())
        return 0.0f;

    const auto it = std::upper_bound (c.beats.begin(), c.beats.end(), beat);
    return it == c.beats.begin() ? c.values.front() : c.values[(size_t) (it - c.beats.begin() - 1)];
}

} // namespace modlanes::midiimport
