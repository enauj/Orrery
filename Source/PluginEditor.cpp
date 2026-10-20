#include "PluginEditor.h"
#include "Params.h"

using namespace modlanes;

namespace
{
constexpr int kBaseWidth = 1380, kBaseHeight = 920;
constexpr float kScales[] = { 0.75f, 0.85f, 1.0f, 1.15f, 1.25f, 1.5f, 1.75f, 2.0f };

// Settings shared by all instances (the last chosen interface size).
juce::PropertiesFile& globalSettings()
{
    static juce::PropertiesFile file ([]
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Orrery";
        options.filenameSuffix = ".settings";
        options.folderName = "Orrery";
        options.osxLibrarySubFolder = "Application Support";
        return options;
    }());
    return file;
}

// Knob value popups are parented to the editor: as separate desktop windows they
// flicker inside plugin hosts.
void enableValuePopups (juce::Component& root, juce::Component& parent)
{
    for (auto* child : root.getChildren())
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (child))
            if (slider->getTextBoxPosition() == juce::Slider::NoTextBox)
                slider->setPopupDisplayEnabled (true, true, &parent);

        enableValuePopups (*child, parent);
    }
}
} // namespace

ModLanesEditor::ModLanesEditor (ModLanesProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (palette);

    mapContext.pick = [this] (const ModRoute& route)
    {
        const int lfo = mapContext.activeLfo;
        if (lfo < 0)
            return;

        auto routing = proc.getRouting (lfo);
        if (routing.find (route) < 0)
        {
            if (routing.numRoutes >= kMaxRoutes)
                routing.remove (0); // full: replace the oldest route
            routing.add (route);
            proc.setRouting (lfo, routing);
        }

        setMapping (-1);
    };

    for (int i = 0; i < kNumLanes; ++i)
    {
        lanes[(size_t) i] = std::make_unique<LaneStrip> (proc, i, mapContext);
        auto& grid = lanes[(size_t) i]->grid;
        grid.onSelect = [this] (int lane, int index) { selectBlock (lane, index); };
        grid.onBlockChanged = [this] (int lane, int index) { selectBlock (lane, index); };
        grid.onCommitted = [this] (int version) { knownPatternVersion = version; };
        grid.onUserModulesChanged = [this] { palette.refreshUserModules(); };
        addAndMakeVisible (*lanes[(size_t) i]);
    }

    for (int i = 0; i < kNumLfos; ++i)
    {
        lfos[(size_t) i] = std::make_unique<LfoPanel> (proc, i, mapContext);
        lfos[(size_t) i]->onMapClicked = [this] (int lfo) { setMapping (mapContext.activeLfo == lfo ? -1 : lfo); };
        addAndMakeVisible (*lfos[(size_t) i]);
    }

    // The inspector edits the shape; placement and id always come from the grid.
    inspector.onEdit = [this] (int lane, int index, const Block& edited)
    {
        auto& grid = lanes[(size_t) lane]->grid;
        if (index >= grid.getPattern().numBlocks)
            return;

        const auto& original = grid.getPattern().blocks[(size_t) index];
        auto b = edited;
        b.start = original.start;
        b.length = original.length;
        b.id = original.id;
        grid.replaceBlock (index, b);
    };
    inspector.attach (proc, mapContext);
    addAndMakeVisible (inspector);

    for (auto* b : { &freeRunButton, &thruButton, &liveButton })
        addAndMakeVisible (*b);

    // Live view is a UI preference stored with the plugin state, not an automatable parameter.
    liveButton.setTooltip ("Animate playheads, value dots, meters and LFOs. Turn off to save CPU.");
    liveButton.setToggleState (proc.apvts.state.getProperty ("liveView", true), juce::dontSendNotification);
    liveButton.onClick = [this] { proc.apvts.state.setProperty ("liveView", liveButton.getToggleState(), nullptr); };
    grain = theme::makeGrainTile();
    addAndMakeVisible (phrase);
    addAndMakeVisible (sceneBar);
    addAndMakeVisible (orrery);
    freeRunButton.setTooltip ("Keep the orbits running at the host tempo while the transport is stopped");
    thruButton.setTooltip ("Pass incoming MIDI through to the output");
    freeRunAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::freeRun, freeRunButton);
    thruAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, params::thru, thruButton);

    enableValuePopups (*this, *this);
    knownPatternVersion = proc.getPatternVersion();

    // Interface size: from the project if it has one, otherwise the last one used.
    uiScale = juce::jlimit (0.5f, 2.5f, (float) proc.apvts.state.getProperty ("uiScale",
                                                     globalSettings().getDoubleValue ("uiScale", 1.0)));
    addMouseListener (this, true); // right-clicks on empty panel space open the size menu

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (1200 * uiScale), juce::roundToInt (840 * uiScale),
                     juce::roundToInt (2400 * uiScale), juce::roundToInt (1500 * uiScale));
    setSize (juce::roundToInt (kBaseWidth * uiScale), juce::roundToInt (kBaseHeight * uiScale));
    startTimerHz (30);
}

