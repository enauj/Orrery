#pragma once

#include "LiveState.h"
#include "MapContext.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include "MidiImport.h"
#include "UserModules.h"

namespace modlanes
{

// The step sequencer for one lane. Blocks are placed by dragging modules from
// the palette and shaped directly with handles, similar to Bitwig's automation
// shapes: amplitude (centre dot), fade in / fade out (top corners), cycles
// (bottom pill) and length (left / right edges).
class StepGrid : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget
{
public:
    StepGrid (ModLanesProcessor& processor, int laneIndex, MapContext& mapContext);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    // MIDI files with CC data (e.g. clips dragged from the DAW) become a Curve
    // block, or the Points envelope when the orbit is in Points view.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Called by the editor timer: records the follower trace and handles note learn.
    void poll();

    // Off: no playhead, value dot or follower trace (saves CPU).
    void setLiveView (bool live) { liveView = live; }

    // Per display frame: advances the drawn playhead and repaints only the columns
    // it left and entered (the whole grid while following audio, whose trace scrolls).
    void animate();

    // Per display frame: redraws the modulated curves of blocks that LFOs move.
    void animateModulation();

    // Playhead position extrapolated from the last audio block (smooth even with
    // large host buffers), in steps, or -1 when the orbit is idle.
    float livePosition() const;

   #if MODLANES_PAINT_LOG
    // Benchmark-only instrumentation: called for every animation step and paint.
    static inline std::function<void (int lane, double ms, double position)> animateLog, paintLog;
    static inline std::function<void (int lane, double durationMs, juce::String reason)> staticRenderLog;
   #endif

    void reloadFromProcessor();
    void clearSelection();
    void replaceBlock (int index, const Block& block);
    const LanePattern& getPattern() const { return pattern; }

    std::function<void (int lane, int blockIndex)> onSelect;       // selection changed
    std::function<void (int lane, int blockIndex)> onBlockChanged; // edited on the grid
    std::function<void (int version)> onCommitted;
    std::function<void()> onUserModulesChanged; // a module was saved

private:
    enum class Handle { None, Body, ResizeLeft, ResizeRight, Amp, FadeIn, FadeOut, Cycles };

    struct Geometry
    {
        juce::Rectangle<float> body;  // visible part of the block
        juce::Rectangle<float> curve; // full-length drawing area, zero on its centre line
        juce::Point<float> amp, fadeIn, fadeOut;
        juce::Rectangle<float> cycles;
    };

    int numSteps() const;
    float stepWidth() const;
    int stepAt (float x) const;
    int blockAtStep (int step) const;
    float curveTop() const { return 12.0f; }
    float curveBottom() const { return (float) getHeight() - 12.0f; }

    Geometry geometryFor (const Block&) const;
    Handle handleAt (int index, juce::Point<float> p) const;
    void paintStatic (juce::Graphics&);
    void paintBlock (juce::Graphics&, int index, const std::array<LfoRouting, kNumLfos>& routings);
    void paintHandles (juce::Graphics&, const Block&, const Geometry&, juce::Colour);
    void paintBlendedCurve (juce::Graphics&);
    void paintFollowerTrace (juce::Graphics&);
    int mode() const;
    float liveValue (float position) const;
    void paintLiveBlocks (juce::Graphics&);

    // Points view (StepGridPoints.cpp)
    bool pointsView() const { return pointsParam->load() >= 0.5f; }
    juce::Point<float> pointPosition (const EnvPoint&) const;
    juce::Point<float> segmentHandle (int segment) const;
    int pointAt (juce::Point<float>) const;
    int segmentAt (juce::Point<float>) const;
    void paintPoints (juce::Graphics&);
    void pointsMouseDown (const juce::MouseEvent&);
    void pointsMouseDrag (const juce::MouseEvent&);
    void pointsMouseMove (const juce::MouseEvent&);
    void pointsDoubleClick (const juce::MouseEvent&);
    bool pointsKeyPressed (const juce::KeyPress&);
    void showPointsMenu (int point, int segment);
    void renderFromBlocks();
    void resized() override;

    void select (int index);
    void commit();
    void notifyEdited();
    void addOrReplaceAt (BlockType type, int step);
    void placePrototypeAt (const Block& prototype, int step);
    void importMidi (const juce::File& file, int step);
    void saveAsModule (const Block& block, const juce::String& suggestion);
    void showModTargetMenu (int blockIndex);
    void duplicate (int index);
    void showContextMenu (int blockIndex, int step);

    ModLanesProcessor& proc;
    const int lane;
    MapContext& mapContext;
    std::atomic<float>* stepsParam;
    std::atomic<float>* modeParam;
    std::atomic<float>* blendParam;
    std::atomic<float>* pointsParam;
    std::atomic<float>* onParam;

    enum class PointDrag { None, Point, Bend };
    PointDrag pointDrag = PointDrag::None;
    int selectedPoint = -1, hoveredPoint = -1, hoveredSegment = -1, dragSegment = -1;
    float dragStartBend = 0.0f;
    bool shownPointsView = false;
    bool liveView = true;

    // Everything except the playhead is cached at screen resolution and only
    // redrawn when something that affects it changes.
    struct StaticKey
    {
        int patternVersion, routingVersion, selected, hovered, hoveredHandle, hoverStep, mapLfo, steps, mode;
        int points, selectedPoint, hoveredPoint, hoveredSegment, learnBlockId, width, height;
        float blend, scale;
        bool focus, on;
        bool operator== (const StaticKey&) const = default;
    };
    StaticKey makeStaticKey (float scale) const;
    juce::Image staticLayer;
    juce::Rectangle<int> lastLiveArea;

    // The drawn playhead runs on its own clock at the orbit's speed and is only
    // nudged towards the reported position, so bursty audio blocks do not make it jump.
    double shownPosition = -1.0;
    double lastAnimationMs = 0.0;
    double lastPaintedPosition = -2.0;
    StaticKey staticKey {};
    juce::TextButton renderButton { "From blocks" };

    std::array<float, 240> trace {};
    int tracePos = 0;
    int learnBlockId = 0; // block waiting for a note, 0 = none
    int learnSerial = 0;

    LanePattern pattern;

    // Blocks that LFOs modulate, as they play right now (amplitude folded into
    // amount); drawn over the cached curves and followed by the value dot.
    struct LiveBlock
    {
        Block block;
        int lfo = 0;
    };
    LiveModulation liveMod { proc };
    std::vector<LiveBlock> liveBlocks;
    std::array<float, kNumLfos> shownLfoOutputs {};
    std::array<int, 4> shownLiveVersion {};
    juce::Rectangle<int> liveBlockArea;
    int selected = -1;
    int hovered = -1;
    Handle hoveredHandle = Handle::None;
    int hoverStep = -1;

    Handle dragMode = Handle::None;
    Block dragStart;
    juce::Point<float> dragStartPos;
    int grabOffset = 0;
    BlockType lastType = BlockType::Envelope;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

} // namespace modlanes
