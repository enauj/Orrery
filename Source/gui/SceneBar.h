#pragma once

#include "Params.h"
#include "Presets.h"
#include "PluginProcessor.h"
#include "Theme.h"

namespace modlanes
{

// Transport (clock source, BPM, play), the 10 scene pads, scene quantise,
// master length and the preset menu.
class SceneBar : public juce::Component
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    explicit SceneBar (ModLanesProcessor& p) : proc (p)
    {
        auto& apvts = p.apvts;

        clock.addItemList (params::clockNames, 1);
        clock.setTooltip ("Host: follow the DAW transport. Internal: own tempo and start/stop.");
        quantize.addItemList (params::quantizeNames, 1);
        quantize.setTooltip ("When a scene change takes effect while playing");
        for (auto* c : { &clock, &quantize })
            addAndMakeVisible (*c);

        theme::setupBar (bpm, theme::accent);
        theme::setupBar (length, theme::accent);
        bpm.setTooltip ("Internal tempo (used when Clock is Internal)");
        length.setTooltip ("Alignment: every N bars all orbits and LFOs line up and restart together, however slow they are. "
                           "Scene changes can wait for it (Quantize). Off = every orbit loops on its own.");
        for (auto* s : { &bpm, &length })
            addAndMakeVisible (*s);

        play.setTooltip ("Start / stop the internal clock");
        addAndMakeVisible (play);

        presetsButton.setTooltip ("Load a preset into the current scene, or save the current scene");
        presetsButton.onClick = [this] { showPresetMenu (proc.getCurrentScene()); };
        addAndMakeVisible (presetsButton);

        for (int i = 0; i < ModLanesProcessor::kNumScenes; ++i)
        {
            auto* pad = pads.add (new ScenePad (*this, i));
            addAndMakeVisible (pad);
        }

        clockAttachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, params::clock, clock);
        quantizeAttachment = std::make_unique<APVTS::ComboBoxAttachment> (apvts, params::quantize, quantize);
        bpmAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, params::bpm, bpm);
        lengthAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, params::masterLength, length);
        playAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, params::play, play);

        bpm.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " BPM"; };
        bpm.updateText();
        length.textFromValueFunction = [] (double v) { return v < 0.5 ? juce::String ("Off") : juce::String ((int) v) + (v < 1.5 ? " bar" : " bars"); };
        length.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789").getDoubleValue(); };
        length.updateText();
    }

    void refresh()
    {
        const bool internal = proc.apvts.getRawParameterValue (params::clock)->load() >= 0.5f;
        bpm.setEnabled (internal);
        play.setEnabled (internal);
        bpm.setAlpha (internal ? 1.0f : 0.4f);
        play.setAlpha (internal ? 1.0f : 0.4f);

        for (auto* pad : pads)
            pad->update();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f, true));
        g.drawText ("SCENE", sceneLabel, juce::Justification::centredRight);
        g.drawText ("QUANTIZE", quantizeLabel, juce::Justification::centredRight);
        g.drawText ("ALIGNMENT", lengthLabel, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto take = [&r] (int w, int gapAfter = 6)
        {
            auto result = r.removeFromLeft (w);
            r.removeFromLeft (gapAfter);
            return result;
        };

        clock.setBounds (take (96));
        bpm.setBounds (take (96));
        play.setBounds (take (44, 22));

        sceneLabel = take (48, 8);
        for (auto* pad : pads)
            pad->setBounds (take (30, 4));
        r.removeFromLeft (14);

        quantizeLabel = take (66, 6);
        quantize.setBounds (take (112, 14));
        lengthLabel = take (74, 6);
        length.setBounds (take (86));

        presetsButton.setBounds (r.removeFromRight (110));
    }