ModLanesEditor::~ModLanesEditor()
{
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void ModLanesEditor::selectBlock (int lane, int index)
{
    for (int i = 0; i < kNumLanes; ++i)
        if (i != lane)
            lanes[(size_t) i]->grid.clearSelection();

    if (index >= 0)
        inspector.showBlock (lane, index, lanes[(size_t) lane]->grid.getPattern().blocks[(size_t) index]);
    else
        inspector.clear();
}

void ModLanesEditor::setMapping (int lfo)
{
    mapContext.activeLfo = lfo;

    for (auto& lane : lanes)
        lane->mapStateChanged();
    inspector.mapStateChanged();
    for (auto& panel : lfos)
        panel->refresh (liveButton.getToggleState());

    repaint (0, 0, getWidth(), juce::roundToInt (50 * uiScale));
}

bool ModLanesEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && mapContext.isMapping())
    {
        setMapping (-1);
        return true;
    }

    return false;
}

// JUCE 8's Direct2D renderer can leave flickering rectangles in some hosts and
// GPUs when small regions repaint quickly; the software renderer is plenty here.
void ModLanesEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer == nullptr || peer == rendererPeer)
        return;

    rendererPeer = peer;
    if (const int engine = peer->getAvailableRenderingEngines().indexOf (theme::useGpuRenderer() ? "Direct2D" : "Software Renderer"); engine >= 0)
        peer->setCurrentRenderingEngine (engine);
}

void ModLanesEditor::animationFrame()
{
    // Follows the display refresh exactly: skipping frames on fast displays made the
    // spacing uneven. The guard only filters duplicate callbacks.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastAnimationMs < 4.0)
        return;
    lastAnimationMs = now;

    const bool live = liveButton.getToggleState();
    for (auto& lane : lanes)
        lane->animate();
    inspector.animateModulation (live);
    orrery.refresh (live);

    // Paint now rather than whenever Windows gets round to it: paint messages have the
    // lowest priority and were delivered in bursts (about 25 per second with ~100 ms gaps).
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
}

void ModLanesEditor::timerCallback()
{
    useSoftwareRenderer();

    // Patterns changed outside the editor (state restored by the host).
    if (const int version = proc.getPatternVersion(); version != knownPatternVersion)
    {
        knownPatternVersion = version;
        for (auto& lane : lanes)
            lane->grid.reloadFromProcessor();
        inspector.clear();
    }

    const bool live = liveButton.getToggleState();
    phrase.refresh(); // always on (also without Live view); repaints only when it changes
    sceneBar.refresh();
    inspector.refreshModulation();

    // A scene switch replaces the patterns; the version check below reloads the grids.
    if (const int v = proc.sceneVersion.load(); v != knownSceneVersion)
    {
        knownSceneVersion = v;
        inspector.clear();
    }

    for (auto& lane : lanes)
        lane->refresh (live);

    for (auto& lfo : lfos)
        lfo->refresh (live);

    if (const bool running = proc.transportRunning.load(); running != wasRunning)
    {
        wasRunning = running;
        repaint (0, 0, getWidth(), juce::roundToInt (50 * uiScale));
    }
}

void ModLanesEditor::paint (juce::Graphics& g)
{
    // Grain and astrolabe are rendered once per size / scale and then just copied.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int width = juce::roundToInt ((float) getWidth() * scale), height = juce::roundToInt ((float) getHeight() * scale);
    if (background.getWidth() != width || background.getHeight() != height)
    {
        // Software images: the window uses the software renderer, so GPU images would need reading back.
        background = juce::Image (juce::Image::RGB, juce::jmax (1, width), juce::jmax (1, height), false, theme::cacheImageType());
        juce::Graphics bg (background);
        bg.addTransform (juce::AffineTransform::scale (scale));
        bg.fillAll (theme::bg);
        bg.setTiledImageFill (grain, 0, 0, 1.0f);
        bg.fillAll();
        theme::drawAstrolabeUnder (bg, *this, 0.7f);
    }
    g.drawImageTransformed (background, juce::AffineTransform::scale (1.0f / scale));

    g.addTransform (juce::AffineTransform::scale (uiScale));
    auto header = logicalBounds().reduced (14, 10).removeFromTop (34);

    g.setColour (theme::gold);
    g.setFont (theme::font (20.0f, true).withExtraKerningFactor (0.12f));
    g.drawText ("ORRERY", header.removeFromLeft (150), juce::Justification::centredLeft);

    if (mapContext.isMapping())
    {
        const auto colour = theme::lfoColour (mapContext.activeLfo);
        auto banner = header.removeFromLeft (620).reduced (0, 4);
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (banner.toFloat(), 5.0f);
        g.setColour (colour);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("Mapping LFO " + juce::String (mapContext.activeLfo + 1)
                        + ": click an orbit's Depth, Offset or Rate, a block, or a knob of the selected module (Esc cancels)",
                    banner.reduced (10, 0), juce::Justification::centredLeft);
        return;
    }

    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    g.drawText ("orbits of envelopes & LFOs  ->  MIDI CC", header.removeFromLeft (320), juce::Justification::centredLeft);

    const bool running = proc.transportRunning.load();
    g.setColour (running ? juce::Colour (0xff81c784) : theme::textDim);
    g.drawText (running ? "RUNNING" : "STOPPED", header.removeFromLeft (80), juce::Justification::centredLeft);
}

