#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "ModuleTypes.h"

// Converting lane patterns and LFO routings to and from ValueTrees. Used for the
// plugin state, the scene slots and preset files.
namespace modlanes::stateio
{

inline const juce::Identifier patternsId { "PATTERNS" }, laneId { "LANE" }, blockId { "BLOCK" }, pointId { "POINT" };
inline const juce::Identifier routingsId { "ROUTINGS" }, lfoId { "LFO" }, routeId { "ROUTE" };
inline const juce::Identifier sceneId { "SCENE" }, paramsId { "PARAMS" }, paramId { "P" };
inline const juce::Identifier versionId { "modlanesVersion" };
constexpr int kVersion = 3;

// Version 1 stored cycles as the first shape parameter of LFO-style blocks.
inline void migrateV1Block (Block& b, const std::array<float, kNumBlockParams>& old)
{
    switch (b.type)
    {
        case BlockType::Sine: case BlockType::Triangle: case BlockType::SawUp:
        case BlockType::SawDown: case BlockType::Square: case BlockType::SampleHold:
            b.cycles = old[0];
            b.p = { old[1], old[2], old[3], 0.0f, 0.0f };
            break;

        case BlockType::Wavetable:
            b.cycles = old[2];
            b.p = { old[0], old[1], old[3], old[4], 0.0f };
            break;

        default:
            break;
    }
}

inline juce::ValueTree blockToTree (const Block& block)
{
    juce::ValueTree t (blockId);
    t.setProperty ("type", (int) block.type, nullptr);
    t.setProperty ("start", block.start, nullptr);
    t.setProperty ("length", block.length, nullptr);
    t.setProperty ("id", block.id, nullptr);
    t.setProperty ("amount", block.amount, nullptr);
    t.setProperty ("cycles", block.cycles, nullptr);
    t.setProperty ("fadeIn", block.fadeIn, nullptr);
    t.setProperty ("fadeOut", block.fadeOut, nullptr);
    t.setProperty ("note", block.note, nullptr);
    for (int k = 0; k < kNumBlockParams; ++k)
        t.setProperty ("p" + juce::String (k), block.p[(size_t) k], nullptr);

    if (block.type == BlockType::Formula)
        t.setProperty ("formula", juce::String (block.formulaText.data()), nullptr);

    if (block.type == BlockType::Steps)
    {
        juce::StringArray values;
        for (float v : block.stepValues)
            values.add (juce::String (v, 4));
        t.setProperty ("steps", values.joinIntoString (","), nullptr);
    }

    if (block.type == BlockType::Curve)
    {
        juce::StringArray points;
        for (int i = 0; i < block.numCurvePoints; ++i)
        {
            const auto& c = block.curve[(size_t) i];
            points.add (juce::String (c.x, 5) + " " + juce::String (c.y, 5) + " " + juce::String (c.bend, 3));
        }
        t.setProperty ("curve", points.joinIntoString (";"), nullptr);
    }

    return t;
}

inline Block blockFromTree (const juce::ValueTree& t, int version)
{
    const int type = juce::jlimit (0, kNumBlockTypes - 1, (int) t.getProperty ("type", 0));

    Block b = Block::make ((BlockType) type, t.getProperty ("start", 0), t.getProperty ("length", 1));
    b.id = t.getProperty ("id", 0);
    b.amount = t.getProperty ("amount", 1.0f);
    b.cycles = t.getProperty ("cycles", b.cycles);
    b.fadeIn = t.getProperty ("fadeIn", 0.0f);
    b.fadeOut = t.getProperty ("fadeOut", 0.0f);
    b.note = juce::jlimit (-1, 127, (int) t.getProperty ("note", -1));

    std::array<float, kNumBlockParams> stored {};
    for (int k = 0; k < kNumBlockParams; ++k)
        stored[(size_t) k] = t.getProperty ("p" + juce::String (k), b.p[(size_t) k]);

    b.p = stored;
    if (version < 2)
        migrateV1Block (b, stored);

    if (b.type == BlockType::Formula)
        b.setFormula (t.getProperty ("formula", "sin(2*pi*x)").toString().substring (0, formula::kMaxText - 1).toRawUTF8());

    if (b.type == BlockType::Steps && t.hasProperty ("steps"))
    {
        const auto values = juce::StringArray::fromTokens (t.getProperty ("steps").toString(), ",", "");
        for (int i = 0; i < juce::jmin (values.size(), kMaxSteps); ++i)
            b.stepValues[(size_t) i] = juce::jlimit (0.0f, 1.0f, values[i].getFloatValue());
    }

    if (b.type == BlockType::Curve && t.hasProperty ("curve"))
    {
        b.numCurvePoints = 0;
        for (const auto& entry : juce::StringArray::fromTokens (t.getProperty ("curve").toString(), ";", ""))
        {
            const auto parts = juce::StringArray::fromTokens (entry, " ", "");
            if (parts.size() >= 3 && b.numCurvePoints < kMaxCurvePoints)
                b.curve[(size_t) b.numCurvePoints++] = { juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue()),
                                                         juce::jlimit (-1.0f, 1.0f, parts[1].getFloatValue()),
                                                         juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) };
        }
    }

    return b;
}

