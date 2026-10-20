#pragma once

#include "PluginProcessor.h"
#include "gui/BlockInspector.h"
#include "gui/LaneStrip.h"
#include "gui/LfoPanel.h"
#include "gui/MapContext.h"
#include "gui/ModulePalette.h"
#include "gui/OrreryView.h"
#include "gui/PhraseIndicator.h"
#include "gui/SceneBar.h"

class ModLanesEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,
                       private juce::Timer
{
public:
    explicit ModLanesEditor (ModLanesProcessor&);
    ~ModLanesEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void selectBlock (int lane, int index);
    void setMapping (int lfo);
    void useSoftwareRenderer();

    ModLanesProcessor& proc;
    modlanes::theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    modlanes::MapContext mapContext;

    modlanes::ModulePalette palette;
    std::array<std::unique_ptr<modlanes::LaneStrip>, modlanes::kNumLanes> lanes;
    std::array<std::unique_ptr<modlanes::LfoPanel>, modlanes::kNumLfos> lfos;
    modlanes::BlockInspector inspector;

    juce::ToggleButton freeRunButton { "Run when stopped" }, thruButton { "MIDI thru" }, liveButton { "Live view" };
    juce::Image grain, background; // background: grain + astrolabe at screen resolution
    modlanes::PhraseIndicator phrase { proc };
    modlanes::SceneBar sceneBar { proc };
    modlanes::OrreryView orrery { proc };
    int knownSceneVersion = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freeRunAttachment, thruAttachment;

    void animationFrame();

    // Interface size, chosen by right-clicking empty space. The layout is done at
    // 100% and every child is scaled by the same factor.
    void setUiScale (float scale);
    void showScaleMenu();
    juce::Rectangle<int> logicalBounds() const;
    float uiScale = 1.0f;

    int knownPatternVersion = 0;
    double lastAnimationMs = 0.0;
    bool wasRunning = false;
    juce::ComponentPeer* rendererPeer = nullptr;

    // Playheads and the orrery follow the display's refresh (capped at ~60 fps);
    // everything else uses the 30 fps timer. Declared last so it stops first.
    juce::VBlankAttachment vblank { this, [this] { animationFrame(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModLanesEditor)
};
