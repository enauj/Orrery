#pragma once

#include "ModuleTypes.h"

#include <functional>

namespace modlanes
{

// Shared "map mode" state: after clicking an LFO's Map button, the next click
// on a lane control or a block becomes that LFO's modulation target.
struct MapContext
{
    int activeLfo = -1;
    std::function<void (const ModRoute&)> pick;

    bool isMapping() const { return activeLfo >= 0; }
};

} // namespace modlanes