juce::Rectangle<int> ModLanesEditor::logicalBounds() const
{
    return { juce::roundToInt ((float) getWidth() / uiScale), juce::roundToInt ((float) getHeight() / uiScale) };
}

void ModLanesEditor::setUiScale (float scale)
{
    const auto logical = logicalBounds();
    uiScale = scale;

    proc.apvts.state.setProperty ("uiScale", scale, nullptr);
    globalSettings().setValue ("uiScale", (double) scale);
    globalSettings().saveIfNeeded();

    setResizeLimits (juce::roundToInt (1200 * scale), juce::roundToInt (840 * scale),
                     juce::roundToInt (2400 * scale), juce::roundToInt (1500 * scale));
    setSize (juce::roundToInt ((float) logical.getWidth() * scale), juce::roundToInt ((float) logical.getHeight() * scale));
    resized();
    repaint();
}

void ModLanesEditor::showScaleMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Interface size");
    int id = 1;
    for (float scale : kScales)
        menu.addItem (id++, juce::String (juce::roundToInt (scale * 100.0f)) + "%", true, std::abs (scale - uiScale) < 0.01f);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<ModLanesEditor> (this)] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->setUiScale (kScales[result - 1]);
                        });
}

void ModLanesEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    // Only empty space: the window background and panel backgrounds (not controls,
    // grids or the Steps editor, which have their own right-click menus).
    auto* c = e.eventComponent;
    const bool emptySpace = c == this || dynamic_cast<LaneStrip*> (c) != nullptr || dynamic_cast<LfoPanel*> (c) != nullptr
                         || dynamic_cast<ModulePalette*> (c) != nullptr || dynamic_cast<SceneBar*> (c) != nullptr
                         || dynamic_cast<OrreryView*> (c) != nullptr || dynamic_cast<PhraseIndicator*> (c) != nullptr
                         || (c == &inspector && ! inspector.getPreviewArea().contains (e.getEventRelativeTo (&inspector).getPosition()));
    if (emptySpace)
        showScaleMenu();
}

void ModLanesEditor::resized()
{
    auto r = logicalBounds().reduced (12, 10);

    auto header = r.removeFromTop (34);
    thruButton.setBounds (header.removeFromRight (100).withSizeKeepingCentre (100, 24));
    header.removeFromRight (8);
    freeRunButton.setBounds (header.removeFromRight (150).withSizeKeepingCentre (150, 24));
    header.removeFromRight (8);
    liveButton.setBounds (header.removeFromRight (100).withSizeKeepingCentre (100, 24));
    header.removeFromRight (20);
    phrase.setBounds (header.removeFromRight (250).withSizeKeepingCentre (250, 30));
    r.removeFromTop (6);
    sceneBar.setBounds (r.removeFromTop (26));
    r.removeFromTop (10);

    auto bottom = r.removeFromBottom (272);
    r.removeFromBottom (8);

    auto leftColumn = r.removeFromLeft (160);
    orrery.setBounds (leftColumn.removeFromBottom (196));
    leftColumn.removeFromBottom (8);
    palette.setBounds (leftColumn);
    r.removeFromLeft (8);

    const int gap = 8;
    const int laneHeight = (r.getHeight() - gap * (kNumLanes - 1)) / kNumLanes;
    for (auto& lane : lanes)
    {
        lane->setBounds (r.removeFromTop (laneHeight));
        r.removeFromTop (gap);
    }

    inspector.setBounds (bottom.removeFromLeft (560));
    bottom.removeFromLeft (gap);
    const int lfoWidth = (bottom.getWidth() - gap * (kNumLfos - 1)) / kNumLfos;
    for (auto& lfo : lfos)
    {
        lfo->setBounds (bottom.removeFromLeft (lfoWidth));
        bottom.removeFromLeft (gap);
    }

    // Everything above was laid out at 100%; scale it to the chosen interface size.
    // (The tooltip window and JUCE's resize corner position themselves in window pixels.)
    for (auto* child : getChildren())
        if (dynamic_cast<juce::TooltipWindow*> (child) == nullptr
            && dynamic_cast<juce::ResizableCornerComponent*> (child) == nullptr
            && dynamic_cast<juce::ResizableBorderComponent*> (child) == nullptr)
            child->setTransform (juce::AffineTransform::scale (uiScale));
}
