#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"
#include "Shapes.h"
#include "StateIO.h"

using namespace modlanes;

namespace
{
using namespace modlanes::stateio;

// Version 3 reversed the synced rate lists (slow -> fast). Flip saved indices.
void migrateRateOrder (juce::ValueTree& state)
{
    for (auto param : state)
    {
        const juce::String id = param.getProperty ("id");
        const int count = id.endsWith ("_rate") ? params::rateNames.size()
                        : id.endsWith ("_div")  ? params::lfoDivNames.size() : 0;
        if (count > 0)
            param.setProperty ("value", (count - 1) - (int) param.getProperty ("value", 0), nullptr);
    }
}

double positiveMod (double x, double m)
{
    const double r = std::fmod (x, m);
    return r < 0.0 ? r + m : r;
}

float ease (double u)
{
    const float x = juce::jlimit (0.0f, 1.0f, (float) u);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

ModLanesProcessor::ModLanesProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ModLanes", createLayout())
{
    for (int i = 0; i < kNumLanes; ++i)
    {
        const auto p = [&] (const char* name) { return apvts.getRawParameterValue (params::lane (i, name)); };
        laneParams[i] = { p ("on"), p ("steps"), p ("rate"), p ("ch"), p ("cc"), p ("depth"), p ("offset"), p ("bipolar"), p ("smooth"),
                          p ("mode"), p ("source"), p ("invert"), p ("blend"), p ("vel"), p ("thresh"), p ("attack"), p ("release"),
                          p ("sync"), p ("hz"), p ("points"), p ("phase"), p ("fit") };
        laneSpeed[i].store (0.0f);

        lanePosition[i].store (-1.0f);
        laneRaw[i].store (0.0f);
        laneOutput[i].store (0.0f);
        laneActive[i].store (false);
        lastValue[i] = lastController[i] = lastChannel[i] = -1;
    }

    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto p = [&] (const char* name) { return apvts.getRawParameterValue (params::lfo (i, name)); };
        lfoParams[i] = { p ("on"), p ("shape"), p ("sync"), p ("hz"), p ("div"), p ("phase"), p ("skew"), p ("smooth"), p ("uni") };
        lfoOutput[i].store (0.0f);
        lfoPhase[i].store (0.0f);
    }

    freeRunParam = apvts.getRawParameterValue (params::freeRun);
    thruParam = apvts.getRawParameterValue (params::thru);
    clockParam = apvts.getRawParameterValue (params::clock);
    bpmParam = apvts.getRawParameterValue (params::bpm);
    playParam = apvts.getRawParameterValue (params::play);
    quantizeParam = apvts.getRawParameterValue (params::quantize);
    masterLengthParam = apvts.getRawParameterValue (params::masterLength);

    // Everything except the global transport settings belongs to a scene.
    for (auto* param : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            if (! params::isGlobal (withId->getParameterID()))
                sceneParamIds.add (withId->getParameterID());

    juce::ignoreUnused (wavetables());
    loadDefaultPatterns();
    scenes[0] = captureScene();

    apvts.addParameterListener (params::scene, this);
    startTimerHz (60);
}

ModLanesProcessor::~ModLanesProcessor()
{
    stopTimer();
    apvts.removeParameterListener (params::scene, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout ModLanesProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumLanes; ++i)
    {
        const auto id = [i] (const char* name) { return ParameterID { params::lane (i, name), 1 }; };
        const String prefix = "Orbit " + String (i + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool> (id ("on"), prefix + "On", true));
        layout.add (std::make_unique<AudioParameterInt> (id ("steps"), prefix + "Steps", 1, kMaxSteps, 16));
        layout.add (std::make_unique<AudioParameterChoice> (id ("rate"), prefix + "Rate", params::rateNames, params::kDefaultRate));
        layout.add (std::make_unique<AudioParameterInt> (id ("ch"), prefix + "MIDI Channel", 1, 16, 1));
        layout.add (std::make_unique<AudioParameterInt> (id ("cc"), prefix + "CC", 0, 127, 20 + i));
        layout.add (std::make_unique<AudioParameterFloat> (id ("depth"), prefix + "Depth", NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (id ("offset"), prefix + "Offset", NormalisableRange<float> (-1.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterBool> (id ("bipolar"), prefix + "Bipolar", i == 2));
        layout.add (std::make_unique<AudioParameterFloat> (id ("smooth"), prefix + "Smooth",
                                                           NormalisableRange<float> (0.0f, 250.0f, 0.0f, 0.4f), 5.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterChoice> (id ("mode"), prefix + "Mode", params::modeNames, params::Sequence));
        layout.add (std::make_unique<AudioParameterChoice> (id ("source"), prefix + "Audio Source", params::sourceNames, 0));
        layout.add (std::make_unique<AudioParameterBool> (id ("invert"), prefix + "Invert", false));
        layout.add (std::make_unique<AudioParameterFloat> (id ("blend"), prefix + "Blend",
                                                           NormalisableRange<float> (0.0f, 4.0f, 0.0f, 0.6f), 0.5f,
                                                           AudioParameterFloatAttributes().withLabel ("steps")));
        layout.add (std::make_unique<AudioParameterFloat> (id ("vel"), prefix + "Velocity", NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<AudioParameterFloat> (id ("thresh"), prefix + "Threshold",
                                                           NormalisableRange<float> (-60.0f, 0.0f), -30.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterFloat> (id ("attack"), prefix + "Attack",
                                                           NormalisableRange<float> (0.1f, 200.0f, 0.0f, 0.35f), 5.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterFloat> (id ("release"), prefix + "Release",
                                                           NormalisableRange<float> (5.0f, 2000.0f, 0.0f, 0.35f), 150.0f,
                                                           AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<AudioParameterBool> (id ("sync"), prefix + "Sync", true));
        layout.add (std::make_unique<AudioParameterFloat> (id ("hz"), prefix + "Free Rate",
                                                           NormalisableRange<float> (0.05f, 64.0f, 0.0f, 0.3f), 4.0f,
                                                           AudioParameterFloatAttributes().withLabel ("steps/s")));
        layout.add (std::make_unique<AudioParameterBool> (id ("points"), prefix + "Points View", false));
        layout.add (std::make_unique<AudioParameterFloat> (id ("phase"), prefix + "Phase", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterChoice> (id ("fit"), prefix + "Fit", params::fitNames, 0));
    }

    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto id = [i] (const char* name) { return ParameterID { params::lfo (i, name), 1 }; };
        const String prefix = "LFO " + String (i + 1) + " ";

        layout.add (std::make_unique<AudioParameterBool> (id ("on"), prefix + "On", true));
        layout.add (std::make_unique<AudioParameterChoice> (id ("shape"), prefix + "Shape", params::lfoShapeNames, 0));
        layout.add (std::make_unique<AudioParameterBool> (id ("sync"), prefix + "Sync", true));
        layout.add (std::make_unique<AudioParameterFloat> (id ("hz"), prefix + "Rate",
                                                           NormalisableRange<float> (0.01f, 20.0f, 0.0f, 0.3f), 0.5f,
                                                           AudioParameterFloatAttributes().withLabel ("Hz")));
        layout.add (std::make_unique<AudioParameterChoice> (id ("div"), prefix + "Division", params::lfoDivNames, params::kDefaultLfoDiv));
        layout.add (std::make_unique<AudioParameterFloat> (id ("phase"), prefix + "Phase", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (id ("skew"), prefix + "Skew", NormalisableRange<float> (0.02f, 0.98f), 0.5f));
        layout.add (std::make_unique<AudioParameterFloat> (id ("smooth"), prefix + "Smooth", NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterBool> (id ("uni"), prefix + "Unipolar", false));
    }

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { params::freeRun, 1 }, "Run When Stopped", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { params::thru, 1 }, "MIDI Thru", true));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { params::clock, 1 }, "Clock", params::clockNames, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { params::bpm, 1 }, "Internal BPM",
                                                       NormalisableRange<float> (30.0f, 300.0f, 0.1f), 120.0f,
                                                       AudioParameterFloatAttributes().withLabel ("BPM")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { params::play, 1 }, "Internal Play", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { params::scene, 1 }, "Scene", 1, kNumScenes, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { params::quantize, 1 }, "Scene Quantize", params::quantizeNames, 1));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { params::masterLength, 1 }, "Alignment", 0, 16, 0,
                                                     AudioParameterIntAttributes().withStringFromValueFunction (
                                                         [] (int v, int) { return v == 0 ? String ("Off") : String (v) + (v == 1 ? " bar" : " bars"); })));

    return layout;
}

void ModLanesProcessor::loadDefaultPatterns()
{
    LanePattern p[kNumLanes];

    for (int s = 0; s < 16; s += 4)
        p[0].add (Block::make (BlockType::Envelope, s, 4));
    p[0].blocks[1].p[1] = 0.1f; // shorter decay on the second hit
    p[0].blocks[3].cycles = 2.0f; // double hit at the end

    auto sine = Block::make (BlockType::Sine, 0, 8);
    sine.cycles = 4.0f;
    sine.fadeIn = 0.6f;
    sine.p[2] = 1.0f; // unipolar
    p[1].add (sine);
    auto tri = Block::make (BlockType::Triangle, 8, 8);
    tri.cycles = 2.0f;
    tri.fadeOut = 0.5f;
    tri.p[2] = 1.0f;
    p[1].add (tri);

    p[2].add (Block::make (BlockType::InvEnvelope, 0, 4));
    p[2].add (Block::make (BlockType::Ramp, 4, 4));
    auto sh = Block::make (BlockType::SampleHold, 8, 8);
    sh.cycles = 8.0f;
    p[2].add (sh);

    for (int i = 0; i < kNumLanes; ++i)
        setPattern (i, p[i]);
}

//==============================================================================
LanePattern ModLanesProcessor::getPattern (int lane) const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return patterns[lane];
}

int ModLanesProcessor::setPattern (int lane, const LanePattern& pattern)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    patterns[lane] = pattern;

    bool routingChanged = false;
    for (auto& routing : routings)
        for (int r = routing.numRoutes; --r >= 0;)
        {
            const auto& route = routing.routes[(size_t) r];
            if (route.targetsBlock() && route.lane == lane && pattern.findById (route.blockId) < 0)
            {
                routing.remove (r);
                routingChanged = true;
            }
        }

    if (routingChanged)
        ++routingVersion;

    return ++patternVersion;
}

LfoRouting ModLanesProcessor::getRouting (int lfo) const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return routings[lfo];
}

int ModLanesProcessor::setRouting (int lfo, const LfoRouting& routing)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    routings[lfo] = routing;
    return ++routingVersion;
}

void ModLanesProcessor::pullStateForAudio()
{
    const int pv = patternVersion.load(), rv = routingVersion.load();
    if (pv == audioPatternVersion && rv == audioRoutingVersion)
        return;

    const juce::SpinLock::ScopedTryLockType lock (stateLock);
    if (! lock.isLocked())
        return; // try again next block

    for (int i = 0; i < kNumLanes; ++i)
        audioPatterns[i] = patterns[i];
    for (int i = 0; i < kNumLfos; ++i)
        audioRoutings[i] = routings[i];

    audioPatternVersion = pv;
    audioRoutingVersion = rv;
}

//==============================================================================
bool ModLanesProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != layouts.getMainInputChannelSet() || (out != mono && out != stereo))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);
        if (! sidechain.isDisabled() && sidechain != mono && sidechain != stereo)
            return false;
    }

    return true;
}

void ModLanesProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    wasRunning = false;

    for (int i = 0; i < kNumLanes; ++i)
    {
        lastValue[i] = -1;
        triggers[i] = {};
        followers[i] = {};
    }
}

// Notes mapped to a block trigger that block; other notes (and audio hits) step
// round-robin through the eligible blocks in grid order.
void ModLanesProcessor::startTrigger (int lane, int note, float velocity, int steps)
{
    const auto& pattern = audioPatterns[lane];
    auto& trigger = triggers[lane];
    const float velocityAmount = laneParams[lane].velocity->load();
    const float gain = 1.0f - velocityAmount * (1.0f - juce::jlimit (0.0f, 1.0f, velocity));

    // In Points view every trigger plays the whole envelope once.
    if (laneParams[lane].points->load() >= 0.5f)
    {
        if (pattern.envelope.numPoints == 0)
            return;

        trigger.fadeFrom = trigger.lastValue;
        trigger.fadeProgress = 0.0;
        trigger.active = true;
        trigger.wholeLane = true;
        trigger.pos = 0.0;
        trigger.gain = gain;
        return;
    }

    int chosen = -1;

    if (note >= 0)
        for (int b = 0; b < pattern.numBlocks && chosen < 0; ++b)
            if (pattern.blocks[(size_t) b].note == note && pattern.blocks[(size_t) b].start < steps)
                chosen = b;

    if (chosen < 0)
    {
        std::array<int, kMaxBlocks> eligible {};
        int count = 0;
        for (int b = 0; b < pattern.numBlocks; ++b)
        {
            const auto& block = pattern.blocks[(size_t) b];
            if (block.start < steps && (note < 0 || block.note < 0))
            {
                int k = count++;
                while (k > 0 && pattern.blocks[(size_t) eligible[(size_t) k - 1]].start > block.start)
                {
                    eligible[(size_t) k] = eligible[(size_t) k - 1];
                    --k;
                }
                eligible[(size_t) k] = b;
            }
        }

        if (count == 0)
            return;

        chosen = eligible[(size_t) (trigger.roundRobin++ % (std::uint32_t) count)];
    }

    const auto& block = pattern.blocks[(size_t) chosen];

    trigger.fadeFrom = trigger.lastValue;
    trigger.fadeProgress = 0.0;
    trigger.active = true;
    trigger.wholeLane = false;
    trigger.blockId = block.id;
    trigger.pos = block.start;
    trigger.gain = gain;
}

void ModLanesProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Audio passes through untouched; only outputs without a matching input are cleared.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();

    // Collect note-ons before MIDI thru may clear the buffer.
    numNotes = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn() && numNotes < (int) notes.size())
        {
            notes[(size_t) numNotes++] = { metadata.samplePosition, message.getNoteNumber(), message.getFloatVelocity() };
            lastNoteNumber.store (message.getNoteNumber());
            ++lastNoteSerial;
        }
    }

    if (thruParam->load() < 0.5f)
        midi.clear();

    pullStateForAudio();

    const bool internalClock = clockParam->load() >= 0.5f;
    double bpm = 120.0;
    bool hostPlaying = false;
    juce::Optional<double> hostPpq;

    if (auto* hostPlayHead = getPlayHead())
        if (auto pos = hostPlayHead->getPosition())
        {
            if (auto b = pos->getBpm())
                bpm = *b;
            hostPlaying = pos->getIsPlaying();
            hostPpq = pos->getPpqPosition();

            if (auto sig = pos->getTimeSignature(); sig.hasValue() && sig->numerator > 0 && sig->denominator > 0)
            {
                beatLength.store (4.0 / sig->denominator);
                barLength.store (sig->numerator * 4.0 / sig->denominator);
            }
        }

    if (internalClock)
        bpm = bpmParam->load();

    hostBpm.store (bpm);
    const double ppqPerSample = bpm / 60.0 / currentSampleRate;

    bool running = false;
    double ppqStart = internalPpq;

    if (internalClock)
    {
        // Own tempo and start/stop; starting always begins at bar 1.
        const bool play = playParam->load() >= 0.5f;
        if (play && ! internalWasPlaying)
            ppqStart = internalPpq = 0.0;
        internalWasPlaying = play;
        running = play;
    }
    else if (hostPlaying && hostPpq.hasValue())
    {
        running = true;
        ppqStart = *hostPpq;
    }
    else if (freeRunParam->load() >= 0.5f)
    {
        running = true;
    }

    if (! internalClock)
        internalWasPlaying = false;

    internalPpq = ppqStart + numSamples * ppqPerSample;
    transportRunning.store (running);
    playPpq.store (ppqStart);
    positionTime.store (juce::Time::getMillisecondCounterHiRes());

    // Sequence lanes follow the transport; trigger and follower lanes always run.
    const bool justStarted = running && ! wasRunning;
    wasRunning = running;

    if (justStarted)
        for (auto& w : rateWarp)
            w = 0.0;

    // Master length: all lanes restart together every N bars.
    const int masterBars = (int) masterLengthParam->load();
    const double masterBeats = masterBars > 0 ? masterBars * barLength.load() : 0.0;
    bool masterRestart = false;
    if (running && masterBeats > 0.0)
    {
        const double index = std::floor (ppqStart / masterBeats);
        masterRestart = index != lastMasterIndex && ! justStarted;
        lastMasterIndex = index;
    }

    // At the master length everything starts over: free LFOs and rate-modulated lanes too.
    if (masterRestart || justStarted)
    {
        for (int j = 0; j < kNumLfos; ++j)
        {
            lfoFreePhase[j] = 0.0;
            lfoFreeCycle[j] = 0;
        }
        for (auto& w : rateWarp)
            w = 0.0;
    }

    // Scene changes wait for the next bar or master-length boundary (see timerCallback).
    if (running)
    {
        const int quantize = (int) quantizeParam->load();
        const double length = quantize == 2 && masterBeats > 0.0 ? masterBeats : barLength.load();
        if (std::floor ((ppqStart + numSamples * ppqPerSample) / length) != std::floor (ppqStart / length) || justStarted)
            ++boundarySerial;
    }

    const auto mainIn = getBusBuffer (buffer, true, 0);
    const bool hasSidechain = getBusCount (true) > 1 && getBus (true, 1)->isEnabled()
                           && getBus (true, 1)->getNumberOfChannels() > 0;
    const auto sidechain = hasSidechain ? getBusBuffer (buffer, true, 1) : mainIn;

    const auto levelAt = [] (const juce::AudioBuffer<float>& b, int n)
    {
        float level = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            level = juce::jmax (level, std::abs (b.getSample (ch, n)));
        return level;
    };

    const auto coefficient = [this] (float ms) { return 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) currentSampleRate)); };

    // Evaluate at roughly 1 ms resolution; a CC is only sent when its 7-bit value changes.
    const int chunk = juce::jmax (16, (int) (currentSampleRate * 0.001));

    for (int s = 0; s < numSamples; s += chunk)
    {
        const double ppq = ppqStart + s * ppqPerSample;
        const int chunkSamples = juce::jmin (chunk, numSamples - s);
        const double chunkSeconds = chunkSamples / currentSampleRate;
        const double chunkPpq = chunkSamples * ppqPerSample;

        float lfoValue[kNumLfos] {};

        for (int j = 0; j < kNumLfos; ++j)
        {
            const auto& lp = lfoParams[j];
            float phase;
            std::uint32_t cycle;

            if (lp.sync->load() >= 0.5f)
            {
                const double lfoPpq = masterBeats > 0.0 ? positiveMod (ppq, masterBeats) : ppq;
                const double cycles = lfoPpq / params::kLfoDivBeats[(int) lp.div->load()];
                phase = frac (cycles);
                cycle = (std::uint32_t) (std::int64_t) std::floor (cycles);
            }
            else
            {
                phase = (float) lfoFreePhase[j];
                cycle = lfoFreeCycle[j];
                lfoFreePhase[j] += lp.hz->load() * chunkSeconds;
                if (lfoFreePhase[j] >= 1.0)
                {
                    lfoFreePhase[j] -= std::floor (lfoFreePhase[j]);
                    ++lfoFreeCycle[j];
                }
            }

            // Phase offset (wrapping into the next cycle where needed for the random shapes).
            const float shifted = phase + lp.phase->load();
            if (shifted >= 1.0f)
                ++cycle;
            phase = shifted - std::floor (shifted);

            float value = 0.0f;
            if (lp.on->load() >= 0.5f)
            {
                value = evalGlobalLfo ((int) lp.shape->load(), phase, cycle, lp.skew->load());
                if (lp.uni->load() >= 0.5f)
                    value = 0.5f * (value + 1.0f);

                // Smooth: slew up to ~250 ms.
                if (const float smooth = lp.smooth->load(); smooth > 0.0f)
                    value = lfoSmoothed[j] += (float) (1.0 - std::exp (-chunkSeconds / (smooth * 0.25))) * (value - lfoSmoothed[j]);
                else
                    lfoSmoothed[j] = value;
            }
            lfoValue[j] = value;

            if (s == 0)
            {
                lfoOutput[j].store (lfoValue[j]);
                lfoPhase[j].store (phase);
            }
        }

        float depthMod[kNumLanes] {}, offsetMod[kNumLanes] {}, rateMod[kNumLanes] {};
        bool rateRouted[kNumLanes] {};

        for (int j = 0; j < kNumLfos; ++j)
            for (int r = 0; r < audioRoutings[j].numRoutes; ++r)
            {
                const auto& route = audioRoutings[j].routes[(size_t) r];
                const float m = route.amount * lfoValue[j];

                switch (route.target)
                {
                    case ModTarget::LaneDepth:  depthMod[route.lane] += m; break;
                    case ModTarget::LaneOffset: offsetMod[route.lane] += m; break;
                    case ModTarget::LaneRate:   rateMod[route.lane] += m; rateRouted[route.lane] = true; break;
                    case ModTarget::BlockAmp:
                    case ModTarget::None:       break;
                }
            }

        for (int i = 0; i < kNumLanes; ++i)
        {
            const auto& lp = laneParams[i];
            const int mode = (int) lp.mode->load();

            if (lp.on->load() < 0.5f || (mode == params::Sequence && ! running))
            {
                lastValue[i] = -1;
                triggers[i].active = false;
                if (s == 0)
                {
                    lanePosition[i].store (-1.0f);
                    laneActive[i].store (false);
                }
                continue;
            }

            const int steps = juce::jlimit (1, kMaxSteps, (int) lp.steps->load());
            const bool synced = lp.sync->load() >= 0.5f;
            const bool pointsView = lp.points->load() >= 0.5f;
            const double stepBeats = params::stepBeats ((int) lp.rate->load(), (int) lp.fit->load(), steps, barLength.load());
            const double phaseSteps = lp.phase->load() * steps;
            const double stepsPerChunk = synced ? chunkPpq / stepBeats : lp.hz->load() * chunkSeconds;
            const double rateMultiplier = rateRouted[i] ? std::exp2 (2.0 * juce::jlimit (-1.0f, 1.0f, rateMod[i])) : 1.0;
            const float blend = lp.blend->load();
            const auto& pattern = audioPatterns[i];
            const auto& source = (int) lp.source->load() == 1 ? sidechain : mainIn;

            // Blocks of this orbit that LFOs modulate get a modulated copy (amplitude and
            // parameters); all other blocks play as they are.
            int numModulated = 0;
            for (int j = 0; j < kNumLfos; ++j)
                for (int r = 0; r < audioRoutings[j].numRoutes; ++r)
                {
                    const auto& route = audioRoutings[j].routes[(size_t) r];
                    if (route.lane != i || ! route.targetsBlock())
                        continue;

                    int k = 0;
                    while (k < numModulated && modulatedBlocks[(size_t) k].id != route.blockId)
                        ++k;

                    if (k == numModulated)
                    {
                        const int bi = pattern.findById (route.blockId);
                        if (bi < 0 || numModulated >= (int) modulatedBlocks.size())
                            continue;
                        auto& entry = modulatedBlocks[(size_t) numModulated++];
                        entry.id = route.blockId;
                        entry.ampMod = 0.0f;
                        entry.block = pattern.blocks[(size_t) bi];
                    }

                    const float m = route.amount * lfoValue[j];
                    if (route.target == ModTarget::BlockAmp)
                        modulatedBlocks[(size_t) k].ampMod += m;
                    else
                        applyParamMod (modulatedBlocks[(size_t) k].block, route.param, m);
                }

            const auto resolve = [&] (const Block& b) -> BlockView
            {
                for (int k = 0; k < numModulated; ++k)
                    if (modulatedBlocks[(size_t) k].id == b.id)
                        return { &modulatedBlocks[(size_t) k].block, modulatedBlocks[(size_t) k].ampMod };
                return unmodulated (b);
            };

            float v = 0.0f;
            float displayPos = -1.0f;

            if (mode == params::Sequence)
            {
                double pos;
                if (synced)
                {
                    // Without rate modulation the lane stays locked to the host position.
                    if (! rateRouted[i])
                        rateWarp[i] = 0.0;

                    const double lanePpq = masterBeats > 0.0 ? positiveMod (ppq, masterBeats) : ppq;
                    pos = positiveMod (lanePpq / stepBeats + rateWarp[i] + phaseSteps, (double) steps);

                    if (rateRouted[i])
                        rateWarp[i] = positiveMod (rateWarp[i] + (rateMultiplier - 1.0) * stepsPerChunk, (double) steps);
                }
                else
                {
                    // Free rate: runs at its own speed, restarting with the transport.
                    if ((justStarted || masterRestart) && s == 0)
                        freePos[i] = 0.0;

                    pos = positiveMod (freePos[i] + phaseSteps, (double) steps);
                    freePos[i] = positiveMod (freePos[i] + stepsPerChunk * rateMultiplier, (double) steps);
                }

                v = pointsView ? evalPoints (pattern.envelope, steps, pos)
                               : laneValueAt (pattern, steps, pos, blend, resolve);
                displayPos = (float) pos;
            }
            else if (mode == params::AudioFollow)
            {
                auto& f = followers[i];
                const float a = coefficient (lp.attack->load()), r = coefficient (lp.release->load());
                for (int n = s; n < s + chunkSamples; ++n)
                {
                    const float x = levelAt (source, n);
                    f.env += (x > f.env ? a : r) * (x - f.env);
                }

                const float threshold = lp.threshold->load();
                v = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (f.env, -100.0f) - threshold) / juce::jmax (1.0f, -threshold));
            }
            else
            {
                if (mode == params::MidiTrigger)
                {
                    for (int k = 0; k < numNotes; ++k)
                        if (notes[(size_t) k].sample >= s && notes[(size_t) k].sample < s + chunkSamples)
                            startTrigger (i, notes[(size_t) k].note, notes[(size_t) k].velocity, steps);
                }
                else
                {
                    auto& f = followers[i];
                    const float fast = coefficient (0.3f), r = coefficient (lp.release->load());
                    const float threshold = lp.threshold->load();
                    float peak = 0.0f;
                    bool hit = false;

                    for (int n = s; n < s + chunkSamples; ++n)
                    {
                        const float x = levelAt (source, n);
                        peak = juce::jmax (peak, x);
                        f.fastEnv += (x > f.fastEnv ? fast : r) * (x - f.fastEnv);

                        const float db = juce::Decibels::gainToDecibels (f.fastEnv, -100.0f);
                        if (f.holdoff > 0)
                            --f.holdoff;

                        if (f.armed && f.holdoff == 0 && db > threshold)
                        {
                            hit = true;
                            f.armed = false;
                            f.holdoff = (int) (0.03 * currentSampleRate);
                        }
                        else if (! f.armed && db < threshold - 6.0f)
                        {
                            f.armed = true;
                        }
                    }

                    if (hit)
                    {
                        const float velocity = (juce::Decibels::gainToDecibels (peak, -100.0f) - threshold) / juce::jmax (1.0f, -threshold);
                        startTrigger (i, -1, velocity, steps);
                    }
                }

                auto& t = triggers[i];
                if (t.active && t.wholeLane)
                {
                    if (t.pos >= steps)
                    {
                        t.active = false;
                        t.fadeFrom = t.lastValue;
                        t.fadeProgress = 0.0;
                    }
                    else
                    {
                        v = evalPoints (pattern.envelope, steps, t.pos) * t.gain;
                        displayPos = (float) t.pos;
                        t.pos += stepsPerChunk * rateMultiplier;
                    }
                }
                else if (t.active)
                {
                    const int bi = pattern.findById (t.blockId);
                    const Block* b = bi >= 0 ? &pattern.blocks[(size_t) bi] : nullptr;

                    if (b == nullptr || b->start >= steps || t.pos >= b->start + juce::jmin (b->length, steps - b->start))
                    {
                        // Finished: fade back to rest over Blend.
                        t.active = false;
                        t.fadeFrom = t.lastValue;
                        t.fadeProgress = 0.0;
                    }
                    else
                    {
                        const auto view = resolve (*b);
                        v = blockValueAt (*view.block, steps, t.pos, view.ampMod) * t.gain;
                        displayPos = (float) t.pos;
                        t.pos += stepsPerChunk * rateMultiplier;
                    }
                }

                if (t.fadeProgress < 1.0)
                {
                    if (blend <= 0.0f)
                    {
                        t.fadeProgress = 1.0;
                    }
                    else
                    {
                        v = t.fadeFrom + ease (t.fadeProgress) * (v - t.fadeFrom);
                        t.fadeProgress += stepsPerChunk / blend;
                    }
                }

                t.lastValue = v;
            }

            const float shapeValue = v;
            if (lp.invert->load() >= 0.5f)
                v = -v;

            const float depth = juce::jlimit (0.0f, 1.0f, lp.depth->load() + depthMod[i]);
            const float offset = lp.offset->load() + offsetMod[i];
            const float target = juce::jlimit (0.0f, 1.0f, lp.bipolar->load() >= 0.5f ? 0.5f + 0.5f * (depth * v + offset)
                                                                                         : depth * v + offset);

            // Snap (rather than glide) when the lane has just started sending.
            const float smoothMs = lp.smooth->load();
            if (lastValue[i] < 0 || smoothMs <= 0.0f)
                smoothed[i] = target;
            else
                smoothed[i] += (float) (1.0 - std::exp (-chunkSeconds * 1000.0 / smoothMs)) * (target - smoothed[i]);

            const int value = juce::roundToInt (smoothed[i] * 127.0f);
            const int channel = (int) lp.channel->load();
            const int controller = (int) lp.cc->load();

            if (value != lastValue[i] || channel != lastChannel[i] || controller != lastController[i])
            {
                midi.addEvent (juce::MidiMessage::controllerEvent (channel, controller, value), s);
                lastValue[i] = value;
                lastChannel[i] = channel;
                lastController[i] = controller;
            }

            if (s == 0)
            {
                lanePosition[i].store (displayPos);
                laneSpeed[i].store (displayPos >= 0.0f ? (float) (stepsPerChunk / chunkSeconds * rateMultiplier) : 0.0f);
                laneRaw[i].store (shapeValue);
                laneOutput[i].store (smoothed[i]);
                laneActive[i].store (true);
            }
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* ModLanesProcessor::createEditor()
{
    return new ModLanesEditor (*this);
}

