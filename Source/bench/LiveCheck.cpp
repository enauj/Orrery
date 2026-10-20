// Live animation check (benchmark build only): opens the editor in a real window,
// runs the audio engine on its own thread at real-time pace with the internal
// clock playing, and records when orbit 1's playhead is animated and painted.
// Reports how evenly frames arrive and how evenly the dot moves per frame.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Params.h"

#include <algorithm>
#include <functional>
#include <cstdio>
#include <atomic>
#include <mutex>
#include <vector>

namespace
{
struct Sample
{
    double ms, position;
};

class AudioThread : public juce::Thread
{
public:
    AudioThread (ModLanesProcessor& p, int blockSize) : juce::Thread ("bench audio"), proc (p), block (blockSize) {}

    void run() override
    {
        juce::AudioBuffer<float> buffer (4, block);
        juce::MidiBuffer midi;
        const double blockMs = block / 48.0;
        double next = juce::Time::getMillisecondCounterHiRes();

        while (! threadShouldExit())
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);

            next += blockMs;
            const double wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 1.0)
                juce::Thread::sleep ((int) wait);
        }
    }

private:
    ModLanesProcessor& proc;
    const int block;
};

void report (const char* title, const std::vector<Sample>& samples, double stepsPerSecond, double steps)
{
    if (samples.size() < 10)
    {
        std::printf ("  %-12s too few samples (%d)\n", title, (int) samples.size());
        return;
    }

    std::vector<double> intervals, speedError;
    for (size_t i = 1; i < samples.size(); ++i)
    {
        const double dt = samples[i].ms - samples[i - 1].ms;
        if (dt <= 0.0)
            continue;
        intervals.push_back (dt);
        const double moved = std::remainder (samples[i].position - samples[i - 1].position, steps);
        speedError.push_back (moved / (dt / 1000.0) / stepsPerSecond - 1.0); // 0 = perfectly even
    }

    auto sorted = intervals;
    std::sort (sorted.begin(), sorted.end());
    const auto pct = [&sorted] (double q) { return sorted[(size_t) (q * (double) (sorted.size() - 1))]; };

    double sum = 0.0, worstSpeed = 0.0, sumSpeed = 0.0;
    for (double v : intervals) sum += v;
    for (double e : speedError) { worstSpeed = std::max (worstSpeed, std::abs (e)); sumSpeed += e * e; }

    std::printf ("  %-12s %4d frames, %5.1f fps | interval ms: min %5.1f  median %5.1f  95%% %5.1f  max %5.1f | "
                 "speed error: rms %4.1f%%  worst %5.1f%%\n",
                 title, (int) samples.size(), 1000.0 * (double) intervals.size() / sum, sorted.front(), pct (0.5), pct (0.95),
                 sorted.back(), 100.0 * std::sqrt (sumSpeed / (double) speedError.size()), 100.0 * worstSpeed);
}
} // namespace

