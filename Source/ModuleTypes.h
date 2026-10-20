#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "Formula.h"

namespace modlanes
{

constexpr int kNumLanes = 3;
constexpr int kNumLfos = 3;
constexpr int kMaxSteps = 32;
constexpr int kMaxBlocks = 32;
constexpr int kNumBlockParams = 5;
constexpr int kMaxRoutes = 4; // per LFO

// The modules that can be dragged onto a lane's step grid.
enum class BlockType : int
{
    Envelope = 0, // ADSR, 0..1
    Ramp,         // unsculpted linear rise/fall, 0..1
    InvEnvelope,  // ADSR mirrored into the negative range, 0..-1
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    SampleHold,
    Wavetable,
    Line,         // straight segment from start to end level
    // Added later; appended so saved type indices stay valid.
    Spring,       // damped sine
    Bounce,       // bouncing ball
    Euclid,       // euclidean pulses with decay
    Drift,        // smooth random
    Stairs,       // quantised ramp
    Chaos,        // logistic map
    Heartbeat,    // double pulse
    Formula,      // user expression
    Steps,        // step sequencer: one value per step
    Curve,        // a drawn curve saved as a user module
    NumTypes
};

constexpr int kNumBlockTypes = (int) BlockType::NumTypes;

struct ParamInfo
{
    const char* name = nullptr;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    bool stepped = false;
};

struct TypeInfo
{
    const char* name;
    const char* shortName;
    bool integerCycles;   // repeats/steps (true) or fractional LFO cycles (false)
    float defaultCycles;
    int numParams;
    std::array<ParamInfo, kNumBlockParams> params;
};

inline const TypeInfo& getTypeInfo (BlockType t)
{
    static const auto lfo = [] (const char* name, const char* shortName)
    {
        return TypeInfo { name, shortName, false, 1.0f, 3, {{ { "Phase", 0.0f, 1.0f, 0.0f },
                                                                      { "Width", 0.02f, 0.98f, 0.5f },
                                                                      { "Unipolar", 0.0f, 1.0f, 0.0f, true } }} };
    };

    static const std::array<ParamInfo, kNumBlockParams> adsr {{ { "Attack", 0.0f, 1.0f, 0.05f },
                                                                { "Decay", 0.0f, 1.0f, 0.25f },
                                                                { "Sustain", 0.0f, 1.0f, 0.5f },
                                                                { "Release", 0.0f, 1.0f, 0.3f },
                                                                { "Curve", -1.0f, 1.0f, 0.0f } }};

    static const TypeInfo infos[kNumBlockTypes] = {
        { "Envelope", "ENV", true, 1.0f, 5, adsr },
        { "Ramp (raw)", "RAMP", true, 1.0f, 3, {{ { "Start", 0.0f, 1.0f, 0.0f },
                                                               { "Peak", 0.0f, 1.0f, 0.5f },
                                                               { "End", 0.0f, 1.0f, 0.0f } }} },
        { "Inverted Env", "-ENV", true, 1.0f, 5, adsr },
        lfo ("Sine", "SIN"),
        lfo ("Triangle", "TRI"),
        lfo ("Saw Up", "SAW+"),
        lfo ("Saw Down", "SAW-"),
        lfo ("Square", "SQR"),
        { "Sample & Hold", "S&H", true, 4.0f, 3, {{ { "Smooth", 0.0f, 1.0f, 0.0f },
                                                                 { "Seed", 0.0f, 99.0f, 1.0f, true },
                                                                 { "Unipolar", 0.0f, 1.0f, 0.0f, true } }} },
        { "Wavetable", "WT", false, 1.0f, 4, {{ { "Table", 0.0f, 4.0f, 0.0f, true },
                                                             { "Morph", 0.0f, 1.0f, 0.0f },
                                                             { "Phase", 0.0f, 1.0f, 0.0f },
                                                             { "Unipolar", 0.0f, 1.0f, 0.0f, true } }} },
        { "Line", "LINE", true, 1.0f, 2, {{ { "Start", -1.0f, 1.0f, 1.0f },
                                                         { "End", -1.0f, 1.0f, 0.0f } }} },
        { "Spring", "SPRG", false, 4.0f, 3, {{ { "Damping", 0.0f, 1.0f, 0.5f },
                                               { "Phase", 0.0f, 1.0f, 0.0f },
                                               { "Unipolar", 0.0f, 1.0f, 0.0f, true } }} },
        { "Bounce", "BNC", true, 5.0f, 1, {{ { "Elasticity", 0.2f, 0.95f, 0.65f } }} },
        { "Euclid", "EUCL", true, 8.0f, 3, {{ { "Hits", 1.0f, 32.0f, 3.0f, true },
                                              { "Rotate", 0.0f, 31.0f, 0.0f, true },
                                              { "Decay", 0.0f, 1.0f, 0.5f } }} },
        { "Drift", "DRFT", true, 4.0f, 3, {{ { "Seed", 0.0f, 99.0f, 1.0f, true },
                                              { "Rough", 0.0f, 1.0f, 0.3f },
                                              { "Unipolar", 0.0f, 1.0f, 0.0f, true } }} },
        { "Stairs", "STR", true, 4.0f, 2, {{ { "Direction", 0.0f, 2.0f, 0.0f, true },
                                              { "Unipolar", 0.0f, 1.0f, 1.0f, true } }} },
        { "Chaos", "CHAO", true, 8.0f, 3, {{ { "Chaos", 3.5f, 4.0f, 3.9f },
                                              { "Smooth", 0.0f, 1.0f, 0.0f },
                                              { "Start", 0.05f, 0.95f, 0.37f } }} },
        { "Heartbeat", "HRT", true, 2.0f, 1, {{ { "Sharpness", 0.0f, 1.0f, 0.5f } }} },
        { "Formula", "f(x)", false, 1.0f, 3, {{ { "Clip", 0.0f, 2.0f, 1.0f, true },
                                                { "A", 0.0f, 1.0f, 0.5f },
                                                { "B", 0.0f, 1.0f, 0.5f } }} },
        { "Steps", "STEP", true, 8.0f, 2, {{ { "Glide", 0.0f, 1.0f, 0.0f },
                                              { "Bipolar", 0.0f, 1.0f, 0.0f, true } }} },
        { "Curve", "CRV", true, 1.0f, 0, {{}} },
    };

    return infos[(int) t];
}

constexpr float kMaxCycles = 32.0f;

inline float minCycles (BlockType t) { return getTypeInfo (t).integerCycles ? 1.0f : 0.25f; }

constexpr int kMaxPoints = 256;

// A breakpoint of the Points view. The segment to the next point is bent by `bend`
// (-1..1, positive bows the line upwards).
struct EnvPoint
{
    float x = 0.0f; // in steps
    float y = 0.0f; // -1..1
    float bend = 0.0f;
};

constexpr int kMaxCurvePoints = 32;

// One module placed on the grid, covering [start, start + length) steps.
struct Block
{
    BlockType type = BlockType::Envelope;
    int start = 0;
    int length = 1;
    int id = 0;           // stable per lane, so LFO routings survive moves