inline juce::ValueTree laneToTree (const LanePattern& pattern, int lane)
{
    juce::ValueTree laneTree (laneId);
    laneTree.setProperty ("index", lane, nullptr);

    for (int b = 0; b < pattern.numBlocks; ++b)
        laneTree.appendChild (blockToTree (pattern.blocks[(size_t) b]), nullptr);

    for (int k = 0; k < pattern.envelope.numPoints; ++k)
    {
        const auto& point = pattern.envelope.points[(size_t) k];
        juce::ValueTree t (pointId);
        t.setProperty ("x", point.x, nullptr);
        t.setProperty ("y", point.y, nullptr);
        t.setProperty ("bend", point.bend, nullptr);
        laneTree.appendChild (t, nullptr);
    }

    return laneTree;
}

inline LanePattern laneFromTree (const juce::ValueTree& laneTree, int version)
{
    LanePattern pattern;

    for (const auto& t : laneTree)
    {
        if (t.hasType (pointId))
            pattern.envelope.insert ({ juce::jlimit (0.0f, (float) kMaxSteps, (float) t.getProperty ("x", 0.0f)),
                                       juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("y", 0.0f)),
                                       juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("bend", 0.0f)) });
        else if (t.hasType (blockId))
            pattern.add (blockFromTree (t, version));
    }

    return pattern;
}

inline juce::ValueTree routingToTree (const LfoRouting& routing, int lfo)
{
    juce::ValueTree lfoTree (lfoId);
    lfoTree.setProperty ("index", lfo, nullptr);

    for (int r = 0; r < routing.numRoutes; ++r)
    {
        const auto& route = routing.routes[(size_t) r];
        juce::ValueTree t (routeId);
        t.setProperty ("target", (int) route.target, nullptr);
        t.setProperty ("lane", route.lane, nullptr);
        t.setProperty ("block", route.blockId, nullptr);
        t.setProperty ("amount", route.amount, nullptr);
        t.setProperty ("param", route.param, nullptr);
        lfoTree.appendChild (t, nullptr);
    }

    return lfoTree;
}

inline LfoRouting routingFromTree (const juce::ValueTree& lfoTree)
{
    LfoRouting routing;
    for (const auto& t : lfoTree)
    {
        ModRoute route;
        route.target = (ModTarget) juce::jlimit (0, (int) ModTarget::BlockParam, (int) t.getProperty ("target", 0));
        route.param = juce::jlimit (0, kModFadeOut, (int) t.getProperty ("param", 0));
        route.lane = juce::jlimit (0, kNumLanes - 1, (int) t.getProperty ("lane", 0));
        route.blockId = t.getProperty ("block", 0);
        route.amount = t.getProperty ("amount", 0.5f);
        if (route.target != ModTarget::None)
            routing.add (route);
    }
    return routing;
}

} // namespace modlanes::stateio