int runLiveCheck (int blockSize, bool gpu)
{
    modlanes::theme::useGpuRenderer() = gpu;
    ModLanesProcessor proc;
    proc.setPlayConfigDetails (4, 2, 48000.0, blockSize);
    proc.prepareToPlay (48000.0, blockSize);

    const auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* param = proc.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (modlanes::params::clock, 1.0f);   // internal
    set (modlanes::params::bpm, 120.0f);
    set (modlanes::params::play, 1.0f);

    std::mutex lock;
    std::vector<Sample> animated, painted;
    modlanes::StepGrid::animateLog = [&] (int lane, double ms, double position)
    { if (lane == 0) { std::lock_guard<std::mutex> l (lock); animated.push_back ({ ms, position }); } };
    std::vector<std::pair<double, juce::String>> renders;
    modlanes::StepGrid::staticRenderLog = [&] (int lane, double ms, juce::String reason)
    { std::lock_guard<std::mutex> l (lock); renders.push_back ({ ms, "orbit " + juce::String (lane + 1) + ": " + reason }); };
    modlanes::StepGrid::paintLog = [&] (int lane, double ms, double position)
    { if (lane == 0 && position >= 0.0) { std::lock_guard<std::mutex> l (lock); painted.push_back ({ ms, position }); } };

    juce::DocumentWindow window ("Orrery live check", juce::Colours::black, 0);
    window.setUsingNativeTitleBar (true);
    window.setContentOwned (proc.createEditor(), true);
    window.setVisible (true);

    AudioThread audio (proc, blockSize);
    audio.startThread (juce::Thread::Priority::highest);

    // Let it settle for 1.5 s, then record for 4 s.
    juce::Timer::callAfterDelay (1500, [&]
    {
        std::lock_guard<std::mutex> l (lock);
        animated.clear();
        painted.clear();
        renders.clear();
    });
    juce::Timer::callAfterDelay (5500, [] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    juce::MessageManager::getInstance()->runDispatchLoop();

    audio.stopThread (2000);
    window.clearContentComponent();
    modlanes::StepGrid::animateLog = nullptr;
    modlanes::StepGrid::paintLog = nullptr;

    // Orbit 1 default: 16 steps of 1/16 at 120 BPM = 8 steps per second.
    std::printf ("LIVE CHECK (%s renderer, block %d samples at 48 kHz, orbit 1 at 8 steps/s)\n", gpu ? "Direct2D" : "software", blockSize);
    report ("animate", animated, 8.0, 16.0);
    report ("paint", painted, 8.0, 16.0);
    std::printf ("  cached grid layer re-rendered %d times while recording\n", (int) renders.size());
    for (size_t i = 0; i < std::min<size_t> (renders.size(), 6); ++i)
        std::printf ("    %6.1f ms  %s\n", renders[i].first, renders[i].second.toRawUTF8());
    return 0;
}

//==============================================================================
// Where the UI's CPU time goes (benchmark build only): runs the real window with the
// internal clock playing and LFOs routed to Depth and to a block, 3 s per pass. The
// first pass shows everything; each later pass hides one part, so its cost is the
// difference. Uses the interface scale saved by the Standalone.
double threadCpuSeconds();
double processCpuSeconds();

namespace
{
template <typename T>
void collectAll (juce::Component& root, juce::Array<juce::Component*>& out)
{
    for (auto* child : root.getChildren())
    {
        if (dynamic_cast<T*> (child) != nullptr)
            out.add (child);
        collectAll<T> (*child, out);
    }
}
} // namespace

int runProfile (int blockSize, bool gpu)
{
    modlanes::theme::useGpuRenderer() = gpu;
    ModLanesProcessor proc;
    proc.setPlayConfigDetails (4, 2, 48000.0, blockSize);
    proc.prepareToPlay (48000.0, blockSize);

    const auto set = [&proc] (const juce::String& id, float value)
    {
        if (auto* param = proc.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    set (modlanes::params::clock, 1.0f);
    set (modlanes::params::bpm, 120.0f);
    set (modlanes::params::play, 1.0f);

    // ORRERY_PROFILE_STATE=standalone: the state the Standalone last saved, as it is.
    // Otherwise a block on orbit 2 and routes, so the live modulation view has work to do.
    if (juce::SystemStats::getEnvironmentVariable ("ORRERY_PROFILE_STATE", {}) == "standalone")
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Orrery";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        juce::PropertiesFile settings (options);
        juce::MemoryBlock data;
        if (data.fromBase64Encoding (settings.getValue ("filterState")))
            proc.setStateInformation (data.getData(), (int) data.getSize());

        for (int i = 0; i < modlanes::kNumLanes; ++i)
        {
            const auto value = [&] (const char* name) { return proc.apvts.getRawParameterValue (modlanes::params::lane (i, name))->load(); };
            std::printf ("  orbit %d: on %d, mode %d, steps %d, points %d, %d blocks\n", i + 1, (int) value ("on"), (int) value ("mode"),
                         (int) value ("steps"), (int) value ("points"), (int) proc.getPattern (i).blocks.size());
        }
        for (int j = 0; j < modlanes::kNumLfos; ++j)
            for (int r = 0; r < proc.getRouting (j).numRoutes; ++r)
            {
                const auto route = proc.getRouting (j).routes[(size_t) r];
                const int block = proc.getPattern (route.lane).findById (route.blockId);
                std::printf ("  LFO %d -> target %d, orbit %d, block index %d (start %d, length %d), param %d\n", j + 1, (int) route.target,
                             route.lane + 1, block, block >= 0 ? proc.getPattern (route.lane).blocks[(size_t) block].start : -1,
                             block >= 0 ? proc.getPattern (route.lane).blocks[(size_t) block].length : -1, route.param);
            }
        std::printf ("  clock %d, play %d\n", (int) proc.apvts.getRawParameterValue (modlanes::params::clock)->load(),
                     (int) proc.apvts.getRawParameterValue (modlanes::params::play)->load());
        set (modlanes::params::clock, 1.0f);
        set (modlanes::params::play, 1.0f);
    }
    else
    {
        auto pattern = proc.getPattern (1);
        if (pattern.blocks.empty())
            pattern.add (modlanes::Block::make (modlanes::BlockType::Sine, 0, 8));
        proc.setPattern (1, pattern);
        modlanes::LfoRouting routing;
        routing.add ({ modlanes::ModTarget::LaneDepth, 0, 0, 0.4f });
        modlanes::ModRoute cycles { modlanes::ModTarget::BlockParam, 1, proc.getPattern (1).blocks[0].id, 0.5f };
        cycles.param = modlanes::kModCycles;
        routing.add (cycles);
        proc.setRouting (0, routing);
    }

    // ORRERY_PROFILE_SCALE=0.75 etc.: interface scale (default: the saved one).
    if (const auto s = juce::SystemStats::getEnvironmentVariable ("ORRERY_PROFILE_SCALE", {}); s.isNotEmpty())
        proc.apvts.state.setProperty ("uiScale", s.getDoubleValue(), nullptr);
    // ORRERY_PROFILE_LIVE=0: Live view off.
    if (juce::SystemStats::getEnvironmentVariable ("ORRERY_PROFILE_LIVE", "1") == "0")
        proc.apvts.state.setProperty ("liveView", false, nullptr);

    juce::DocumentWindow window ("Orrery profile", juce::Colours::black, 0);
    window.setUsingNativeTitleBar (true);
    window.setContentOwned (proc.createEditor(), true);
    window.setVisible (true);
    auto& editor = *window.getContentComponent();

    struct Part
    {
        const char* name;
        juce::Array<juce::Component*> components;
    };
    std::vector<Part> parts (6);
    parts.reserve (8);
    parts[0].name = "orbit grids (playhead, live curves)";
    collectAll<modlanes::StepGrid> (editor, parts[0].components);
    parts[1].name = "LFO panels (waves, knobs)";
    collectAll<modlanes::LfoPanel> (editor, parts[1].components);
    parts[2].name = "orrery view";
    collectAll<modlanes::OrreryView> (editor, parts[2].components);
    parts[3].name = "phrase indicator + scene bar";
    collectAll<modlanes::PhraseIndicator> (editor, parts[3].components);
    collectAll<modlanes::SceneBar> (editor, parts[3].components);
    parts[4].name = "orbit strips (knobs, meters)";
    for (auto* c : editor.getChildren())
        if (dynamic_cast<modlanes::LaneStrip*> (c) != nullptr)
            for (auto* child : c->getChildren())
                if (dynamic_cast<modlanes::StepGrid*> (child) == nullptr)
                    parts[4].components.add (child);
    parts[5].name = "inspector";
    collectAll<modlanes::BlockInspector> (editor, parts[5].components);
    parts.push_back ({ "ALL of the above hidden", {} });
    for (size_t i = 0; i + 1 < parts.size(); ++i)
        parts.back().components.addArray (parts[i].components);

    std::atomic<int> staticRenders { 0 };
    std::atomic<double> staticRenderMs { 0.0 };
    juce::String lastReason;
    modlanes::StepGrid::staticRenderLog = [&] (int, double ms, juce::String reason)
    {
        ++staticRenders;
        staticRenderMs = staticRenderMs + ms;
        lastReason = reason;
    };

    AudioThread audio (proc, blockSize);
    audio.startThread (juce::Thread::Priority::highest);

    // Passes are interleaved over several rounds and the median taken, because a
    // laptop CPU changes its clock speed from second to second.
    constexpr int rounds = 3;
    const int perRound = (int) parts.size() + 1;
    const int numPasses = rounds * perRound;
    std::vector<std::vector<double>> ui ((size_t) perRound), process ((size_t) perRound);
    double startWall = 0.0, startUi = 0.0, startProcess = 0.0;
    int pass = -1; // -1 = settling
    struct Split { double animate, paint, timer, fps; };
    std::vector<Split> split;

    std::function<void()> next = [&]
    {
        const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        if (pass >= 0)
        {
            const int kind = pass % perRound;
            if (kind == 0)
            {
                const auto& t = ModLanesEditor::frameTimes;
                const double wall = (now - startWall) * 1000.0;
                const double perSecond = wall / 1000.0;
                std::printf ("    painting %.0f ms/s in %d calls/s; window background %.0f ms/s in %d paints/s\n",
                             t.paint / perSecond, (int) (t.paintCalls / perSecond), t.editorMs / perSecond, (int) (t.editorPaints / perSecond));
                split.push_back ({ t.animate / wall, t.paint / wall, t.timer / wall, t.frames / (wall / 1000.0) });
                const auto& gs = modlanes::StepGrid::paintStats;
                std::printf ("    grids: %d paints/s, own paint() %.1f ms/s, avg clip box %.0f px (editor %d x %d)\n",
                             (int) (gs.calls / (wall / 1000.0)), gs.ms / (wall / 1000.0), gs.calls > 0 ? gs.area / gs.calls : 0.0,
                             editor.getWidth(), editor.getHeight());
                std::printf ("    grid cache re-rendered %d times (%.1f ms/s), last reason: %s\n", staticRenders.load(),
                             staticRenderMs.load() / (wall / 1000.0), lastReason.toRawUTF8());
            }
            ui[(size_t) kind].push_back ((threadCpuSeconds() - startUi) / (now - startWall));
            process[(size_t) kind].push_back ((processCpuSeconds() - startProcess) / (now - startWall));
            if (kind > 0)
                for (auto* c : parts[(size_t) kind - 1].components)
                    c->setVisible (true);
        }

        if (++pass >= numPasses)
        {
            juce::MessageManager::getInstance()->stopDispatchLoop();
            return;
        }
        if (const int kind = pass % perRound; kind > 0)
            for (auto* c : parts[(size_t) kind - 1].components)
                c->setVisible (false);

        ModLanesEditor::frameTimes = {};
        modlanes::StepGrid::paintStats = {};
        staticRenders = 0;
        staticRenderMs = 0.0;
        startWall = now;
        startUi = threadCpuSeconds();
        startProcess = processCpuSeconds();
        juce::Timer::callAfterDelay (2000, next);
    };
    juce::Timer::callAfterDelay (1500, next);
    juce::MessageManager::getInstance()->runDispatchLoop();

    audio.stopThread (2000);
    window.clearContentComponent();

    const float scale = (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale;
    std::printf ("PROFILE (%s renderer, display scale %.2f, clock playing, LFOs routed; %% of one core)\n", gpu ? "Direct2D" : "software", scale);
    const auto median = [] (std::vector<double> v) { std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
    const double all = median (ui[0]);
    std::printf ("  %-40s UI thread %5.1f %%   whole process %5.1f %%\n", "everything visible", 100.0 * all, 100.0 * median (process[0]));
    for (const auto& t : split)
        std::printf ("    wall-clock share: display-rate frame %4.1f %%  30 Hz timer %4.1f %%  (painting in both: %4.1f %%)  (%.0f frames/s)\n",
                     100.0 * t.animate, 100.0 * t.timer, 100.0 * t.paint, t.fps);
    for (int k = 1; k < perRound; ++k)
        std::printf ("  %-40s %5.1f %%  (%d components)\n", parts[(size_t) k - 1].name, 100.0 * (all - median (ui[(size_t) k])),
                     parts[(size_t) k - 1].components.size());
    return 0;
}