//==============================================================================
// Scenes

juce::ValueTree ModLanesProcessor::captureScene() const
{
    juce::ValueTree scene (sceneId);
    scene.setProperty (versionId, kVersion, nullptr);

    juce::ValueTree patternsTree (patternsId);
    for (int i = 0; i < kNumLanes; ++i)
        patternsTree.appendChild (laneToTree (getPattern (i), i), nullptr);
    scene.appendChild (patternsTree, nullptr);

    juce::ValueTree routingsTree (routingsId);
    for (int j = 0; j < kNumLfos; ++j)
        routingsTree.appendChild (routingToTree (getRouting (j), j), nullptr);
    scene.appendChild (routingsTree, nullptr);

    juce::ValueTree paramsTree (paramsId);
    for (const auto& id : sceneParamIds)
        if (auto* param = apvts.getParameter (id))
        {
            juce::ValueTree p (paramId);
            p.setProperty ("id", id, nullptr);
            p.setProperty ("value", param->getValue(), nullptr); // normalised
            paramsTree.appendChild (p, nullptr);
        }
    scene.appendChild (paramsTree, nullptr);

    return scene;
}

// Loads a scene into the live state. Anything the scene does not mention is reset
// to its default, so a preset always sounds the same.
void ModLanesProcessor::applySceneData (const juce::ValueTree& scene)
{
    const int version = scene.getProperty (versionId, kVersion);

    std::array<LanePattern, kNumLanes> lanes {};
    for (const auto& laneTree : scene.getChildWithName (patternsId))
        if (const int lane = laneTree.getProperty ("index", -1); lane >= 0 && lane < kNumLanes)
            lanes[(size_t) lane] = laneFromTree (laneTree, version);

    std::array<LfoRouting, kNumLfos> routes {};
    for (const auto& lfoTree : scene.getChildWithName (routingsId))
        if (const int lfo = lfoTree.getProperty ("index", -1); lfo >= 0 && lfo < kNumLfos)
            routes[(size_t) lfo] = routingFromTree (lfoTree);

    for (int i = 0; i < kNumLanes; ++i)
        setPattern (i, lanes[(size_t) i]);
    for (int j = 0; j < kNumLfos; ++j)
        setRouting (j, routes[(size_t) j]);

    const auto paramsTree = scene.getChildWithName (paramsId);
    for (const auto& id : sceneParamIds)
        if (auto* param = apvts.getParameter (id))
        {
            const auto stored = paramsTree.getChildWithProperty ("id", id);
            const float value = stored.isValid() ? (float) stored.getProperty ("value") : param->getDefaultValue();
            if (std::abs (param->getValue() - value) > 1.0e-6f)
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (value);
                param->endChangeGesture();
            }
        }
}

