#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace modlanes::params
{

inline juce::String lane (int index, const char* name) { return "l" + juce::String (index + 1) + "_" + name; }
inline juce::String lfo (int index, const char* name)  { return "lfo" + juce::String (index + 1) + "_" + name; }

inline const juce::String freeRun { "freerun" };
inline const juce::String thru { "thru" };

// Global transport / pattern settings (not stored in scenes).
inline const juce::String clock { "clock" };          // Host or Internal
inline const juce::String bpm { "bpm" };              // internal tempo
inline const juce::String play { "play" };            // internal start/stop
inline const juce::String scene { "scene" };          // 1..kNumScenes
inline const juce::String quantize { "quantize" };    // when scene changes take effect
inline const juce::String masterLength { "masterlen" }; // bars, 0 = off

inline const juce::StringArray clockNames { "Host", "Internal" };
inline const juce::StringArray quantizeNames { "Instant", "Next Bar", "Alignment" };

inline bool isGlobal (const juce::String& id)
{
    return id == freeRun || id == thru || id == clock || id == bpm || id == play
        || id == scene || id == quantize || id == masterLength;
}

// Length of one lane step, ordered slow -> fast so the Rate knob turns faster to the right.
inline const juce::StringArray rateNames { "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
constexpr double kRateBeats[] = { 8.0, 4.0, 2.0, 1.0, 1.0 / 2.0, 1.0 / 3.0, 1.0 / 4.0, 1.0 / 6.0, 1.0 / 8.0, 1.0 / 16.0 };
constexpr int kDefaultRate = 6; // 1/16

// Length of one global LFO cycle when synced, also slow -> fast.
inline const juce::StringArray lfoDivNames { "8 Bars", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16" };
constexpr double kLfoDivBeats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
constexpr int kDefaultLfoDiv = 3; // 1 bar

// Appended over time; the order is stored in projects, so only add at the end.
inline const juce::StringArray lfoShapeNames { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H",
                                               "Smooth Random", "Exp Rise", "Exp Fall", "Stairs", "Bump" };

// Fit: squeeze an orbit's steps into exactly N bars (polyrhythms such as 5 against 4).
inline const juce::StringArray fitNames { "Off", "1/2 Bar", "1 Bar", "2 Bars", "3 Bars", "4 Bars", "8 Bars" };
constexpr double kFitBars[] = { 0.0, 0.5, 1.0, 2.0, 3.0, 4.0, 8.0 };

// Length of one step in quarter notes, from the Rate choice or, with Fit on, from the bar count.
inline double stepBeats (int rateIndex, int fitIndex, int steps, double barLength)
{
    const double fit = kFitBars[juce::jlimit (0, 6, fitIndex)];
    return fit > 0.0 ? fit * barLength / juce::jmax (1, steps) : kRateBeats[juce::jlimit (0, 9, rateIndex)];
}

// What drives a lane.
enum LaneMode { Sequence = 0, MidiTrigger, AudioTrigger, AudioFollow };
inline const juce::StringArray modeNames { "Sequence", "MIDI Trigger", "Audio Trigger", "Audio Follow" };
inline const juce::StringArray sourceNames { "Main In", "Sidechain" };

inline bool isAudioMode (int mode) { return mode == AudioTrigger || mode == AudioFollow; }

// Note names with middle C (60) = C3, as in Bitwig.
inline juce::String noteName (int note)
{
    return note < 0 ? juce::String ("Any") : juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

} // namespace modlanes::params
