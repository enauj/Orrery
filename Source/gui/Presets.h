#pragma once

#include "Params.h"
#include "PluginProcessor.h"
#include "StateIO.h"

// A preset is one scene (lanes, routings and scene parameters) stored as XML in
// Documents/ModLanes/Presets. A few factory presets are built in code.
namespace modlanes::presets
{

inline const juce::String extension { ".mlpreset" };

inline juce::File folder()
{
    // Documents/Orrery (the plugin was called ModLanes before; its folder is moved over once).
    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    if (const auto old = documents.getChildFile ("ModLanes"); old.isDirectory() && ! documents.getChildFile ("Orrery").exists())
        old.moveFileTo (documents.getChildFile ("Orrery"));
    auto dir = documents.getChildFile ("Orrery").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

inline juce::Array<juce::File> userPresets()
{
    auto files = folder().findChildFiles (juce::File::findFiles, false, "*" + extension);
    files.sort();
    return files;
}

inline bool save (const juce::ValueTree& scene, const juce::String& name)
{
    const auto file = folder().getChildFile (juce::File::createLegalFileName (name.trim()) + extension);
    if (auto xml = scene.createXml())
        return xml->writeTo (file);
    return false;
}

inline juce::ValueTree load (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
        if (auto tree = juce::ValueTree::fromXml (*xml); tree.hasType (stateio::sceneId))
            return tree;
    return {};
}

//==============================================================================
// Builds a scene from lane patterns and a few parameter values (real units);
// every other parameter falls back to its default when the scene is loaded.
class SceneBuilder
{
public:
    explicit SceneBuilder (ModLanesProcessor& p) : proc (p) {}

    LanePattern& lane (int i) { return lanes[(size_t) i]; }

    SceneBuilder& set (const juce::String& id, float value)
    {
        values.set (id, value);
        return *this;
    }

    SceneBuilder& route (int lfo, ModRoute r)
    {
        routings[(size_t) lfo].add (r);
        return *this;
    }

    juce::ValueTree build() const
    {
        juce::ValueTree scene (stateio::sceneId);
        scene.setProperty (stateio::versionId, stateio::kVersion, nullptr);

        juce::ValueTree patternsTree (stateio::patternsId);
        for (int i = 0; i < kNumLanes; ++i)
            patternsTree.appendChild (stateio::laneToTree (lanes[(size_t) i], i), nullptr);
        scene.appendChild (patternsTree, nullptr);

        juce::ValueTree routingsTree (stateio::routingsId);
        for (int j = 0; j < kNumLfos; ++j)
            routingsTree.appendChild (stateio::routingToTree (routings[(size_t) j], j), nullptr);
        scene.appendChild (routingsTree, nullptr);

        juce::ValueTree paramsTree (stateio::paramsId);
        for (const auto& entry : values)
            if (auto* param = proc.apvts.getParameter (entry.name.toString()))
            {
                juce::ValueTree p (stateio::paramId);
                p.setProperty ("id", entry.name.toString(), nullptr);
                p.setProperty ("value", param->convertTo0to1 ((float) entry.value), nullptr);
                paramsTree.appendChild (p, nullptr);
            }
        scene.appendChild (paramsTree, nullptr);
        return scene;
    }

private:
    ModLanesProcessor& proc;
    std::array<LanePattern, kNumLanes> lanes {};
    std::array<LfoRouting, kNumLfos> routings {};
    juce::NamedValueSet values;
};

inline int rateIndex (const char* name) { return params::rateNames.indexOf (name); }

inline std::vector<std::pair<juce::String, juce::ValueTree>> factory (ModLanesProcessor& proc)
{
    std::vector<std::pair<juce::String, juce::ValueTree>> list;
    const auto off = [] (SceneBuilder& b, int lane) { b.set (params::lane (lane, "on"), 0.0f); };

    {
        SceneBuilder b (proc);
        list.push_back ({ "Init", b.build() });
    }

    {
        // Sidechain-style ducking on every beat: inverted envelope pulled up by Offset.
        SceneBuilder b (proc);
        for (int s = 0; s < 16; s += 4)
        {
            auto env = Block::make (BlockType::InvEnvelope, s, 4);
            env.p = { 0.0f, 0.7f, 0.0f, 0.0f, -0.4f };
            b.lane (0).add (env);
        }
        b.set (params::lane (0, "offset"), 1.0f).set (params::lane (0, "blend"), 0.0f);
        off (b, 1);
        off (b, 2);
        list.push_back ({ "Quarter Pump", b.build() });
    }

    {
        SceneBuilder b (proc);
        auto sine = Block::make (BlockType::Sine, 0, 16);
        sine.cycles = 2.0f;
        sine.fadeIn = 0.25f;
        sine.p[2] = 1.0f;
        b.lane (0).add (sine);

        auto tri = Block::make (BlockType::Triangle, 0, 16);
        tri.p[2] = 1.0f;
        b.lane (1).add (tri);

        auto wt = Block::make (BlockType::Wavetable, 0, 16);
        wt.p = { 4.0f, 0.6f, 0.0f, 1.0f, 0.0f };
        wt.cycles = 4.0f;
        b.lane (2).add (wt);

        b.set (params::lane (0, "rate"), (float) rateIndex ("1/8"))
         .set (params::lane (1, "rate"), (float) rateIndex ("1/4"))
         .set (params::lane (2, "rate"), (float) rateIndex ("1/8"))
         .route (0, { ModTarget::LaneDepth, 2, 0, 0.4f });
        list.push_back ({ "Slow Wobble", b.build() });
    }

    {
        SceneBuilder b (proc);
        const struct { float steps, smooth, seed; } lanes[] = { { 16.0f, 0.0f, 7.0f }, { 8.0f, 0.6f, 21.0f }, { 4.0f, 1.0f, 3.0f } };
        for (int i = 0; i < kNumLanes; ++i)
        {
            auto sh = Block::make (BlockType::SampleHold, 0, 16);
            sh.cycles = lanes[i].steps;
            sh.p = { lanes[i].smooth, lanes[i].seed, 1.0f, 0.0f, 0.0f };
            b.lane (i).add (sh);
        }
        list.push_back ({ "Random Steps", b.build() });
    }

    {
        // Rising ramps on a triplet grid against straight decays.
        SceneBuilder b (proc);
        for (int s = 0; s < 12; s += 3)
        {
            auto ramp = Block::make (BlockType::Ramp, s, 3);
            ramp.p = { 0.0f, 0.9f, 0.0f, 0.0f, 0.0f };
            b.lane (0).add (ramp);
        }
        for (int s = 0; s < 16; s += 2)
        {
            auto env = Block::make (BlockType::Envelope, s, 2);
            env.p = { 0.0f, 0.4f, 0.2f, 0.2f, 0.5f };
            env.amount = s % 4 == 0 ? 1.0f : 0.5f;
            b.lane (1).add (env);
        }
        b.set (params::lane (0, "steps"), 12.0f)
         .set (params::lane (0, "rate"), (float) rateIndex ("1/8T"));
        off (b, 2);
        list.push_back ({ "Triplet Ramps", b.build() });
    }

    return list;
}

} // namespace modlanes::presets