    float amount = 1.0f;  // amplitude, -1..1
    float cycles = 1.0f;  // LFO cycles, envelope repeats or S&H steps
    float fadeIn = 0.0f;  // fraction of the block
    float fadeOut = 0.0f; // fraction of the block
    int note = -1;        // MIDI note that triggers this block in MIDI Trigger mode, -1 = any
    std::array<float, kNumBlockParams> p {};

    // Steps module only: one value (0..1) per step.
    std::array<float, kMaxSteps> stepValues {};

    // Curve module only: breakpoints over x = 0..1 (wrapping like the Points view).
    std::array<EnvPoint, kMaxCurvePoints> curve {};
    int numCurvePoints = 0;

    // Formula module only: the text and its compiled program.
    std::array<char, formula::kMaxText> formulaText {};
    formula::Program program;

    bool setFormula (const char* text, const char** error = nullptr)
    {
        formula::Program compiled;
        const bool ok = formula::compile (text, compiled, error);
        formulaText = {};
        for (size_t i = 0; i + 1 < formulaText.size() && text[i] != 0; ++i)
            formulaText[i] = text[i];
        program = compiled;
        return ok;
    }

    static Block make (BlockType t, int start, int length)
    {
        Block b;
        b.type = t;
        b.start = start;
        b.length = length;

        const auto& info = getTypeInfo (t);
        b.cycles = info.defaultCycles;
        for (int i = 0; i < kNumBlockParams; ++i)
            b.p[(size_t) i] = i < info.numParams ? info.params[(size_t) i].def : 0.0f;

        if (t == BlockType::Formula)
            b.setFormula ("sin(2*pi*x)");

        if (t == BlockType::Steps)
        {
            constexpr float pattern[] = { 0.9f, 0.2f, 0.55f, 0.35f, 1.0f, 0.1f, 0.7f, 0.45f };
            for (size_t i = 0; i < b.stepValues.size(); ++i)
                b.stepValues[i] = pattern[i % 8];
        }

        if (t == BlockType::Curve)
        {
            b.curve[0] = { 0.0f, 0.0f, 0.4f };
            b.curve[1] = { 0.5f, 1.0f, -0.4f };
            b.numCurvePoints = 2;
        }

        return b;
    }

