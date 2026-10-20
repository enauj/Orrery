#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "StateIO.h"

// User modules: single blocks (any module with its settings, or a drawn curve)
// saved as files in Documents/ModLanes/Modules and shown in the palette.
namespace modlanes::usermodules
{

inline const juce::String extension { ".mlmodule" };
inline const juce::Identifier moduleId { "MODULE" };

inline juce::File folder()
{
    // Documents/Orrery (the plugin was called ModLanes before; its folder is moved over once).
    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    if (const auto old = documents.getChildFile ("ModLanes"); old.isDirectory() && ! documents.getChildFile ("Orrery").exists())
        old.moveFileTo (documents.getChildFile ("Orrery"));
    auto dir = documents.getChildFile ("Orrery").getChildFile ("Modules");
    dir.createDirectory();
    return dir;
}

inline juce::Array<juce::File> list()
{
    auto files = folder().findChildFiles (juce::File::findFiles, false, "*" + extension);
    files.sort();
    return files;
}

inline bool save (const Block& block, const juce::String& name)
{
    juce::ValueTree tree (moduleId);
    tree.setProperty (stateio::versionId, stateio::kVersion, nullptr);
    tree.appendChild (stateio::blockToTree (block), nullptr);

    const auto file = folder().getChildFile (juce::File::createLegalFileName (name.trim()) + extension);
    if (auto xml = tree.createXml())
        return xml->writeTo (file);
    return false;
}

// Returns false if the file is not a valid module.
inline bool load (const juce::File& file, Block& out)
{
    if (auto xml = juce::parseXML (file))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        const auto blockTree = tree.getChildWithName (stateio::blockId);
        if (tree.hasType (moduleId) && blockTree.isValid())
        {
            out = stateio::blockFromTree (blockTree, tree.getProperty (stateio::versionId, stateio::kVersion));
            out.id = 0;
            return true;
        }
    }
    return false;
}

// Asks for a name in a small modal dialog, then calls onName with it.
inline void askForName (juce::Component* parent, const juce::String& title, const juce::String& suggestion,
                        std::function<void (const juce::String&)> onName)
{
    auto* window = new juce::AlertWindow (title, "Name:", juce::MessageBoxIconType::NoIcon, parent);
    window->addTextEditor ("name", suggestion);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onName = std::move (onName)] (int result)
    {
        if (result == 1)
            if (const auto name = window->getTextEditorContents ("name").trim(); name.isNotEmpty())
                onName (name);
    }), true);
}

} // namespace modlanes::usermodules