bool ModLanesProcessor::isSceneUsed (int scene) const
{
    const juce::ScopedLock lock (sceneLock);
    return scene == currentScene.load() || scenes[(size_t) scene].isValid();
}

void ModLanesProcessor::requestScene (int scene)
{
    if (auto* param = apvts.getParameter (params::scene))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) (scene + 1)));
        param->endChangeGesture();
    }
}

void ModLanesProcessor::copyCurrentSceneTo (int scene)
{
    const juce::ScopedLock lock (sceneLock);
    if (scene != currentScene.load())
        scenes[(size_t) scene] = captureScene();
    ++sceneVersion;
}

void ModLanesProcessor::clearScene (int scene)
{
    {
        const juce::ScopedLock lock (sceneLock);
        scenes[(size_t) scene] = {};
    }

    if (scene == currentScene.load())
        applySceneData (juce::ValueTree (sceneId)); // back to an empty pattern

    ++sceneVersion;
}

void ModLanesProcessor::loadIntoCurrentScene (const juce::ValueTree& scene)
{
    applySceneData (scene);
    ++sceneVersion;
}

void ModLanesProcessor::setSceneData (int scene, const juce::ValueTree& data)
{
    if (scene == currentScene.load())
    {
        loadIntoCurrentScene (data);
        return;
    }

    const juce::ScopedLock lock (sceneLock);
    scenes[(size_t) scene] = data.createCopy();
    ++sceneVersion;
}