    // Change the module type but keep placement, amplitude and fades.
    Block withType (BlockType t) const
    {
        auto b = make (t, start, length);
        b.id = id;
        b.amount = amount;
        b.fadeIn = fadeIn;
        b.fadeOut = fadeOut;
        b.note = note;
        return b;
    }
};

//==============================================================================
// Points sorted by x; the last segment wraps around to the first point.
struct PointEnvelope
{
    std::array<EnvPoint, kMaxPoints> points {};
    int numPoints = 0;

    // Inserts keeping the order; returns the new index or -1 when full.
    int insert (EnvPoint p)
    {
        if (numPoints >= kMaxPoints)
            return -1;

        int i = numPoints++;
        while (i > 0 && points[(size_t) i - 1].x > p.x)
        {
            points[(size_t) i] = points[(size_t) i - 1];
            --i;
        }
        points[(size_t) i] = p;
        return i;
    }

    void remove (int index)
    {
        if (index < 0 || index >= numPoints)
            return;

        for (int i = index; i < numPoints - 1; ++i)
            points[(size_t) i] = points[(size_t) i + 1];

        --numPoints;
    }
};

// Fixed-size, allocation-free so it can be copied to the audio thread.
struct LanePattern
{
    std::array<Block, kMaxBlocks> blocks {};
    int numBlocks = 0;
    int nextId = 1;

    PointEnvelope envelope; // used when the lane is in Points view

    int findBlockAt (int step) const
    {
        for (int i = 0; i < numBlocks; ++i)
            if (step >= blocks[(size_t) i].start && step < blocks[(size_t) i].start + blocks[(size_t) i].length)
                return i;

        return -1;
    }

    int findById (int id) const
    {
        for (int i = 0; i < numBlocks; ++i)
            if (blocks[(size_t) i].id == id)
                return i;

        return -1;
    }

    bool isFree (int start, int length, int ignoreIndex = -1) const
    {
        if (start < 0 || length < 1 || start + length > kMaxSteps)
            return false;

        for (int i = 0; i < numBlocks; ++i)
        {
            const auto& b = blocks[(size_t) i];
            if (i != ignoreIndex && start < b.start + b.length && b.start < start + length)
                return false;
        }

        return true;
    }

