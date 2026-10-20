#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ModuleTypes.h"

#include <atomic>

class ModLanesProcessor : public juce::AudioProcessor,
                          private juce::Timer,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    ModLanesProcessor();
    ~ModLanesProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Message-thread access to the step patterns and LFO routings. The setters
    // return the new version. setPattern also drops routes to deleted blocks.
    modlanes::LanePattern getPattern (int lane) const;
    int setPattern (int lane, const modlanes::LanePattern& pattern);
    int getPatternVersion() const { return patternVersion.load(); }

    modlanes::LfoRouting getRouting (int lfo) const;
    int setRouting (int lfo, const modlanes::LfoRouting& routing);
    int getRoutingVersion() const { return routingVersion.load(); }

    //==============================================================================
    // Scenes: 10 slots, each a full pattern (lanes, LFO routings and all non-global
    // parameters). Changing the "scene" parameter switches, quantised by "quantize".
    static constexpr int kNumScenes = 10;

    int getCurrentScene() const { return currentScene.load(); }
    int getPendingScene() const { return pendingScene.load(); }
    bool isSceneUsed (int scene) const;
    void requestScene (int scene);              // via the parameter, so hosts can record it
    void copyCurrentSceneTo (int scene);
    void clearScene (int scene);
    std::atomic<int> sceneVersion { 0 };        // bumps whenever slots change

    juce::ValueTree captureScene() const;       // current live pattern
    void loadIntoCurrentScene (const juce::ValueTree& scene); // e.g. from a preset
    void setSceneData (int scene, const juce::ValueTree& data);

    juce::AudioProcessorValueTreeState apvts;

    // Written by the audio thread, read by the editor.
    std::atomic<float> lanePosition[modlanes::kNumLanes]; // in steps, -1 when no playhead
    std::atomic<float> laneRaw[modlanes::kNumLanes];      // value before depth/offset, -1..1
    std::atomic<float> laneOutput[modlanes::kNumLanes];   // 0..1
    std::atomic<bool> laneActive[modlanes::kNumLanes];    // currently sending
    std::atomic<float> lfoOutput[modlanes::kNumLfos];     // -1..1
    std::atomic<float> lfoPhase[modlanes::kNumLfos];      // 0..1, including the Phase offset
    std::atomic<float> laneSpeed[modlanes::kNumLanes];    // steps per second, for smooth playheads in the UI
    std::atomic<double> positionTime { 0.0 };             // ms timestamp (hi-res counter) of the positions above
    std::atomic<bool> transportRunning { false };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<double> playPpq { 0.0 };    // song position (host or free-run) in quarter notes
    std::atomic<double> barLength { 4.0 };  // in quarter notes, from the host time signature
    std::atomic<double> beatLength { 1.0 }; // in quarter notes

    // Most recent incoming note, for "learn trigger note" in the editor.
    std::atomic<int> lastNoteNumber { -1 }, lastNoteSerial { 0 };

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& id, float newValue) override;
    void switchToScene (int scene);
    void applySceneData (const juce::ValueTree& scene);

    mutable juce::CriticalSection sceneLock;
    std::array<juce::ValueTree, kNumScenes> scenes;
    std::atomic<int> currentScene { 0 }, pendingScene { 0 };
    std::atomic<int> boundarySerial { 0 };   // bumped by the audio thread at each quantise boundary
    int requestSerial = 0;
    bool waitingForBoundary = false;
    juce::StringArray sceneParamIds;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadDefaultPatterns();
    void pullStateForAudio();
    void startTrigger (int lane, int note, float velocity, int steps);

    struct LaneParams
    {
        std::atomic<float>* on;
        std::atomic<float>* steps;
        std::atomic<float>* rate;
        std::atomic<float>* channel;
        std::atomic<float>* cc;
        std::atomic<float>* depth;
        std::atomic<float>* offset;
        std::atomic<float>* bipolar;
        std::atomic<float>* smooth;
        std::atomic<float>* mode;
        std::atomic<float>* source;
        std::atomic<float>* invert;
        std::atomic<float>* blend;
        std::atomic<float>* velocity;
        std::atomic<float>* threshold;
        std::atomic<float>* attack;
        std::atomic<float>* release;
        std::atomic<float>* sync;
        std::atomic<float>* hz;
        std::atomic<float>* points;
        std::atomic<float>* phase;
        std::atomic<float>* fit;
    };

    // One-shot playback of a block in the trigger modes.
    struct TriggerState
    {
        bool active = false;
        bool wholeLane = false; // Points view: play the whole envelope once
        int blockId = 0;
        double pos = 0.0;       // lane position in steps
        float gain = 1.0f;      // from velocity
        float fadeFrom = 0.0f;  // value at retrigger, crossfaded out over Blend
        double fadeProgress = 1.0;
        float lastValue = 0.0f;
        std::uint32_t roundRobin = 0;
    };

    struct FollowerState
    {
        float env = 0.0f;     // smoothed level, linear
        float fastEnv = 0.0f; // transient detector, linear
        bool armed = true;
        int holdoff = 0;      // samples until the next audio trigger is allowed
    };

    struct NoteEvent
    {
        int sample, note;
        float velocity;
    };

    struct LfoParams
    {
        std::atomic<float>* on;
        std::atomic<float>* shape;
        std::atomic<float>* sync;
        std::atomic<float>* hz;
        std::atomic<float>* div;
        std::atomic<float>* phase;
        std::atomic<float>* skew;
        std::atomic<float>* smooth;
        std::atomic<float>* uni;
    };

    LaneParams laneParams[modlanes::kNumLanes];
    LfoParams lfoParams[modlanes::kNumLfos];
    std::atomic<float>* freeRunParam = nullptr;
    std::atomic<float>* thruParam = nullptr;
    std::atomic<float>* clockParam = nullptr;
    std::atomic<float>* bpmParam = nullptr;
    std::atomic<float>* playParam = nullptr;
    std::atomic<float>* quantizeParam = nullptr;
    std::atomic<float>* masterLengthParam = nullptr;

    // Edited by the UI, guarded by stateLock. The audio thread keeps its own
    // copy and refreshes it with a try-lock when a version changes.
    mutable juce::SpinLock stateLock;
    modlanes::LanePattern patterns[modlanes::kNumLanes];
    modlanes::LfoRouting routings[modlanes::kNumLfos];
    std::atomic<int> patternVersion { 1 }, routingVersion { 1 };

    modlanes::LanePattern audioPatterns[modlanes::kNumLanes];
    modlanes::LfoRouting audioRoutings[modlanes::kNumLfos];
    int audioPatternVersion = 0, audioRoutingVersion = 0;

    // Audio-thread state
    double currentSampleRate = 44100.0;
    double internalPpq = 0.0;
    double lfoFreePhase[modlanes::kNumLfos] {};
    std::uint32_t lfoFreeCycle[modlanes::kNumLfos] {};
    float lfoSmoothed[modlanes::kNumLfos] {};
    double rateWarp[modlanes::kNumLanes] {}; // accumulated step offset from rate modulation
    double freePos[modlanes::kNumLanes] {};  // lane position when not synced, in steps
    TriggerState triggers[modlanes::kNumLanes];

    // Modulated copies of the blocks LFOs point at, rebuilt for each orbit and slice.
    struct ModulatedBlock
    {
        int id = 0;
        float ampMod = 0.0f;
        modlanes::Block block;
    };
    std::array<ModulatedBlock, modlanes::kNumLfos * modlanes::kMaxRoutes> modulatedBlocks;
    FollowerState followers[modlanes::kNumLanes];
    std::array<NoteEvent, 256> notes {};
    int numNotes = 0;
    float smoothed[modlanes::kNumLanes] {};
    int lastValue[modlanes::kNumLanes] {};
    int lastController[modlanes::kNumLanes] {};
    int lastChannel[modlanes::kNumLanes] {};
    bool wasRunning = false;
    bool internalWasPlaying = false;
    double lastMasterIndex = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModLanesProcessor)
};
