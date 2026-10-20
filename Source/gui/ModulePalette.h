#pragma once

#include "Theme.h"
#include "UserModules.h"

namespace modlanes
{

// A draggable module tile: either a built-in module type or a saved user module.
// Drag descriptions are "module:<type index>" or "userfile:<path>".
class PaletteItem : public juce::Component
{
public:
    explicit PaletteItem (BlockType t) : prototype (Block::make (t, 0, 1)), name (getTypeInfo (t).name),
                                         description ("module:" + juce::String ((int) t)) {}

    PaletteItem (const Block& b, const juce::File& f)
        : prototype (b), name (f.getFileNameWithoutExtension()), description ("userfile:" + f.getFullPathName()), file (f) {}

    std::function<void (const juce::File&)> onDelete;

    void paint (juce::Graphics& g) override
    {
        const auto colour = file.exists() ? theme::gold : theme::text.withAlpha (0.8f);
        auto r = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (isMouseOver() ? theme::panelLight.brighter (0.06f) : theme::panelLight);
        g.fillRoundedRectangle (r, 4.0f);
        r.removeFromLeft (4.0f);

        auto icon = r.removeFromRight (40.0f).reduced (4.0f, 5.0f);
        g.setColour (theme::gridLine);
        g.drawHorizontalLine ((int) icon.getCentreY(), icon.getX(), icon.getRight());
        g.setColour (colour);
        g.strokePath (theme::makeBlockCurve (prototype, icon, 40), juce::PathStrokeType (1.4f));

        g.setColour (file.exists() ? theme::gold.brighter (0.2f) : theme::text);
        g.setFont (theme::font (12.0f));
        g.drawFittedText (name, r.withTrimmedLeft (8.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || ! file.exists())
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "Delete module \"" + name + "\"");
        menu.addItem (2, "Show in folder");
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                            [safe = juce::Component::SafePointer<PaletteItem> (this)] (int result)
                            {
                                if (safe == nullptr)
                                    return;
                                if (result == 1 && safe->onDelete)
                                    safe->onDelete (safe->file);
                                else if (result == 2)
                                    safe->file.revealToUser();
                            });
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 4)
            return;

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! container->isDragAndDropActive())
                container->startDragging (description, this);
    }

private:
    const Block prototype;
    const juce::String name;
    const juce::String description;
    const juce::File file;
};

// Built-in modules plus the user's saved modules, in a scrolling list.
class ModulePalette : public juce::Component
{
public:
    ModulePalette()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);
        refreshUserModules();
    }

    // Re-reads Documents/ModLanes/Modules (after saving or deleting a module).
    void refreshUserModules()
    {
        content.removeAllChildren();
        builtIn.clear();
        user.clear();

        for (int t = 0; t < kNumBlockTypes; ++t)
            if ((BlockType) t != BlockType::Curve) // curves only come from user modules
                content.addAndMakeVisible (builtIn.add (new PaletteItem ((BlockType) t)));

        for (const auto& file : usermodules::list())
        {
            Block b;
            if (usermodules::load (file, b))
            {
                auto* item = user.add (new PaletteItem (b, file));
                item->onDelete = [this] (const juce::File& f)
                {
                    f.moveToTrash();
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ModulePalette> (this)]
                                                     { if (safe != nullptr) safe->refreshUserModules(); });
                };
                content.addAndMakeVisible (item);
            }
        }

        layoutContent();
    }

    void paint (juce::Graphics& g) override
    {
        theme::paintPanel (g, getLocalBounds().toFloat());
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f, true));
        g.drawText ("MODULES", getLocalBounds().reduced (10, 6).removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (theme::font (10.0f));
        g.drawFittedText ("Drag onto an orbit.\nRight-click a block to save it.",
                          getLocalBounds().reduced (10, 8).removeFromBottom (28), juce::Justification::centredLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        r.removeFromTop (22);
        r.removeFromBottom (32);
        viewport.setBounds (r);
        layoutContent();
    }

private:
    // Content shows a "USER" heading above the saved modules.
    struct Content : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            if (userTop < 0)
                return;

            const auto line = juce::Rectangle<int> (0, userTop, getWidth(), 20);
            g.setColour (theme::gold.withAlpha (0.8f));
            g.setFont (theme::font (10.5f, true));
            g.drawText ("USER", line.withTrimmedLeft (4), juce::Justification::centredLeft);
            g.setColour (theme::gold.withAlpha (0.25f));
            g.drawHorizontalLine (line.getCentreY(), 40.0f, (float) getWidth() - 4.0f);
        }

        int userTop = -1;
    };

    void layoutContent()
    {
        constexpr int rowHeight = 26;
        const int rows = builtIn.size() + user.size();
        const int totalHeight = rows * rowHeight + (user.isEmpty() ? 0 : 26);
        const int width = viewport.getWidth() - (totalHeight > viewport.getHeight() ? 8 : 0);
        int y = 0;

        for (auto* item : builtIn)
        {
            item->setBounds (0, y, width, rowHeight - 2);
            y += rowHeight;
        }

        content.userTop = user.isEmpty() ? -1 : y + 4;
        if (! user.isEmpty())
            y += 26;

        for (auto* item : user)
        {
            item->setBounds (0, y, width, rowHeight - 2);
            y += rowHeight;
        }

        content.setSize (juce::jmax (1, width), y);
        content.repaint();
    }

    juce::Viewport viewport;
    Content content;
    juce::OwnedArray<PaletteItem> builtIn, user;
};

} // namespace modlanes
