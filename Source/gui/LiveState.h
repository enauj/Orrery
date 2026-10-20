#pragma once

#include "PluginProcessor.h"
#include "Shapes.h"

namespace modlanes
{

// An orbit's playhead extrapolated from the last audio block, in steps (-1 when
// idle). The audio thread reports positions once per block; moving on by the
// time elapsed since then keeps animations smooth with large host buffers.
inline float livePosition (const ModLanesProcessor& proc, int lane, int steps, bool wrap)
{
    const float position = proc.lanePosition[lane].load();
    if (position < 0.0f)
        return -1.0f;

    const double elapsed = juce::jlimit (0.0, 0.1, (juce::Time::getMillisecondCounterHiRes() - proc.positionTime.load()) / 1000.0);
    double p = position + elapsed * proc.laneSpeed[lane].load();
    p = wrap ? std::fmod (p, (double) steps) : std::min (p, steps - 1.0e-3);
    return (float) p;
}

// What the LFOs do right now, for drawing: the routings (cached per routing
// version) combined with the latest LFO outputs, the same way the audio thread does.
class LiveModulation
{
public:
    explicit LiveModulation (const ModLanesProcessor& p) : proc (p) {}

    // Re-reads the routings if they changed. Cheap; call once per frame.
    void update()
    {
        if (const int v = proc.getRoutingVersion(); v != seenVersion)
        {
            seenVersion = v;
            for (int j = 0; j < kNumLfos; ++j)
                routings[j] = proc.getRouting (j);
        }
    }

    // Sum of amount x LFO value over the routes to this target; false if none.
    // `lfo` gets the LFO pushing hardest (for the colour).
    bool sum (const ModRoute& target, float& total, int& lfo) const
    {
        bool found = false;
        float strongest = -1.0f;
        total = 0.0f;
        for (int j = 0; j < kNumLfos; ++j)
            for (int r = 0; r < routings[j].numRoutes; ++r)
                if (const auto& route = routings[j].routes[(size_t) r]; route.sameTarget (target))
                {
                    const float m = route.amount * proc.lfoOutput[j].load();
                    total += m;
                    found = true;
                    if (std::abs (m) > strongest)
                    {
                        strongest = std::abs (m);
                        lfo = j;
                    }
                }
        return found;
    }

    // Applies every route on this block (amplitude and parameters) to a copy of it;
    // false if no LFO touches it. `lfo` gets the LFO pushing hardest.
    bool modulate (int lane, Block& b, float& ampMod, int& lfo) const
    {
        bool found = false;
        float strongest = -1.0f;
        ampMod = 0.0f;
        for (int j = 0; j < kNumLfos; ++j)
            for (int r = 0; r < routings[j].numRoutes; ++r)
                if (const auto& route = routings[j].routes[(size_t) r]; route.targetsBlock() && route.lane == lane && route.blockId == b.id)
                {
                    const float m = route.amount * proc.lfoOutput[j].load();
                    found = true;
                    if (std::abs (m) > strongest)
                    {
                        strongest = std::abs (m);
                        lfo = j;
                    }
                    if (route.target == ModTarget::BlockAmp)
                        ampMod += m;
                    else
                        applyParamMod (b, route.param, m);
                }
        return found;
    }

    // True if any LFO routes to a block of this lane.
    bool touchesBlocks (int lane) const
    {
        for (int j = 0; j < kNumLfos; ++j)
            for (int r = 0; r < routings[j].numRoutes; ++r)
                if (routings[j].routes[(size_t) r].targetsBlock() && routings[j].routes[(size_t) r].lane == lane)
                    return true;
        return false;
    }

private:
    const ModLanesProcessor& proc;
    LfoRouting routings[kNumLfos];
    int seenVersion = -1;
};

} // namespace modlanes