void ModLanesProcessor::switchToScene (int scene)
{
    const juce::ScopedLock lock (sceneLock);
    const int from = currentScene.load();

    scenes[(size_t) from] = captureScene();

    // An empty slot starts as a copy of the current scene, ready to be varied.
    if (! scenes[(size_t) scene].isValid())
        scenes[(size_t) scene] = scenes[(size_t) from].createCopy();

    currentScene.store (scene);
    applySceneData (scenes[(size_t) scene]);
    ++sceneVersion;
}

void ModLanesProcessor::parameterChanged (const juce::String& id, float newValue)
{
    if (id == params::scene)
        pendingScene.store (juce::jlimit (0, kNumScenes - 1, juce::roundToInt (newValue) - 1));
}

// Message thread: performs requested scene changes, waiting for the quantise
// boundary reported by the audio thread while the transport runs.
void ModLanesProcessor::timerCallback()
{
    const int target = pendingScene.load();
    if (target == currentScene.load())
    {
        waitingForBoundary = false;
        return;
    }

    if (! waitingForBoundary)
    {
        waitingForBoundary = true;
        requestSerial = boundarySerial.load();
    }

    const bool instant = (int) quantizeParam->load() == 0 || ! transportRunning.load();
    if (instant || boundarySerial.load() != requestSerial)
    {
        waitingForBoundary = false;
        switchToScene (target);
    }
}