private:
    struct ScenePad : public juce::Component, public juce::SettableTooltipClient
    {
        ScenePad (SceneBar& b, int i) : bar (b), index (i)
        {
            setTooltip ("Scene " + juce::String (i + 1) + ". Click to switch, right-click for copy / clear / presets.");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paint (juce::Graphics& g) override
        {
            auto& processor = bar.proc;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool current = processor.getCurrentScene() == index;
            const bool pending = processor.getPendingScene() == index && ! current;
            const bool used = processor.isSceneUsed (index);

            g.setColour (current ? theme::gold.withAlpha (0.85f) : theme::panelLight.brighter (isMouseOver() ? 0.08f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);

            if (pending)
            {
                // Blink while waiting for the quantise boundary.
                const bool on = (juce::Time::getMillisecondCounter() / 180) % 2 == 0;
                g.setColour (theme::text.withAlpha (on ? 0.9f : 0.3f));
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.6f);
            }

            g.setColour (current ? theme::bg : used ? theme::text : theme::textDim.withAlpha (0.6f));
            g.setFont (theme::font (12.0f, true));
            g.drawText (juce::String (index + 1), r, juce::Justification::centred);

            if (used && ! current)
            {
                g.setColour (theme::text.withAlpha (0.5f));
                g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ r.getCentreX(), r.getBottom() - 4.0f }));
            }
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        // Repaints only when the pad's state changes (or while it blinks).
        void update()
        {
            auto& processor = bar.proc;
            const bool current = processor.getCurrentScene() == index;
            const bool pending = processor.getPendingScene() == index && ! current;
            const bool blinkOn = pending && (juce::Time::getMillisecondCounter() / 180) % 2 == 0;
            const int key = (current ? 1 : 0) | (pending ? 2 : 0) | (blinkOn ? 4 : 0) | (processor.isSceneUsed (index) ? 8 : 0);
            if (key != shownKey)
            {
                shownKey = key;
                repaint();
            }
        }

        int shownKey = -1;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                bar.showPadMenu (index);
            else
                bar.proc.requestScene (index);
        }

        SceneBar& bar;
        const int index;
    };

    void showPadMenu (int scene)
    {
        juce::PopupMenu menu;
        const bool current = scene == proc.getCurrentScene();
        menu.addItem (1, "Copy current scene here", ! current);
        menu.addItem (2, "Clear scene");
        menu.addSeparator();
        menu.addSubMenu ("Load preset into scene " + juce::String (scene + 1), buildPresetList (1000));
        menu.addItem (3, "Save scene as preset...", current);

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safe = juce::Component::SafePointer<SceneBar> (this), scene] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                if (result == 1)          safe->proc.copyCurrentSceneTo (scene);
                                else if (result == 2)     safe->proc.clearScene (scene);
                                else if (result == 3)     safe->askAndSavePreset();
                                else if (result >= 1000)  safe->loadPreset (result - 1000, scene);
                            });
    }

    void showPresetMenu (int scene)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Load into scene " + juce::String (scene + 1));
        menu.addSubMenu ("Factory", buildFactoryList (1000));

        const auto user = presets::userPresets();
        juce::PopupMenu userMenu;
        for (int i = 0; i < user.size(); ++i)
            userMenu.addItem (2000 + i, user[i].getFileNameWithoutExtension());
        menu.addSubMenu ("User", userMenu, ! user.isEmpty());

        menu.addSeparator();
        menu.addItem (1, "Save current scene as preset...");
        menu.addItem (2, "Open preset folder");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton),
                            [safe = juce::Component::SafePointer<SceneBar> (this), scene] (int result)
                            {
                                if (safe == nullptr || result == 0)
                                    return;
                                if (result == 1)       safe->askAndSavePreset();
                                else if (result == 2)  presets::folder().startAsProcess();
                                else                   safe->loadPreset (result - 1000, scene);
                            });
    }

    // Ids: base + i for factory presets, base + 1000 + i for user presets.
    juce::PopupMenu buildPresetList (int base)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Factory");
        for (int i = 0; i < factoryNames().size(); ++i)
            menu.addItem (base + i, factoryNames()[i]);

        const auto user = presets::userPresets();
        if (! user.isEmpty())
        {
            menu.addSectionHeader ("User");
            for (int i = 0; i < user.size(); ++i)
                menu.addItem (base + 1000 + i, user[i].getFileNameWithoutExtension());
        }
        return menu;
    }

    juce::PopupMenu buildFactoryList (int base)
    {
        juce::PopupMenu menu;
        for (int i = 0; i < factoryNames().size(); ++i)
            menu.addItem (base + i, factoryNames()[i]);
        return menu;
    }

    const juce::StringArray& factoryNames()
    {
        if (factoryNameCache.isEmpty())
            for (const auto& [name, tree] : presets::factory (proc))
                factoryNameCache.add (name);
        return factoryNameCache;
    }

    void loadPreset (int code, int scene)
    {
        juce::ValueTree data;
        if (code < 1000)
        {
            const auto list = presets::factory (proc);
            if (code >= 0 && code < (int) list.size())
                data = list[(size_t) code].second;
        }
        else
        {
            const auto user = presets::userPresets();
            if (code - 1000 < user.size())
                data = presets::load (user[code - 1000]);
        }

        if (data.isValid())
            proc.setSceneData (scene, data);
    }

    void askAndSavePreset()
    {
        auto* window = new juce::AlertWindow ("Save preset", "Name for the current scene:", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", "Scene " + juce::String (proc.getCurrentScene() + 1));
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<SceneBar> (this), window] (int result)
            {
                if (safe != nullptr && result == 1)
                    if (const auto name = window->getTextEditorContents ("name").trim(); name.isNotEmpty())
                        presets::save (safe->proc.captureScene(), name);
            }), true);
    }

    ModLanesProcessor& proc;

    juce::ComboBox clock, quantize;
    juce::Slider bpm, length;
    // Play / stop with transport symbols: a triangle when stopped, a square while playing.
    struct TransportButton : public juce::Button
    {
        TransportButton() : juce::Button ("Play") { setClickingTogglesState (true); }

        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool playing = getToggleState();
            const juce::Colour green (0xff81c784);

            g.setColour (playing ? green.withAlpha (0.3f) : theme::panelLight.brighter (over ? 0.08f : 0.0f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (playing ? green : theme::gridStrong);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);

            const auto icon = r.withSizeKeepingCentre (11.0f, 11.0f);
            g.setColour (playing ? green.brighter (0.4f) : theme::text);
            if (playing)
            {
                g.fillRoundedRectangle (icon.reduced (0.5f), 1.5f);
            }
            else
            {
                juce::Path triangle;
                triangle.addTriangle (icon.getX() + 1.5f, icon.getY(), icon.getX() + 1.5f, icon.getBottom(),
                                      icon.getRight() + 0.5f, icon.getCentreY());
                g.fillPath (triangle);
            }
        }
    };

    TransportButton play;
    juce::TextButton presetsButton { "Presets" };
    juce::OwnedArray<ScenePad> pads;
    juce::Rectangle<int> sceneLabel, quantizeLabel, lengthLabel;
    juce::StringArray factoryNameCache;

    std::unique_ptr<APVTS::ComboBoxAttachment> clockAttachment, quantizeAttachment;
    std::unique_ptr<APVTS::SliderAttachment> bpmAttachment, lengthAttachment;
    std::unique_ptr<APVTS::ButtonAttachment> playAttachment;
};

} // namespace modlanes
