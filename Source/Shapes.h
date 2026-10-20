#pragma once

#include "ModuleTypes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace modlanes
{

constexpr float kTwoPi = 6.283185307179586f;

inline float frac (double x) { return (float) (x - std::floor (x)); }

inline float hash01 (std::uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return (float) (x & 0xffffffU) / (float) 0xffffffU;
}

//==============================================================================
struct Wavetables
{
    static constexpr int kTables = 5, kFrames = 4, kSize = 256;

    float data[kTables][kFrames][kSize];

    static const char* name (int table)
    {
        static const char* names[kTables] = { "Harmonics", "Pulses", "Stairs", "Bounce", "Wobble" };
        return names[std::clamp (table, 0, kTables - 1)];
    }

    Wavetables()
    {
        for (int f = 0; f < kFrames; ++f)
        {
            for (int i = 0; i < kSize; ++i)
            {
                const float x = (float) i / (float) kSize;

                // Harmonics: sine growing towards a saw
                float h = 0.0f;
                for (int k = 1; k <= 1 + f * 3; ++k)
                    h += std::sin (kTwoPi * (float) k * x) / (float) k;
                data[0][f][i] = h;

                // Pulses: narrowing pulse width
                const float widths[kFrames] = { 0.5f, 0.3f, 0.15f, 0.05f };
                data[1][f][i] = x < widths[f] ? 1.0f : -1.0f;

                // Stairs: quantised ramp with 2, 4, 8, 16 levels
                const int levels = 2 << f;
                data[2][f][i] = std::floor (x * (float) levels) / (float) (levels - 1) * 2.0f - 1.0f;

                // Bounce: decaying bouncing-ball arcs
                const int bounces[kFrames] = { 2, 3, 5, 8 };
                data[3][f][i] = std::abs (std::sin (kTwoPi * 0.5f * x * (float) bounces[f])) * (1.0f - x) * 2.0f - 1.0f;

                // Wobble: FM sine with growing index
                const float index[kFrames] = { 0.0f, 1.0f, 2.5f, 5.0f };
                data[4][f][i] = std::sin (kTwoPi * x + index[f] * std::sin (kTwoPi * 3.0f * x));
            }
        }

        for (auto& table : data)
            for (auto& frame : table)
            {
                float peak = 0.0f;
                for (float v : frame) peak = std::max (peak, std::abs (v));
                if (peak > 0.0f)
                    for (float& v : frame) v /= peak;
            }
    }

    float read (int table, float morph, float phase) const
    {
        table = std::clamp (table, 0, kTables - 1);
        const float fpos = std::clamp (morph, 0.0f, 1.0f) * (float) (kFrames - 1);
        const int f0 = (int) fpos, f1 = std::min (f0 + 1, kFrames - 1);
        const float fm = fpos - (float) f0;

        const float pos = frac (phase) * (float) kSize;
        const int i0 = (int) pos % kSize, i1 = (i0 + 1) % kSize;
        const float fr = pos - std::floor (pos);

        const auto sample = [&] (int f) { return data[table][f][i0] + fr * (data[table][f][i1] - data[table][f][i0]); };
        const float a = sample (f0);
        return a + fm * (sample (f1) - a);
    }
};

// Constructed once on first use; the processor touches it in its constructor
// so the audio thread never pays for the initialisation.
inline const Wavetables& wavetables()
{
    static const Wavetables w;
    return w;
}

//==============================================================================
inline float curveRise (float x, float c) { return std::pow (x, std::exp2 (c * 2.5f)); }
inline float curveFall (float x, float c) { return 1.0f - std::pow (x, std::exp2 (-c * 2.5f)); }

inline float evalEnvelope (const Block& b, float t)
{
    float a = b.p[0], d = b.p[1], r = b.p[3];
    const float s = b.p[2], c = b.p[4];

    if (const float total = a + d + r; total > 1.0f)
    {
        a /= total; d /= total; r /= total;
    }

    if (t < a)
        return curveRise (t / a, c);
    t -= a;

    if (t < d)
        return s + (1.0f - s) * curveFall (t / d, c);
    t -= d;

    const float sustainLength = 1.0f - a - d - r;
    if (t < sustainLength)
        return s;
    t -= sustainLength;

    return r > 0.0f ? s * curveFall (std::min (1.0f, t / r), c) : 0.0f;
}

inline float evalRamp (const Block& b, float t)
{
    const float start = b.p[0], peak = b.p[1], end = b.p[2];

    if (t < peak)
        return start + (1.0f - start) * (t / peak);

    return peak >= 1.0f ? 1.0f : 1.0f + (end - 1.0f) * ((t - peak) / (1.0f - peak));
}

// Basic bipolar LFO shape for a phase in [0, 1).
inline float lfoShape (BlockType type, float ph, float width)
{
    switch (type)
    {
        case BlockType::Sine:     return std::sin (kTwoPi * ph);
        case BlockType::Triangle: return (ph < width ? ph / width : 1.0f - (ph - width) / (1.0f - width)) * 2.0f - 1.0f;
        case BlockType::SawUp:    return ph * 2.0f - 1.0f;
        case BlockType::SawDown:  return 1.0f - ph * 2.0f;
        case BlockType::Square:   return ph < width ? 1.0f : -1.0f;
        default:                  return 0.0f;
    }
}

inline float evalSampleHold (const Block& b, float t)
{
    const float steps = std::max (1.0f, std::round (b.cycles));
    const float smooth = b.p[0];
    const auto seed = (std::uint32_t) std::round (b.p[1]);

    const float x = t * steps;
    const auto index = (std::uint32_t) (std::int64_t) std::floor (x);
    const float f = x - std::floor (x);

    float v = hash01 (seed * 1000003U + index);
    if (smooth > 0.0f && f > 1.0f - smooth)
    {
        float k = (f - (1.0f - smooth)) / smooth;
        k = k * k * (3.0f - 2.0f * k);
        v += k * (hash01 (seed * 1000003U + index + 1) - v);
    }

    return v * 2.0f - 1.0f;
}

inline float toUnipolarIf (float v, float flag) { return flag >= 0.5f ? 0.5f * (v + 1.0f) : v; }

//==============================================================================
inline float bendSegment (float t, float y0, float y1, float bend)
{
    // Positive bend bows upwards whether the segment rises or falls; a falling
    // segment is the mirror image of a rising one.
    t = std::clamp (t, 0.0f, 1.0f);
    const float exponent = std::exp2 (-bend * 2.5f);
    return y1 >= y0 ? y0 + (y1 - y0) * std::pow (t, exponent)
                    : y1 + (y0 - y1) * std::pow (1.0f - t, exponent);
}

// Breakpoints sorted by x over [0, length), the last segment wrapping to the first.
inline float evalPointArray (const EnvPoint* points, int n, float length, double pos)
{
    if (n == 0)
        return 0.0f;
    if (n == 1)
        return points[0].y;

    int i = -1;
    while (i + 1 < n && points[i + 1].x <= pos)
        ++i;

    EnvPoint a, b;
    if (i < 0)           { a = points[n - 1]; a.x -= length; b = points[0]; }
    else if (i == n - 1) { a = points[i]; b = points[0]; b.x += length; }
    else                 { a = points[i]; b = points[i + 1]; }

    if (b.x - a.x <= 1.0e-6f)
        return b.y;

    return bendSegment ((float) ((pos - a.x) / (b.x - a.x)), a.y, b.y, a.bend);
}

//==============================================================================
// Shapes added later

// Step sequencer: one value per step, optionally gliding into the next one.
inline float evalSteps (const Block& b, float t)
{
    const int count = std::clamp ((int) std::round (b.cycles), 1, kMaxSteps);
    const float x = t * (float) count;
    const int index = std::clamp ((int) x, 0, count - 1);
    float v = b.stepValues[(size_t) index];

    const float glide = b.p[0], f = x - (float) index;
    if (glide > 0.0f && f > 1.0f - glide && index + 1 < count)
    {
        float k = (f - (1.0f - glide)) / glide;
        k = k * k * (3.0f - 2.0f * k);
        v += k * (b.stepValues[(size_t) index + 1] - v);
    }

    return b.p[1] >= 0.5f ? v * 2.0f - 1.0f : v;
}

// A drawn curve (from a user module), repeated `cycles` times.
inline float evalCurve (const Block& b, float t)
{
    const float repeats = std::max (1.0f, std::round (b.cycles));
    const float x = t * repeats - std::floor (t * repeats);
    return evalPointArray (b.curve.data(), b.numCurvePoints, 1.0f, x);
}

// Damped sine: cycles oscillations dying away over the block.
inline float evalSpring (const Block& b, float t)
{
    const float damping = b.p[0] * 10.0f;
    return toUnipolarIf (std::sin (kTwoPi * (t * b.cycles + b.p[1])) * std::exp (-damping * std::max (0.0f, t)), b.p[2]);
}

// Bouncing ball: each bounce is shorter and lower by the elasticity, 0..1.
inline float evalBounce (const Block& b, float t)
{
    const int bounces = std::max (1, (int) std::round (b.cycles));
    const float r = b.p[0];

    float total = 0.0f;
    for (int i = 0; i < bounces; ++i)
        total += std::pow (r, (float) i);

    float start = 0.0f;
    for (int i = 0; i < bounces; ++i)
    {
        const float duration = std::pow (r, (float) i) / total;
        if (t < start + duration || i == bounces - 1)
        {
            const float u = std::clamp ((t - start) / duration, 0.0f, 1.0f);
            return std::pow (r, 2.0f * (float) i) * 4.0f * u * (1.0f - u);
        }
        start += duration;
    }
    return 0.0f;
}

// Euclidean rhythm: Hits spread evenly over `cycles` slots, each a decaying pulse, 0..1.
inline float evalEuclid (const Block& b, float t)
{
    const int slots = std::max (1, (int) std::round (b.cycles));
    const int hits = std::clamp ((int) std::round (b.p[0]), 0, slots);
    const int rotate = (int) std::round (b.p[1]);

    const float x = t * (float) slots;
    const int slot = std::min (slots - 1, (int) x);
    const int s = ((slot - rotate) % slots + slots) % slots;
    const bool hit = (s * hits) / slots != ((s + 1) * hits) / slots;
    if (! hit)
        return 0.0f;

    const float u = x - (float) slot;
    return std::pow (1.0f - u, 1.0f + b.p[2] * 8.0f);
}

// Smooth random wandering through `cycles` points, with optional finer detail.
inline float evalDrift (const Block& b, float t)
{
    const auto seed = (std::uint32_t) std::round (b.p[0]);
    const auto noise = [seed] (float x)
    {
        const float i = std::floor (x), f = x - i;
        const auto at = [seed] (float n) { return hash01 (seed * 104729U + (std::uint32_t) (std::int64_t) n * 2654435761U) * 2.0f - 1.0f; };
        const float s = f * f * (3.0f - 2.0f * f);
        return at (i) + s * (at (i + 1.0f) - at (i));
    };

    const float x = t * std::max (1.0f, std::round (b.cycles));
    const float rough = b.p[1];
    const float v = (noise (x) + rough * 0.5f * noise (x * 3.0f + 17.0f)) / (1.0f + rough * 0.5f);
    return toUnipolarIf (v, b.p[2]);
}

// Quantised ramp with `cycles` levels: up, down or up-down.
inline float evalStairs (const Block& b, float t)
{
    const int levels = std::max (2, (int) std::round (b.cycles));
    const int direction = (int) std::round (b.p[0]);

    float x = t;
    if (direction == 1) x = 1.0f - t;
    if (direction == 2) x = t < 0.5f ? t * 2.0f : 2.0f - t * 2.0f;

    const float v = std::min ((float) (levels - 1), std::floor (x * (float) levels)) / (float) (levels - 1);
    return toUnipolarIf (v * 2.0f - 1.0f, b.p[1]);
}

// Logistic map x -> r x (1 - x), one value per segment, optionally smoothed.
inline float evalChaos (const Block& b, float t)
{
    const int count = std::max (1, (int) std::round (b.cycles));
    const float r = b.p[0], smooth = b.p[1];
    const float x = t * (float) count;
    const int index = std::clamp ((int) x, 0, count - 1);

    float v = std::clamp (b.p[2], 0.01f, 0.99f), next = v;
    for (int i = 0; i <= index; ++i)
    {
        v = next;
        next = r * v * (1.0f - v);
    }

    const float f = x - (float) index;
    if (smooth > 0.0f && f > 1.0f - smooth)
    {
        float k = (f - (1.0f - smooth)) / smooth;
        k = k * k * (3.0f - 2.0f * k);
        v += k * (next - v);
    }
    return v * 2.0f - 1.0f;
}

// Heartbeat: a strong and a weaker pulse per beat, 0..1.
inline float evalHeartbeat (const Block& b, float t)
{
    const float x = t * std::max (1.0f, std::round (b.cycles));
    const float u = x - std::floor (x);
    const float width = 0.012f + (1.0f - b.p[0]) * 0.03f;
    const auto bump = [u, width] (float centre, float height) { const float d = (u - centre) / width; return height * std::exp (-d * d); };
    return std::clamp (bump (0.12f, 1.0f) + bump (0.3f, 0.55f) - bump (0.2f, 0.15f), 0.0f, 1.0f);
}

inline float evalFormula (const Block& b, float t)
{
    const float x = t * b.cycles;
    const formula::Inputs in { t, x - std::floor (x), std::floor (x), b.p[1], b.p[2] };
    return formula::clip (formula::evaluate (b.program, in), (int) std::round (b.p[0]));
}

// Raw shape of a block (cycles applied, before amplitude and fades) at t in [0, 1). Range -1..1.
inline float evaluateShape (const Block& b, float t)
{
    const float repeats = std::max (1.0f, std::round (b.cycles));
    const float rt = frac ((double) t * repeats);

    switch (b.type)
    {
        case BlockType::Envelope:    return evalEnvelope (b, rt);
        case BlockType::InvEnvelope: return -evalEnvelope (b, rt);
        case BlockType::Ramp:        return evalRamp (b, rt);
        case BlockType::Line:        return b.p[0] + rt * (b.p[1] - b.p[0]);

        case BlockType::Sine:
        case BlockType::Triangle:
        case BlockType::SawUp:
        case BlockType::SawDown:
        case BlockType::Square:
            return toUnipolarIf (lfoShape (b.type, frac ((double) t * b.cycles + b.p[0]), b.p[1]), b.p[2]);

        case BlockType::SampleHold:
            return toUnipolarIf (evalSampleHold (b, t), b.p[2]);

        case BlockType::Wavetable:
            return toUnipolarIf (wavetables().read ((int) std::round (b.p[0]), b.p[1], frac ((double) t * b.cycles + b.p[2])), b.p[3]);

        case BlockType::Spring:    return evalSpring (b, t);
        case BlockType::Bounce:    return evalBounce (b, t);
        case BlockType::Euclid:    return evalEuclid (b, t);
        case BlockType::Drift:     return evalDrift (b, t);
        case BlockType::Stairs:    return evalStairs (b, t);
        case BlockType::Chaos:     return evalChaos (b, t);
        case BlockType::Heartbeat: return evalHeartbeat (b, t);
        case BlockType::Formula:   return evalFormula (b, t);
        case BlockType::Steps:     return evalSteps (b, t);
        case BlockType::Curve:     return evalCurve (b, t);

        case BlockType::NumTypes:
            break;
    }

    return 0.0f;
}

// Amplitude envelope from the fade-in / fade-out handles, 0..1.
inline float fadeGain (const Block& b, float t)
{
    const auto ease = [] (float x) { return x * x * (3.0f - 2.0f * x); };
    float g = 1.0f;

    if (b.fadeIn > 0.0f && t < b.fadeIn)
        g = ease (t / b.fadeIn);

    if (b.fadeOut > 0.0f && t > 1.0f - b.fadeOut)
        g = std::min (g, ease ((1.0f - t) / b.fadeOut));

    return g;
}

// Final block output at t: shape * amplitude (plus modulation) * fades.
inline float blockValue (const Block& b, float t, float ampMod = 0.0f)
{
    const float amp = std::clamp (b.amount + ampMod, -1.0f, 1.0f);
    return amp * fadeGain (b, t) * evaluateShape (b, t);
}

//==============================================================================
// Periodic shapes keep running past their block edges, which is what makes
// crossfades between blocks smooth; envelopes hold their first / last value.
inline bool isPeriodic (BlockType t)
{
    switch (t)
    {
        case BlockType::Sine: case BlockType::Triangle: case BlockType::SawUp: case BlockType::SawDown:
        case BlockType::Square: case BlockType::SampleHold: case BlockType::Wavetable:
        case BlockType::Spring: case BlockType::Drift:
            return true;
        default:
            return false;
    }
}

// Block value at a lane position (in steps), which may lie outside the block.
inline float blockValueAt (const Block& b, int steps, double pos, float ampMod)
{
    const int length = std::max (1, std::min (b.length, steps - b.start));
    const double t = (pos - b.start) / length;
    const float clamped = std::clamp ((float) t, 0.0f, 0.99999f);
    const float amp = std::clamp (b.amount + ampMod, -1.0f, 1.0f);
    return amp * fadeGain (b, clamped) * evaluateShape (b, isPeriodic (b.type) ? (float) t : clamped);
}

// A block as it plays right now: possibly a modulated copy, plus its amplitude modulation.
struct BlockView
{
    const Block* block;
    float ampMod;
};

inline BlockView unmodulated (const Block& b) { return { &b, 0.0f }; }

// Applies LFO modulation m (amount x LFO value, about -1..1) to one parameter.
// Ranged parameters move by m times their full range around the current setting,
// Cycles by up to +-2 octaves, stepped values stay whole numbers.
inline void applyParamMod (Block& b, int param, float m)
{
    if (param == kModCycles)
    {
        const float c = b.cycles * std::exp2 (2.0f * m);
        b.cycles = std::clamp (getTypeInfo (b.type).integerCycles ? std::round (c) : c, minCycles (b.type), kMaxCycles);
        return;
    }
    if (param == kModFadeIn)  { b.fadeIn = std::clamp (b.fadeIn + m, 0.0f, 1.0f); return; }
    if (param == kModFadeOut) { b.fadeOut = std::clamp (b.fadeOut + m, 0.0f, 1.0f); return; }

    const auto& info = getTypeInfo (b.type);
    if (param < 0 || param >= info.numParams)
        return;

    const auto& range = info.params[(size_t) param];
    float v = std::clamp (b.p[(size_t) param] + m * (range.max - range.min), range.min, range.max);
    if (range.stepped)
        v = std::round (v);
    b.p[(size_t) param] = v;
}

// A block, or a stretch of empty steps between blocks (block == -1).
struct Region
{
    int start, end, block;
};

inline Region regionAt (const LanePattern& p, int steps, int step)
{
    if (const int bi = p.findBlockAt (step); bi >= 0)
    {
        const auto& b = p.blocks[(size_t) bi];
        return { b.start, std::min (b.start + b.length, steps), bi };
    }

    int s0 = step, s1 = step + 1;
    while (s0 > 0 && p.findBlockAt (s0 - 1) < 0) --s0;
    while (s1 < steps && p.findBlockAt (s1) < 0) ++s1;
    return { s0, s1, -1 };
}

// The lane's value at `pos` (in steps). With blend > 0, each edge between two
// regions becomes a crossfade `blend` steps wide (limited by the region sizes),
// including the wrap from the last step back to the first.
// `resolve` maps each block to the version that plays now (see BlockView).
template <typename ResolveFn>
float laneValueAt (const LanePattern& p, int steps, double pos, float blend, ResolveFn&& resolve)
{
    const auto valueOf = [&] (const Region& r, double x)
    {
        if (r.block < 0)
            return 0.0f;

        const auto& b = p.blocks[(size_t) r.block];
        const BlockView view = resolve (b);
        return blockValueAt (*view.block, steps, x, view.ampMod);
    };

    const auto ease = [] (double u) { const float x = (float) u; return x * x * (3.0f - 2.0f * x); };

    const Region cur = regionAt (p, steps, std::clamp ((int) pos, 0, steps - 1));
    const float self = valueOf (cur, pos);
    if (blend <= 0.0f)
        return self;

    const double length = cur.end - cur.start;

    {
        const Region next = regionAt (p, steps, cur.end % steps);
        const double shift = cur.end >= steps ? steps : 0.0;
        const double half = std::min ({ (double) blend * 0.5, length * 0.5, (next.end - next.start) * 0.5 });
        const double dr = cur.end - pos;
        if (half > 0.0 && dr < half)
            return self + ease (0.5 - dr / (2.0 * half)) * (valueOf (next, pos - shift) - self);
    }

    {
        const Region prev = regionAt (p, steps, cur.start == 0 ? steps - 1 : cur.start - 1);
        const double shift = cur.start <= 0 ? -steps : 0.0;
        const double half = std::min ({ (double) blend * 0.5, length * 0.5, (prev.end - prev.start) * 0.5 });
        const double dl = pos - cur.start;
        if (half > 0.0 && dl < half)
        {
            const float before = valueOf (prev, pos - shift);
            return before + ease (0.5 + dl / (2.0 * half)) * (self - before);
        }
    }

    return self;
}

//==============================================================================
// Points view
// Value of the point envelope at pos (in steps), looping over `steps`.
// Points beyond the current step count are ignored.
inline float evalPoints (const PointEnvelope& e, int steps, double pos)
{
    int n = 0;
    while (n < e.numPoints && e.points[(size_t) n].x <= (float) steps)
        ++n;

    return evalPointArray (e.points.data(), n, (float) steps, pos);
}

// Simplifies a sampled curve (xs ascending, last sample = wrap point) to at most
// maxPoints breakpoints: Ramer-Douglas-Peucker on vertical error, with the
// tolerance raised until it fits, then a bend fitted to every segment.
inline std::vector<EnvPoint> fitPoints (const std::vector<float>& xs, const std::vector<float>& ys, int maxPoints)
{
    const int count = (int) xs.size() - 1;
    std::vector<bool> keep;

    for (float tolerance = 0.006f;; tolerance *= 1.5f)
    {
        keep.assign (xs.size(), false);
        keep.front() = keep.back() = true;

        std::vector<std::pair<int, int>> stack { { 0, count } };
        while (! stack.empty())
        {
            const auto [lo, hi] = stack.back();
            stack.pop_back();

            int worst = -1;
            float worstError = tolerance;
            for (int k = lo + 1; k < hi; ++k)
            {
                const float t = (xs[(size_t) k] - xs[(size_t) lo]) / (xs[(size_t) hi] - xs[(size_t) lo]);
                const float error = std::abs (ys[(size_t) k] - (ys[(size_t) lo] + t * (ys[(size_t) hi] - ys[(size_t) lo])));
                if (error > worstError) { worstError = error; worst = k; }
            }

            if (worst >= 0)
            {
                keep[(size_t) worst] = true;
                stack.push_back ({ lo, worst });
                stack.push_back ({ worst, hi });
            }
        }

        // The final sample duplicates the first one (the curve wraps).
        if ((int) std::count (keep.begin(), keep.end() - 1, true) <= maxPoints)
            break;
    }

    std::vector<int> kept;
    for (int k = 0; k <= count; ++k)
        if (keep[(size_t) k])
            kept.push_back (k);

    std::vector<EnvPoint> points;
    for (size_t s = 0; s + 1 < kept.size(); ++s)
    {
        const int lo = kept[s], hi = kept[s + 1];
        float bestBend = 0.0f, bestError = std::numeric_limits<float>::max();

        for (int b = -20; b <= 20; ++b)
        {
            const float bend = (float) b / 20.0f;
            float error = 0.0f;
            for (int k = lo + 1; k < hi; ++k)
            {
                const float t = (xs[(size_t) k] - xs[(size_t) lo]) / (xs[(size_t) hi] - xs[(size_t) lo]);
                error += std::abs (bendSegment (t, ys[(size_t) lo], ys[(size_t) hi], bend) - ys[(size_t) k]);
            }

            if (error < bestError - 1.0e-6f || (error <= bestError + 1.0e-6f && std::abs (bend) < std::abs (bestBend)))
            {
                bestError = error;
                bestBend = bend;
            }
        }

        points.push_back ({ xs[(size_t) lo], ys[(size_t) lo], bestBend });
    }

    return points;
}

// Turns the block layout (including Blend) into breakpoints for the Points view.
inline PointEnvelope renderBlocksToPoints (const LanePattern& p, int steps, float blend)
{
    constexpr int perStep = 32;
    const int count = steps * perStep;
    const auto none = [] (const Block& b) { return unmodulated (b); };

    std::vector<float> xs ((size_t) count + 1), ys ((size_t) count + 1);
    for (int k = 0; k <= count; ++k)
    {
        xs[(size_t) k] = (float) k / (float) perStep;
        ys[(size_t) k] = laneValueAt (p, steps, std::min ((double) xs[(size_t) k], steps - 1.0e-4), blend, none);
    }

    PointEnvelope e;
    for (const auto& point : fitPoints (xs, ys, kMaxPoints))
        e.points[(size_t) e.numPoints++] = point;
    return e;
}

// A lane's Points envelope as a Curve block (x normalised to 0..1), for user modules.
inline Block curveBlockFromEnvelope (const PointEnvelope& envelope, int steps)
{
    constexpr int perStep = 32;
    const int count = steps * perStep;

    std::vector<float> xs ((size_t) count + 1), ys ((size_t) count + 1);
    for (int k = 0; k <= count; ++k)
    {
        xs[(size_t) k] = (float) k / (float) count;
        ys[(size_t) k] = evalPoints (envelope, steps, std::min ((double) k / perStep, steps - 1.0e-4));
    }

    auto b = Block::make (BlockType::Curve, 0, steps);
    b.numCurvePoints = 0;
    for (const auto& point : fitPoints (xs, ys, kMaxCurvePoints))
        b.curve[(size_t) b.numCurvePoints++] = point;
    return b;
}

// Skew bends the phase so the first half of the cycle takes `skew` of the time:
// pulse width for squares, tilt for triangles, asymmetry for sines.
inline float skewPhase (float phase, float skew)
{
    skew = std::clamp (skew, 0.02f, 0.98f);
    return phase < skew ? 0.5f * phase / skew : 0.5f + 0.5f * (phase - skew) / (1.0f - skew);
}

// Global (orbit-modulating) LFO at a phase in [0, 1) within cycle number `cycle`.
// shape indexes params::lfoShapeNames. Range -1..1.
inline float evalGlobalLfo (int shape, float phase, std::uint32_t cycle, float skew = 0.5f)
{
    const float w = skewPhase (phase, skew);
    const auto random = [] (std::uint32_t c) { return hash01 (c * 2654435761U + 17U) * 2.0f - 1.0f; };

    switch (shape)
    {
        case 0:  return std::sin (kTwoPi * w);
        case 1:  return lfoShape (BlockType::Triangle, w, 0.5f);
        case 2:  return w * 2.0f - 1.0f;
        case 3:  return 1.0f - w * 2.0f;
        case 4:  return w < 0.5f ? 1.0f : -1.0f;
        case 5:  return random (cycle);
        case 6:
        {
            const float k = w * w * (3.0f - 2.0f * w);
            return random (cycle) + k * (random (cycle + 1) - random (cycle));
        }
        case 7:  return (std::exp (4.0f * w) - 1.0f) / (std::exp (4.0f) - 1.0f) * 2.0f - 1.0f;
        case 8:  return 1.0f - (std::exp (4.0f * w) - 1.0f) / (std::exp (4.0f) - 1.0f) * 2.0f;
        case 9:  return std::min (3.0f, std::floor (w * 4.0f)) / 3.0f * 2.0f - 1.0f;
        case 10: return std::sin (kTwoPi * 0.5f * w) * 2.0f - 1.0f;
        default: return 0.0f;
    }
}

} // namespace modlanes
