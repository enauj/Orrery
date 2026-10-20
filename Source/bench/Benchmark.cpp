// Performance benchmark for ModLanes (not part of the plugin).
//
// Audio: times processBlock for several lane setups at 48 kHz / 512 samples and
// reports the share of the real-time budget used.
// UI: times what one animation frame repaints (grids, meters, LFO scopes, phrase
// indicator, scene pads), including the parent components painted underneath,
// at 2x scale like a HiDPI screen, with the software renderer the plugin window uses.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Params.h"
#include "gui/MidiExport.h"
#include "gui/MidiImport.h"
#include "gui/OrreryView.h"
#include "gui/Presets.h"

#include <chrono>
#include <cstdio>

namespace
{
using Clock = std::chrono::high_resolution_clock;

double secondsSince (Clock::time_point start)
{
    return std::chrono::duration<double> (Clock::now() - start).count();
}

void setParam (ModLanesProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

// Runs the audio engine and returns the average time per block in microseconds.
double timeAudio (ModLanesProcessor& p, int blocks)
{
    constexpr int blockSize = 512;
    juce::AudioBuffer<float> buffer (4, blockSize); // main stereo + sidechain stereo
    juce::Random random (1);
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < blockSize; ++i)
            buffer.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 50; ++i) // warm up
    {
        midi.clear();
        p.processBlock (buffer, midi);
    }

    const auto start = Clock::now();
    for (int i = 0; i < blocks; ++i)
    {
        midi.clear();
        if (i % 32 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
        p.processBlock (buffer, midi);
    }
    return secondsSince (start) / blocks * 1.0e6;
}

void reportAudio (const char* name, double microseconds)
{
    const double budget = 512.0 / 48000.0 * 1.0e6;
    std::printf ("  %-44s %8.2f us/block   %6.3f %% of real time\n", name, microseconds, microseconds / budget * 100.0);
}

// Paints the editor clipped to `regions` (what a frame's repaint() calls cause)
// and returns milliseconds per frame.
double timeFrame (juce::Component& editor, const juce::RectangleList<int>& regions, int frames, float scale = 2.0f)
{
    juce::Image image (juce::Image::ARGB, juce::roundToInt (editor.getWidth() * scale), juce::roundToInt (editor.getHeight() * scale), true, juce::SoftwareImageType());

    const auto start = Clock::now();
    for (int f = 0; f < frames; ++f)
        for (const auto& region : regions)
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));
            g.reduceClipRegion (region);
            editor.paintEntireComponent (g, true);
        }
    return secondsSince (start) / frames * 1000.0;
}

template <typename T>
void collect (juce::Component& root, juce::Array<T*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (auto* match = dynamic_cast<T*> (child))
            out.add (match);
        collect (*child, out);
    }
}
} // namespace