//==============================================================================
void ModLanesProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (versionId, kVersion, nullptr);

    // The live pattern at the top level, as in earlier versions.
    const auto live = captureScene();
    for (const auto& id : { patternsId, routingsId })
    {
        state.removeChild (state.getChildWithName (id), nullptr);
        state.appendChild (live.getChildWithName (id).createCopy(), nullptr);
    }

    juce::ValueTree scenesTree ("SCENES");
    scenesTree.setProperty ("current", currentScene.load(), nullptr);
    {
        const juce::ScopedLock lock (sceneLock);
        for (int k = 0; k < kNumScenes; ++k)
        {
            const auto scene = k == currentScene.load() ? live : scenes[(size_t) k];
            if (scene.isValid())
            {
                auto slot = scene.createCopy();
                slot.setProperty ("slot", k, nullptr);
                scenesTree.appendChild (slot, nullptr);
            }
        }
    }
    state.removeChild (state.getChildWithName ("SCENES"), nullptr);
    state.appendChild (scenesTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ModLanesProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const int version = tree.getProperty (versionId, 1);

    for (int i = 0; i < kNumLanes; ++i)
        setPattern (i, {});
    for (int j = 0; j < kNumLfos; ++j)
        setRouting (j, {});

    for (const auto& laneTree : tree.getChildWithName (patternsId))
        if (const int lane = laneTree.getProperty ("index", -1); lane >= 0 && lane < kNumLanes)
            setPattern (lane, laneFromTree (laneTree, version));

    for (const auto& lfoTree : tree.getChildWithName (routingsId))
        if (const int lfo = lfoTree.getProperty ("index", -1); lfo >= 0 && lfo < kNumLfos)
            setRouting (lfo, routingFromTree (lfoTree));

    const auto scenesTree = tree.getChildWithName ("SCENES");
    {
        const juce::ScopedLock lock (sceneLock);
        for (auto& scene : scenes)
            scene = {};
        for (const auto& slot : scenesTree)
            if (const int k = slot.getProperty ("slot", -1); k >= 0 && k < kNumScenes)
                scenes[(size_t) k] = slot.createCopy();
    }

    const int current = juce::jlimit (0, kNumScenes - 1, (int) scenesTree.getProperty ("current", 0));
    currentScene.store (current);

    for (const auto& id : { patternsId, routingsId, juce::Identifier ("SCENES") })
        tree.removeChild (tree.getChildWithName (id), nullptr);

    if (version < 3)
        migrateRateOrder (tree);

    tree.removeProperty (versionId, nullptr);
    apvts.replaceState (tree);
    pendingScene.store (current); // replaceState may have notified the scene listener
    ++sceneVersion;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModLanesProcessor();
}