    // Number of consecutive empty steps starting at `start`, up to maxLength.
    int freeLengthFrom (int start, int maxLength) const
    {
        int n = 0;
        while (n < maxLength && start + n < kMaxSteps && findBlockAt (start + n) < 0)
            ++n;
        return n;
    }

    // Number of consecutive empty steps directly before `start`, up to maxLength.
    int freeLengthBefore (int start, int maxLength) const
    {
        int n = 0;
        while (n < maxLength && start - n - 1 >= 0 && findBlockAt (start - n - 1) < 0)
            ++n;
        return n;
    }

    // A block with id 0 gets a fresh id; a non-zero id (e.g. from saved state) is kept.
    bool add (Block b)
    {
        if (numBlocks >= kMaxBlocks || ! isFree (b.start, b.length))
            return false;

        if (b.id == 0)
            b.id = nextId++;
        else
            nextId = std::max (nextId, b.id + 1);

        blocks[(size_t) numBlocks++] = b;
        return true;
    }

    void remove (int index)
    {
        if (index < 0 || index >= numBlocks)
            return;

        for (int i = index; i < numBlocks - 1; ++i)
            blocks[(size_t) i] = blocks[(size_t) i + 1];

        --numBlocks;
    }
};

//==============================================================================
enum class ModTarget : int
{
    None = 0,
    LaneDepth,
    LaneOffset,
    LaneRate,  // speed multiplier, +-2 octaves at full amount
    BlockAmp,  // amplitude of one block (by id)
    BlockParam // one parameter of one block (by id and ModRoute::param)
};

// ModRoute::param for BlockParam: 0..4 are the module's own parameters, then these.
constexpr int kModCycles = 5, kModFadeIn = 6, kModFadeOut = 7;

// Label of the Cycles control for a module type.
inline const char* cyclesLabel (BlockType t)
{
    switch (t)
    {
        case BlockType::SampleHold: case BlockType::Euclid: case BlockType::Steps: return "Steps";
        case BlockType::Bounce:    return "Bounces";
        case BlockType::Drift:     return "Points";
        case BlockType::Stairs:    return "Levels";
        case BlockType::Chaos:     return "Values";
        case BlockType::Heartbeat: return "Beats";
        default: return getTypeInfo (t).integerCycles ? "Repeats" : "Cycles";
    }
}

inline const char* modParamName (BlockType t, int param)
{
    if (param == kModCycles)  return cyclesLabel (t);
    if (param == kModFadeIn)  return "Fade In";
    if (param == kModFadeOut) return "Fade Out";
    const auto& info = getTypeInfo (t);
    return param >= 0 && param < info.numParams ? info.params[(size_t) param].name : "?";
}

struct ModRoute
{
    ModTarget target = ModTarget::None;
    int lane = 0;
    int blockId = 0;
    float amount = 0.5f;
    int param = 0; // BlockParam only

    bool targetsBlock() const { return target == ModTarget::BlockAmp || target == ModTarget::BlockParam; }

    bool sameTarget (const ModRoute& o) const
    {
        return target == o.target && lane == o.lane
            && (! targetsBlock() || blockId == o.blockId)
            && (target != ModTarget::BlockParam || param == o.param);
    }
};

struct LfoRouting
{
    std::array<ModRoute, kMaxRoutes> routes {};
    int numRoutes = 0;

    int find (const ModRoute& r) const
    {
        for (int i = 0; i < numRoutes; ++i)
            if (routes[(size_t) i].sameTarget (r))
                return i;

        return -1;
    }

    bool add (const ModRoute& r)
    {
        if (numRoutes >= kMaxRoutes || find (r) >= 0)
            return false;

        routes[(size_t) numRoutes++] = r;
        return true;
    }

    void remove (int index)
    {
        if (index < 0 || index >= numRoutes)
            return;

        for (int i = index; i < numRoutes - 1; ++i)
            routes[(size_t) i] = routes[(size_t) i + 1];

        --numRoutes;
    }
};

} // namespace modlanes