int runLiveCheck (int blockSize, bool gpu);

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    // "ModLanesBench live [blockSize]": animation smoothness check in a real window.
    if (argc > 1 && juce::String (argv[1]) == "live")
        return runLiveCheck (argc > 2 ? juce::String (argv[2]).getIntValue() : 512, argc > 3 && juce::String (argv[3]) == "gpu");

    ModLanesProcessor proc;
    proc.setPlayConfigDetails (4, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    setParam (proc, modlanes::params::freeRun, 1.0f); // run without a host transport

    std::printf ("AUDIO (48 kHz, 512-sample blocks)\n");

    const auto factory = modlanes::presets::factory (proc);
    for (const auto& [name, scene] : factory)
    {
        proc.loadIntoCurrentScene (scene);
        setParam (proc, modlanes::params::freeRun, 1.0f);
        reportAudio (("preset: " + name).toRawUTF8(), timeAudio (proc, 4000));
    }

    // Heavy case: every lane full of blocks with Blend, LFOs routed to everything.
    proc.loadIntoCurrentScene (factory[2].second); // Slow Wobble
    for (int i = 0; i < modlanes::kNumLanes; ++i)
    {
        modlanes::LanePattern pattern;
        for (int s = 0; s < 32; ++s)
        {
            auto b = modlanes::Block::make ((modlanes::BlockType) (s % modlanes::kNumBlockTypes), s, 1);
            if (b.type == modlanes::BlockType::Formula)
                b.setFormula ("sin(2*pi*x)*exp(-4*t) + noise(t*8)*a");
            pattern.add (b);
        }
        proc.setPattern (i, pattern);
        setParam (proc, modlanes::params::lane (i, "steps"), 32.0f);
        setParam (proc, modlanes::params::lane (i, "blend"), 1.0f);
    }
    for (int j = 0; j < modlanes::kNumLfos; ++j)
    {
        modlanes::LfoRouting routing;
        routing.add ({ modlanes::ModTarget::LaneDepth, j, 0, 0.3f });
        routing.add ({ modlanes::ModTarget::LaneRate, j, 0, 0.2f });
        routing.add ({ modlanes::ModTarget::BlockAmp, j, proc.getPattern (j).blocks[3].id, 0.5f });
        proc.setRouting (j, routing);
    }
    reportAudio ("heavy: 3 x 32 blocks, Blend, 9 LFO routes", timeAudio (proc, 4000));

    for (int i = 0; i < modlanes::kNumLanes; ++i)
        setParam (proc, modlanes::params::lane (i, "points"), 1.0f);
    for (int i = 0; i < modlanes::kNumLanes; ++i)
    {
        auto pattern = proc.getPattern (i);
        pattern.envelope = modlanes::renderBlocksToPoints (pattern, 32, 1.0f);
        proc.setPattern (i, pattern);
    }
    reportAudio ("heavy, Points view (dense envelopes)", timeAudio (proc, 4000));

    for (int i = 0; i < modlanes::kNumLanes; ++i)
    {
        setParam (proc, modlanes::params::lane (i, "points"), 0.0f);
        setParam (proc, modlanes::params::lane (i, "mode"), (float) (i == 0 ? modlanes::params::AudioFollow
                                                                       : i == 1 ? modlanes::params::AudioTrigger
                                                                                : modlanes::params::MidiTrigger));
    }
    reportAudio ("lane modes: follow / audio trigger / MIDI trigger", timeAudio (proc, 4000));

    for (int i = 0; i < modlanes::kNumLanes; ++i)
        setParam (proc, modlanes::params::lane (i, "mode"), 0.0f);

    // Pattern edits while playing: every block copies the patterns to the audio thread.
    {
        constexpr int blocks = 2000;
        juce::AudioBuffer<float> buffer (4, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        const auto pattern = proc.getPattern (0);
        const auto start = Clock::now();
        for (int i = 0; i < blocks; ++i)
        {
            proc.setPattern (0, pattern);
            midi.clear();
            proc.processBlock (buffer, midi);
        }
        reportAudio ("heavy + a pattern edit before every block", secondsSince (start) / blocks * 1.0e6);
    }

    //==============================================================================
    std::printf ("\nUI (one animation frame, 2x scale; the editor timer runs at 30 frames/s)\n");

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    editor->setSize (1380, 880);

    juce::Array<modlanes::StepGrid*> grids;
    collect (*editor, grids);
    juce::Array<modlanes::LfoPanel*> lfoPanels;
    collect (*editor, lfoPanels);
    juce::Array<modlanes::PhraseIndicator*> phrases;
    collect (*editor, phrases);
    juce::Array<modlanes::SceneBar*> sceneBars;
    collect (*editor, sceneBars);

    const auto inEditor = [&editor] (juce::Component& c) { return editor->getLocalArea (&c, c.getLocalBounds()); };

    juce::RectangleList<int> gridRegions, meterRegions, lfoRegions, headerRegions;
    for (auto* grid : grids)
    {
        // The playhead columns a frame repaints (old + new position, about one step plus the dot).
        gridRegions.addWithoutMerging (inEditor (*grid).withTrimmedLeft (120).withWidth (64));
        auto* strip = grid->getParentComponent();
        // The meter is the narrow column right of the grid.
        meterRegions.addWithoutMerging (editor->getLocalArea (strip, juce::Rectangle<int> (grid->getRight() + 6, grid->getY() + 30, 36, grid->getHeight() - 30)));
    }
    for (auto* panel : lfoPanels)
        lfoRegions.addWithoutMerging (inEditor (*panel).withTrimmedTop (36).withHeight (80));
    for (auto* phrase : phrases)
        headerRegions.addWithoutMerging (inEditor (*phrase));
    for (auto* bar : sceneBars)
        headerRegions.addWithoutMerging (inEditor (*bar).withTrimmedLeft (430).withWidth (400)); // the scene pads

    constexpr int frames = 60;
    const double fullFrame = timeFrame (*editor, juce::RectangleList<int> (editor->getLocalBounds()), 10);
    const double gridsMs = timeFrame (*editor, gridRegions, frames);
    const double metersMs = timeFrame (*editor, meterRegions, frames);
    const double lfoMs = timeFrame (*editor, lfoRegions, frames);
    const double headerMs = timeFrame (*editor, headerRegions, frames);
    const double liveFrame = gridsMs + metersMs + lfoMs + headerMs;

    std::printf ("  %-44s %8.2f ms\n", "whole window (resize / first paint)", fullFrame);
    std::printf ("  %-44s %8.2f ms\n", "3 step grids (playhead columns)", gridsMs);
    std::printf ("  %-44s %8.2f ms\n", "3 output meters", metersMs);
    std::printf ("  %-44s %8.2f ms\n", "3 LFO waves", lfoMs);
    std::printf ("  %-44s %8.2f ms\n", "phrase indicator + scene pads", headerMs);
    std::printf ("  %-44s %8.2f ms  -> %5.1f %% of one core at 30 fps\n", "Live view frame (all of the above)", liveFrame, liveFrame * 30.0 / 10.0);
    std::printf ("  %-44s %8.2f ms  -> %5.1f %% of one core at 30 fps\n", "Live view off (phrase + scene pads only)", headerMs, headerMs * 30.0 / 10.0);

    // Breakdown: each component's own paint() on a small region, at 2x.
    std::printf ("\nUI breakdown (own paint() only, 2x, 100 x 100 region, us per call)\n");
    const auto timePaint = [] (juce::Component& c, juce::Rectangle<int> area, int calls)
    {
        juce::Image image (juce::Image::ARGB, juce::jmax (1, c.getWidth() * 2), juce::jmax (1, c.getHeight() * 2), true, juce::SoftwareImageType());
        const auto start = Clock::now();
        for (int i = 0; i < calls; ++i)
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            g.reduceClipRegion (area);
            c.paint (g);
        }
        return secondsSince (start) / calls * 1.0e6;
    };

    auto* strip = grids[0]->getParentComponent();
    const auto small = juce::Rectangle<int> (0, 0, 100, 100);
    std::printf ("  %-44s %8.1f\n", "editor background", timePaint (*editor, small.withPosition (300, 300), 200));
    std::printf ("  %-44s %8.1f\n", "lane strip panel", timePaint (*strip, small, 200));
    std::printf ("  %-44s %8.1f\n", "step grid (cached layer + playhead)", timePaint (*grids[0], small, 200));
    std::printf ("  %-44s %8.1f\n", "LFO panel", timePaint (*lfoPanels[0], small, 200));
    std::printf ("  %-44s %8.1f\n", "scene bar", timePaint (*sceneBars[0], small, 200));
    std::printf ("  %-44s %8.1f\n", "phrase indicator", timePaint (*phrases[0], phrases[0]->getLocalBounds(), 200));
    for (auto* child : lfoPanels[0]->getChildren())
        if (child->isOpaque())
            std::printf ("  %-44s %8.1f\n", "LFO scope", timePaint (*child, child->getLocalBounds(), 200));

    {
        juce::Image image (juce::Image::ARGB, 2760, 1760, true, juce::SoftwareImageType());
        const auto start = Clock::now();
        for (int i = 0; i < 200; ++i)
        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (2.0f));
            g.reduceClipRegion (small);
            modlanes::theme::paintPanel (g, small.toFloat());
        }
        std::printf ("  %-44s %8.1f\n", "theme::paintPanel", secondsSince (start) / 200 * 1.0e6);
    }

    //==============================================================================
    std::printf ("\nCHECKS\n");
    int failures = 0;
    const auto check = [&failures] (const char* name, bool ok, const juce::String& detail)
    {
        std::printf ("  [%s] %-46s %s\n", ok ? " ok " : "FAIL", name, detail.toRawUTF8());
        failures += ok ? 0 : 1;
    };

    // Fit: 5 steps over 1 bar of 4/4 = 0.8 quarter notes per step.
    check ("Fit: 5 steps in 1 bar", std::abs (modlanes::params::stepBeats (6, 2, 5, 4.0) - 0.8) < 1e-9,
           juce::String (modlanes::params::stepBeats (6, 2, 5, 4.0), 3) + " beats per step");

    // New LFO shapes stay in range at every skew.
    {
        float lo = 1e9f, hi = -1e9f;
        bool finite = true;
        for (int shape = 0; shape < modlanes::params::lfoShapeNames.size(); ++shape)
            for (float skew : { 0.02f, 0.25f, 0.5f, 0.75f, 0.98f })
                for (int i = 0; i < 1000; ++i)
                {
                    const float v = modlanes::evalGlobalLfo (shape, (float) i / 1000.0f, (std::uint32_t) i / 100, skew);
                    finite = finite && std::isfinite (v);
                    lo = std::min (lo, v);
                    hi = std::max (hi, v);
                }
        check ("LFO shapes x skew in -1..1", finite && lo >= -1.0001f && hi <= 1.0001f,
               juce::String (modlanes::params::lfoShapeNames.size()) + " shapes, range " + juce::String (lo, 2) + " .. " + juce::String (hi, 2));
    }

    // Natural alignment: 16 x 1/16 (1 bar), 12 x 1/16 (3 beats), 5 steps fitted into 1 bar -> 3 bars.
    proc.loadIntoCurrentScene (factory[0].second);
    setParam (proc, modlanes::params::freeRun, 1.0f);
    {
        setParam (proc, modlanes::params::lane (0, "steps"), 16.0f);
        setParam (proc, modlanes::params::lane (1, "steps"), 12.0f);
        setParam (proc, modlanes::params::lane (2, "steps"), 5.0f);
        setParam (proc, modlanes::params::lane (2, "fit"), 2.0f); // 1 bar
        modlanes::OrreryView view (proc);
        const double bars = view.naturalAlignmentBars();
        check ("Orrery: 1 bar, 3 beats, 1 bar realign", std::abs (bars - 3.0) < 1e-9, juce::String (bars, 3) + " bars");
        setParam (proc, modlanes::params::lane (2, "fit"), 0.0f);
    }

    // Phase: half a loop shifts the playhead by half the steps.
    {
        juce::AudioBuffer<float> buffer (4, 512);
        buffer.clear();
        juce::MidiBuffer midi;
        setParam (proc, modlanes::params::freeRun, 0.0f);
        proc.processBlock (buffer, midi); // stop
        setParam (proc, modlanes::params::lane (0, "phase"), 0.5f);
        setParam (proc, modlanes::params::freeRun, 1.0f);
        midi.clear();
        proc.processBlock (buffer, midi); // starts from the current internal position
        const float withPhase = proc.lanePosition[0].load();
        setParam (proc, modlanes::params::lane (0, "phase"), 0.0f);
        midi.clear();
        proc.processBlock (buffer, midi);
        const float without = proc.lanePosition[0].load();
        const float shift = std::fmod (withPhase - without + 16.0f, 16.0f);
        check ("Phase 50% shifts 16 steps by ~8", std::abs (shift - 8.0f) < 0.5f, juce::String (shift, 2) + " steps");
    }

    // MIDI export -> import gives the same CC curve back.
    {
        auto pattern = modlanes::LanePattern();
        auto sine = modlanes::Block::make (modlanes::BlockType::Sine, 0, 16);
        sine.cycles = 2.0f;
        sine.p[2] = 1.0f;
        pattern.add (sine);
        proc.setPattern (0, pattern);
        setParam (proc, modlanes::params::lane (0, "steps"), 16.0f);
        setParam (proc, modlanes::params::lane (0, "rate"), 6.0f); // 1/16

        const auto file = modlanes::exportLaneToMidi (proc, 0);
        modlanes::midiimport::CcCurve curve;
        const bool read = modlanes::midiimport::read (file, (int) proc.apvts.getRawParameterValue (modlanes::params::lane (0, "cc"))->load(), curve);

        float worst = 0.0f;
        for (int k = 0; read && k < 400; ++k)
        {
            const double beat = 4.0 * k / 400.0;
            const float expected = modlanes::blockValue (sine, (float) (beat / 4.0)); // 16 steps x 1/16 = 4 beats
            worst = std::max (worst, std::abs (modlanes::midiimport::valueAt (curve, beat) - expected));
        }
        check ("MIDI export -> import round trip", read && curve.lengthBeats == 4.0 && worst < 0.03f,
               "length " + juce::String (curve.lengthBeats, 1) + " beats, max error " + juce::String (worst, 3) + " (1 CC step = 0.008)");
    }

    // Parameter modulation maths: ranges, stepped values, cycles in octaves.
    {
        auto euclid = modlanes::Block::make (modlanes::BlockType::Euclid, 0, 4);
        euclid.p[0] = 3.0f;
        auto hits = euclid;
        modlanes::applyParamMod (hits, 0, 0.1f);       // +10% of 1..32
        auto clamped = euclid;
        modlanes::applyParamMod (clamped, 0, 5.0f);    // far past the top
        auto cycles = modlanes::Block::make (modlanes::BlockType::Sine, 0, 4);
        cycles.cycles = 2.0f;
        modlanes::applyParamMod (cycles, modlanes::kModCycles, 0.5f); // +1 octave
        const bool ok = hits.p[0] == std::round (hits.p[0]) && hits.p[0] == 6.0f && clamped.p[0] == 32.0f && std::abs (cycles.cycles - 4.0f) < 1e-4f;
        check ("applyParamMod: stepped, clamped, octaves", ok,
               "hits 3 -> " + juce::String (hits.p[0]) + ", clamp -> " + juce::String (clamped.p[0]) + ", cycles 2 -> " + juce::String (cycles.cycles, 2));
    }

    // End to end: an LFO on a block's Cycles changes the CC output while playing.
    {
        proc.loadIntoCurrentScene (factory[0].second);
        modlanes::LanePattern pattern;
        auto sine = modlanes::Block::make (modlanes::BlockType::Sine, 0, 16);
        sine.p[2] = 1.0f; // unipolar
        pattern.add (sine);
        proc.setPattern (0, pattern);
        const int id = proc.getPattern (0).blocks[0].id;

        const auto collect = [&]
        {
            juce::AudioBuffer<float> buffer (4, 512);
            buffer.clear();
            juce::MidiBuffer midi;
            setParam (proc, modlanes::params::freeRun, 0.0f);
            proc.processBlock (buffer, midi);
            setParam (proc, modlanes::params::freeRun, 1.0f);
            std::vector<int> ccs;
            for (int b = 0; b < 200; ++b)
            {
                midi.clear();
                proc.processBlock (buffer, midi);
                for (const auto m : midi)
                    if (m.getMessage().isController())
                        ccs.push_back (m.getMessage().getControllerValue());
            }
            return ccs;
        };

        const auto plain = collect();
        modlanes::LfoRouting routing;
        modlanes::ModRoute route { modlanes::ModTarget::BlockParam, 0, id, 1.0f };
        route.param = modlanes::kModCycles;
        routing.add (route);
        proc.setRouting (0, routing);
        setParam (proc, modlanes::params::lfo (0, "div"), 7.0f); // fast LFO (1/16)
        const auto modulated = collect();
        proc.setRouting (0, {});

        const bool inRange = std::all_of (modulated.begin(), modulated.end(), [] (int v) { return v >= 0 && v <= 127; });
        check ("LFO -> block Cycles changes the output", inRange && modulated != plain && ! modulated.empty(),
               juce::String ((int) plain.size()) + " vs " + juce::String ((int) modulated.size()) + " CC events, differ: "
                   + (modulated != plain ? "yes" : "no"));
    }

    std::printf ("  %s\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");

    editor = nullptr;
    return failures;
}
