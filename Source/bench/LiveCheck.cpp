// Live animation check (benchmark build only): opens the editor in a real window,
// runs the audio engine on its own thread at real-time pace with the internal
// clock playing, and records when orbit 1's playhead is animated and painted.
// Reports how evenly frames arrive and how evenly the dot moves per frame.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Params.h"

#include <algorithm>
#include <cstdio>
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
