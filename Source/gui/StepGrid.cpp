#include "StepGrid.h"
#include "LiveState.h"
#include "Params.h"

namespace modlanes
{

namespace
{
float snapCycles (BlockType type, float raw, bool fine)
{
    if (getTypeInfo (type).integerCycles)
        return juce::jlimit (1.0f, kMaxCycles, std::round (raw));

    const float snapped = fine ? std::round (raw * 100.0f) / 100.0f
                               : (raw < 1.0f ? std::round (raw * 4.0f) / 4.0f : std::round (raw));
    return juce::jlimit (0.25f, kMaxCycles, snapped);
}

juce::String cyclesText (const Block& b)
{
    if (getTypeInfo (b.type).integerCycles)
        return "x" + juce::String ((int) std::round (b.cycles));

    const bool whole = std::abs (b.cycles - std::round (b.cycles)) < 0.001f;
    return "x" + (whole ? juce::String ((int) std::round (b.cycles)) : juce::String (b.cycles, 2));
}

bool sameShape (const Block& a, const Block& b)
{
    return a.type == b.type && a.start == b.start && a.length == b.length && a.amount == b.amount
        && a.cycles == b.cycles && a.fadeIn == b.fadeIn && a.fadeOut == b.fadeOut && a.p == b.p;
}
} // namespace

StepGrid::StepGrid (ModLanesProcessor& p, int laneIndex, MapContext& context)
    : proc (p), lane (laneIndex), mapContext (context),
      stepsParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "steps"))),
      modeParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "mode"))),
      blendParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "blend"))),
      pointsParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "points"))),
      onParam (p.apvts.getRawParameterValue (params::lane (laneIndex, "on")))
{
    setWantsKeyboardFocus (true);
    setOpaque (true); // the lane strip behind it never needs repainting for the playhead

    renderButton.setTooltip ("Replace the points with the current block layout");
    renderButton.onClick = [this] { renderFromBlocks(); };
    addChildComponent (renderButton);

    reloadFromProcessor();
}

int StepGrid::mode() const { return (int) modeParam->load(); }

float StepGrid::livePosition() const
{
    return modlanes::livePosition (proc, lane, numSteps(), mode() == params::Sequence);
}

// The value the dot sits on: worked out from the pattern at the extrapolated
// position in Sequence mode, so it glides along the curve.
float StepGrid::liveValue (float position) const
{
    if (mode() != params::Sequence)
        return proc.laneRaw[lane].load();

    // Blocks the LFOs move play their modulated copy, so the dot rides the live curve.
    const auto live = [this] (const Block& b)
    {
        for (const auto& l : liveBlocks)
            if (l.block.id == b.id)
                return unmodulated (l.block);
        return unmodulated (b);
    };
    return pointsView() ? evalPoints (pattern.envelope, numSteps(), position)
                        : laneValueAt (pattern, numSteps(), position, blendParam->load(), live);
}

void StepGrid::animateModulation()
{
    liveMod.update();

    std::array<float, kNumLfos> outputs {};
    for (int j = 0; j < kNumLfos; ++j)
        outputs[(size_t) j] = proc.lfoOutput[j].load();

    const bool show = liveView && ! pointsView() && mode() != params::AudioFollow
                   && onParam->load() >= 0.5f && liveMod.touchesBlocks (lane);

    // Nothing to redraw while the LFOs and the pattern stand still.
    const std::array<int, 4> version { proc.getPatternVersion(), proc.getRoutingVersion(), numSteps(), getWidth() };
    if (show && outputs == shownLfoOutputs && version == shownLiveVersion && ! liveBlocks.empty())
        return;
    if (! show && liveBlocks.empty())
        return;

    // A moving curve repaints most of a block, so it follows the LFOs at about 30 frames
    // per second (like the LFO waves); the playhead still moves every frame.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (show && version == shownLiveVersion && ! liveBlocks.empty() && now - lastLiveCurveMs < 30.0)
        return;
    lastLiveCurveMs = now;

    shownLfoOutputs = outputs;
    shownLiveVersion = version;
    liveBlocks.clear();
    juce::Rectangle<int> area;

    if (show)
        for (const auto& b : pattern.blocks)
        {
            if (b.start >= numSteps())
                continue;

            LiveBlock live { b, 0 };
            float ampMod = 0.0f;
            if (liveMod.modulate (lane, live.block, ampMod, live.lfo))
            {
                // The outlines are built here once per frame (paint() runs for every
                // playhead move too), with a point every 2 px and plain joints; only the
                // strip the curve covers (old and new) repaints, not the whole block.
                live.block.amount = juce::jlimit (-1.0f, 1.0f, live.block.amount + ampMod);
                const auto geo = geometryFor (live.block);
                const int resolution = juce::jlimit (16, 300, juce::jmax ((int) geo.curve.getWidth() / 2, (int) (live.block.cycles * 12.0f)));
                const auto curve = theme::makeBlockCurve (live.block, geo.curve, resolution);
                juce::PathStrokeType (4.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (live.glow, curve);
                juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (live.line, curve);
                area = area.getUnion (live.glow.getBounds().expanded (1.0f).getSmallestIntegerContainer()
                                          .getIntersection (geo.body.toNearestInt()));
                liveBlocks.push_back (std::move (live));
            }
        }

    repaint (liveBlockArea.getUnion (area));
    liveBlockArea = area;
}

// The modulated curves, in the colour of the LFO that moves them most.
void StepGrid::paintLiveBlocks (juce::Graphics& g)
{
    for (const auto& live : liveBlocks)
    {
        const auto geo = geometryFor (live.block);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (geo.body.toNearestInt());

        const auto colour = theme::lfoColour (live.lfo);
        g.setColour (colour.withAlpha (0.25f));
        g.fillPath (live.glow);
        g.setColour (colour);
        g.fillPath (live.line);
    }
}

void StepGrid::animate()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.1, (now - lastAnimationMs) / 1000.0);
    lastAnimationMs = now;

    const float target = liveView ? livePosition() : -1.0f;
    const double steps = numSteps();
    const bool wraps = mode() == params::Sequence;

    if (target < 0.0f)
    {
        shownPosition = -1.0;
    }
    else if (shownPosition < 0.0)
    {
        shownPosition = target;
    }
    else
    {
        // Move at the orbit's speed, then correct a little towards the report.
        double next = shownPosition + dt * proc.laneSpeed[lane].load();
        double error = target - next;
        if (wraps)
        {
            next = std::fmod (next, steps);
            error = std::remainder (target - next, steps); // shortest way round the loop
        }

        // Seeks, loop jumps and retriggers snap; small drift is eased out.
        shownPosition = std::abs (error) > 1.0 ? (double) target : next + error * 0.12;
        if (wraps)
            shownPosition = std::fmod (shownPosition + steps, steps);
    }

   #if MODLANES_PAINT_LOG
    if (animateLog)
        animateLog (lane, now, shownPosition);
   #endif

    if (mode() == params::AudioFollow)
    {
        repaint();
        return;
    }

    // The highlighted step cell repaints only when the playhead enters another step;
    // in between, only a narrow band around the line and dot (old and new spot).
    juce::Rectangle<int> cell, band;
    if (const float position = (float) shownPosition; position >= 0.0f)
    {
        const float sw = stepWidth();
        const int cellX = (int) ((float) (int) position * sw);
        cell = { cellX - 1, 0, (int) std::ceil (sw) + 2, getHeight() };
        band = { (int) (position * sw) - 8, 0, 16, getHeight() };
    }

    if (cell != lastLiveArea)
    {
        repaint (lastLiveArea.getUnion (cell));
        lastLiveArea = cell;
    }
    if (shownPosition != lastPaintedPosition)
    {
        repaint (lastLiveBand.getUnion (band));
        lastLiveBand = band;
        lastPaintedPosition = shownPosition;
    }
}

void StepGrid::poll()
{
    // Switching to Points view the first time renders the blocks into points.
    if (const bool points = pointsView(); points != shownPointsView)
    {
        shownPointsView = points;
        renderButton.setVisible (points);
        selected = hovered = selectedPoint = hoveredPoint = hoveredSegment = -1;
        if (onSelect)
            onSelect (lane, -1);

        if (points && pattern.envelope.numPoints == 0)
            renderFromBlocks();
    }

    if (liveView && mode() == params::AudioFollow)
    {
        trace[(size_t) tracePos] = proc.laneRaw[lane].load();
        tracePos = (tracePos + 1) % (int) trace.size();
    }

    if (learnBlockId != 0 && proc.lastNoteSerial.load() != learnSerial)
    {
        if (const int index = pattern.findById (learnBlockId); index >= 0)
        {
            pattern.blocks[(size_t) index].note = proc.lastNoteNumber.load();
            commit();
            if (index == selected)
                notifyEdited();
        }

        learnBlockId = 0;
        repaint();
    }
}

int StepGrid::numSteps() const { return juce::jlimit (1, kMaxSteps, (int) stepsParam->load()); }
float StepGrid::stepWidth() const { return (float) getWidth() / (float) numSteps(); }
int StepGrid::stepAt (float x) const { return juce::jlimit (0, numSteps() - 1, (int) (x / stepWidth())); }

int StepGrid::blockAtStep (int step) const
{
    const int index = pattern.findBlockAt (step);
    return index >= 0 && pattern.blocks[(size_t) index].start < numSteps() ? index : -1;
}

StepGrid::Geometry StepGrid::geometryFor (const Block& b) const
{
    const float sw = stepWidth();
    const int visibleLength = juce::jmin (b.length, numSteps() - b.start);

    Geometry g;
    g.body = juce::Rectangle<float> ((float) b.start * sw, 0.0f, (float) visibleLength * sw, (float) getHeight()).reduced (1.5f, 3.0f);
    g.curve = juce::Rectangle<float> ((float) b.start * sw, curveTop(), (float) b.length * sw, curveBottom() - curveTop()).reduced (3.0f, 0.0f);

    const float mid = g.curve.getCentreY(), half = g.curve.getHeight() * 0.5f;
    const float left = g.body.getX() + 5.0f, right = g.body.getRight() - 5.0f;

    g.amp = { g.body.getCentreX(), juce::jlimit (g.body.getY() + 18.0f, g.body.getBottom() - 20.0f, mid - b.amount * half) };
    g.fadeIn = { juce::jlimit (left, right, g.curve.getX() + b.fadeIn * g.curve.getWidth()), g.body.getY() + 7.0f };
    g.fadeOut = { juce::jlimit (left, right, g.curve.getRight() - b.fadeOut * g.curve.getWidth()), g.body.getY() + 7.0f };
    g.cycles = juce::Rectangle<float> (30.0f, 13.0f).withCentre ({ g.body.getCentreX(), g.body.getBottom() - 9.0f });
    return g;
}

StepGrid::Handle StepGrid::handleAt (int index, juce::Point<float> p) const
{
    if (index < 0)
        return Handle::None;

    const auto g = geometryFor (pattern.blocks[(size_t) index]);

    if (index == selected || index == hovered)
    {
        if (g.cycles.expanded (2.0f).contains (p))   return Handle::Cycles;
        if (p.getDistanceFrom (g.fadeIn) < 7.0f)     return Handle::FadeIn;
        if (p.getDistanceFrom (g.fadeOut) < 7.0f)    return Handle::FadeOut;
        if (p.getDistanceFrom (g.amp) < 8.0f)        return Handle::Amp;
    }

    if (p.x > g.body.getRight() - 7.0f)                          return Handle::ResizeRight;
    if (p.x < g.body.getX() + 7.0f && g.body.getWidth() > 24.0f) return Handle::ResizeLeft;
    return Handle::Body;
}

//==============================================================================
StepGrid::StaticKey StepGrid::makeStaticKey (float scale) const
{
    return { proc.getPatternVersion(), proc.getRoutingVersion(), selected, hovered, (int) hoveredHandle, hoverStep,
             mapContext.activeLfo, numSteps(), mode(), pointsView() ? 1 : 0, selectedPoint, hoveredPoint, hoveredSegment,
             learnBlockId, getWidth(), getHeight(), blendParam->load(), scale, hasKeyboardFocus (false), onParam->load() >= 0.5f };
}

void StepGrid::paint (juce::Graphics& g)
{
   #if MODLANES_PAINT_LOG
    struct Timed
    {
        double start = juce::Time::getMillisecondCounterHiRes();
        ~Timed() { paintStats.ms += juce::Time::getMillisecondCounterHiRes() - start; ++paintStats.calls; }
    } timed;
    paintStats.area += (double) g.getClipBounds().getWidth() * g.getClipBounds().getHeight();
   #endif
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto key = makeStaticKey (scale);

    if (! (key == staticKey) || ! staticLayer.isValid())
    {
       #if MODLANES_PAINT_LOG
        const double renderStart = juce::Time::getMillisecondCounterHiRes();
        juce::String reason;
        if (key.patternVersion != staticKey.patternVersion) reason << "pattern ";
        if (key.routingVersion != staticKey.routingVersion) reason << "routing ";
        if (key.hovered != staticKey.hovered || key.hoveredHandle != staticKey.hoveredHandle || key.hoverStep != staticKey.hoverStep) reason << "hover ";
        if (key.width != staticKey.width || key.height != staticKey.height || key.scale != staticKey.scale) reason << "size ";
        if (key.focus != staticKey.focus) reason << "focus ";
        if (key.blend != staticKey.blend || key.steps != staticKey.steps || key.mode != staticKey.mode || key.on != staticKey.on) reason << "params ";
        if (key.selected != staticKey.selected || key.selectedPoint != staticKey.selectedPoint) reason << "selection ";
       #endif
        staticLayer = juce::Image (juce::Image::RGB, // opaque: copied without blending
                                   juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                   juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), false, theme::cacheImageType());
        juce::Graphics layer (staticLayer);
        layer.addTransform (juce::AffineTransform::scale (scale));
        paintStatic (layer);
        staticKey = key;
       #if MODLANES_PAINT_LOG
        if (staticRenderLog)
            staticRenderLog (lane, juce::Time::getMillisecondCounterHiRes() - renderStart, reason);
       #endif
    }

    theme::drawCachedImage (g, staticLayer, juce::AffineTransform::scale (1.0f / scale));

    // Live layer: current step, follower trace, playhead and value dot.
    const float position = liveView ? (float) shownPosition : -1.0f;
   #if MODLANES_PAINT_LOG
    if (paintLog)
        paintLog (lane, juce::Time::getMillisecondCounterHiRes(), position);
   #endif
    const float h = (float) getHeight();
    const float sw = stepWidth();
    const float mid = (curveTop() + curveBottom()) * 0.5f;
    const auto laneColour = theme::laneColour (lane);

    if (liveView && mode() == params::AudioFollow)
        paintFollowerTrace (g);

    paintLiveBlocks (g);

    if (position >= 0.0f)
    {
        g.setColour (laneColour.withAlpha (0.06f));
        g.fillRect ((float) (int) position * sw, 0.0f, sw, h);

        const float x = position * sw;
        const float y = mid - juce::jlimit (-1.0f, 1.0f, liveValue (position)) * (curveBottom() - curveTop()) * 0.5f;

        g.setColour (theme::text.withAlpha (0.35f));
        g.drawVerticalLine ((int) x, 0.0f, h);

        g.setColour (laneColour.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x, y }));
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, y }));
    }
}

void StepGrid::paintStatic (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const int steps = numSteps();
    const float sw = stepWidth();
    const float h = bounds.getHeight();
    const float mid = (curveTop() + curveBottom()) * 0.5f;
    const auto laneColour = theme::laneColour (lane);

    g.fillAll (theme::panel); // matches the lane strip behind the rounded corners
    g.setColour (theme::panel.darker (0.3f));
    g.fillRoundedRectangle (bounds, 5.0f);

    for (int s = 0; s < steps; ++s)
    {
        const juce::Rectangle<float> cell ((float) s * sw, 0.0f, sw, h);

        if ((s / 4) % 2 == 1)
        {
            g.setColour (theme::panelLight.withAlpha (0.22f));
            g.fillRect (cell);
        }

        if (s == hoverStep)
        {
            g.setColour (theme::text.withAlpha (0.1f));
            g.fillRect (cell);
        }

        if (s > 0)
        {
            g.setColour (s % 4 == 0 ? theme::gridStrong.withAlpha (0.7f) : theme::gridLine.withAlpha (0.5f));
            g.drawVerticalLine ((int) cell.getX(), 0.0f, h);
        }

        if (s % 4 == 0)
        {
            g.setColour (theme::textDim.withAlpha (0.45f));
            g.setFont (theme::font (9.0f));
            g.drawText (juce::String (s + 1), cell.withTrimmedLeft (4.0f).removeFromBottom (12.0f), juce::Justification::left);
        }
    }

    g.setColour (theme::gridLine);
    g.drawHorizontalLine ((int) mid, 0.0f, bounds.getWidth());

    const int laneMode = mode();

    if (pointsView())
    {
        paintPoints (g);
    }
    else
    {
        std::array<LfoRouting, kNumLfos> routings;
        for (int j = 0; j < kNumLfos; ++j)
            routings[(size_t) j] = proc.getRouting (j);

        for (int i = 0; i < pattern.numBlocks; ++i)
            if (pattern.blocks[(size_t) i].start < steps)
                paintBlock (g, i, routings);

        if (laneMode == params::Sequence)
            paintBlendedCurve (g);
    }

    if (laneMode == params::AudioFollow)
    {
        // Blocks are not used while following audio; show the level instead.
        g.setColour (theme::panel.darker (0.3f).withAlpha (0.8f));
        g.fillRoundedRectangle (bounds, 5.0f);
    }

    if (laneMode != params::Sequence)
    {
        g.setColour (theme::textDim.withAlpha (0.8f));
        g.setFont (theme::font (9.5f, true));
        g.drawText (params::modeNames[laneMode].toUpperCase(), bounds.reduced (8.0f, 4.0f).removeFromTop (12.0f),
                    juce::Justification::topRight);
    }

    if (onParam->load() < 0.5f)
    {
        g.setColour (theme::panel.withAlpha (0.65f));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f, true));
        g.drawText ("ORBIT OFF", bounds, juce::Justification::centred);
    }

    if (hasKeyboardFocus (false))
    {
        g.setColour (laneColour.withAlpha (0.3f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
    }
}

void StepGrid::paintBlock (juce::Graphics& g, int index, const std::array<LfoRouting, kNumLfos>& routings)
{
    const auto& b = pattern.blocks[(size_t) index];
    const auto geo = geometryFor (b);
    const auto colour = theme::laneColour (lane);
    const bool isSelected = index == selected, isHovered = index == hovered;
    const bool mapping = mapContext.isMapping();
    const float mid = geo.curve.getCentreY(), half = geo.curve.getHeight() * 0.5f;

    g.setColour (colour.withAlpha (isSelected ? 0.15f : isHovered ? 0.1f : 0.06f));
    g.fillRoundedRectangle (geo.body, 5.0f);

    if (mapping)
    {
        const auto mapColour = theme::lfoColour (mapContext.activeLfo);
        g.setColour (mapColour.withAlpha (isHovered ? 0.25f : 0.1f));
        g.fillRoundedRectangle (geo.body, 5.0f);
        g.setColour (mapColour.withAlpha (0.9f));
        g.drawRoundedRectangle (geo.body, 5.0f, isHovered ? 2.0f : 1.0f);
    }
    else
    {
        // All blocks share the lane colour, so a faint outline keeps neighbours apart.
        g.setColour (colour.withAlpha (isSelected ? 0.85f : isHovered ? 0.45f : 0.18f));
        g.drawRoundedRectangle (geo.body, 5.0f, isSelected ? 1.5f : 1.0f);
    }

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (geo.body.toNearestInt());

        const int resolution = juce::jlimit (32, 1500, juce::jmax ((int) geo.curve.getWidth(), (int) (b.cycles * 32.0f)));
        const auto curve = theme::makeBlockCurve (b, geo.curve, resolution);

        auto fill = curve;
        fill.lineTo (geo.curve.getRight(), mid);
        fill.lineTo (geo.curve.getX(), mid);
        fill.closeSubPath();

        juce::ColourGradient gradient (colour.withAlpha (0.35f), 0.0f, geo.curve.getY(),
                                       colour.withAlpha (0.35f), 0.0f, geo.curve.getBottom(), false);
        gradient.addColour (0.5, colour.withAlpha (0.02f));
        g.setGradientFill (gradient);
        g.fillPath (fill);

        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Amplitude envelope (amount x fades), mirrored, while the block is in focus.
        if ((isSelected || isHovered) && ! mapping)
        {
            juce::Path upper, lower;
            const int n = juce::jmax (16, (int) geo.curve.getWidth() / 3);
            for (int k = 0; k <= n; ++k)
            {
                const float t = (float) k / (float) n;
                const float a = std::abs (b.amount) * fadeGain (b, std::min (t, 0.9999f)) * half;
                const float x = geo.curve.getX() + t * geo.curve.getWidth();
                if (k == 0) { upper.startNewSubPath (x, mid - a); lower.startNewSubPath (x, mid + a); }
                else        { upper.lineTo (x, mid - a);          lower.lineTo (x, mid + a); }
            }

            const float dashes[] = { 3.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, upper, dashes, 2);
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, lower, dashes, 2);
            g.setColour (colour.withAlpha (0.45f));
            g.fillPath (dashed);
        }
    }

    if (geo.body.getWidth() > 36.0f)
    {
        g.setColour (colour.withAlpha (0.75f));
        g.setFont (theme::font (9.0f, true));
        g.drawText (getTypeInfo (b.type).shortName, geo.body.reduced (6.0f, 4.0f).withHeight (12.0f),
                    juce::Justification::topLeft);
    }

    // LFO badges for blocks whose amplitude is modulated.
    float badgeX = geo.body.getRight() - 9.0f;
    for (int j = 0; j < kNumLfos; ++j)
    {
        const auto& routing = routings[(size_t) j];
        for (int r = 0; r < routing.numRoutes; ++r)
        {
            const auto& route = routing.routes[(size_t) r];
            if (route.targetsBlock() && route.lane == lane && route.blockId == b.id)
            {
                g.setColour (theme::lfoColour (j));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ badgeX, geo.body.getY() + 8.0f }));
                badgeX -= 10.0f;
            }
        }
    }

    if (mode() == params::MidiTrigger)
    {
        const bool learning = learnBlockId == b.id;
        const auto badge = juce::Rectangle<float> (learning ? 70.0f : 34.0f, 13.0f)
                               .withPosition (geo.body.getX() + 5.0f, geo.body.getBottom() - 18.0f);
        g.setColour (learning ? colour.withAlpha (0.5f) : theme::bg.withAlpha (0.8f));
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (b.note >= 0 || learning ? theme::text : theme::textDim);
        g.setFont (theme::font (9.5f, true));
        g.drawText (learning ? juce::String ("play a note") : params::noteName (b.note), badge, juce::Justification::centred);
    }

    if ((isSelected || isHovered) && ! mapping)
        paintHandles (g, b, geo, colour);
}

// White line wherever Blend makes the output differ from the blocks themselves.
void StepGrid::paintBlendedCurve (juce::Graphics& g)
{
    const float blend = blendParam->load();
    if (blend <= 0.0f || pattern.numBlocks == 0)
        return;

    const int steps = numSteps();
    const float sw = stepWidth();
    const float mid = (curveTop() + curveBottom()) * 0.5f, half = (curveBottom() - curveTop()) * 0.5f;
    const auto noModulation = [] (const Block& b) { return unmodulated (b); };

    juce::Path path;
    bool drawing = false;

    for (int x = 0; x <= getWidth(); x += 2)
    {
        const double pos = juce::jlimit (0.0, steps - 1.0e-4, (double) x / sw);
        const float blended = laneValueAt (pattern, steps, pos, blend, noModulation);
        const float plain = laneValueAt (pattern, steps, pos, 0.0f, noModulation);

        if (std::abs (blended - plain) > 0.002f)
        {
            const float y = mid - juce::jlimit (-1.0f, 1.0f, blended) * half;
            if (drawing)
                path.lineTo ((float) x, y);
            else
                path.startNewSubPath ((float) x, y);
            drawing = true;
        }
        else
        {
            drawing = false;
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.75f));
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void StepGrid::paintFollowerTrace (juce::Graphics& g)
{
    const int n = (int) trace.size();
    const float top = curveTop(), bottom = curveBottom();
    const auto colour = theme::laneColour (lane);

    juce::Path line;
    for (int i = 0; i < n; ++i)
    {
        const float v = juce::jlimit (0.0f, 1.0f, trace[(size_t) ((tracePos + i) % n)]);
        const float x = (float) getWidth() * (float) i / (float) (n - 1);
        const float y = bottom - v * (bottom - top);
        if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }

    auto fill = line;
    fill.lineTo ((float) getWidth(), bottom);
    fill.lineTo (0.0f, bottom);
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.4f), 0.0f, top, colour.withAlpha (0.03f), 0.0f, bottom, false));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (1.8f));
}

void StepGrid::paintHandles (juce::Graphics& g, const Block& b, const Geometry& geo, juce::Colour colour)
{
    const auto handleColour = [&] (Handle h) { return hoveredHandle == h ? juce::Colours::white : colour; };

    // Edge grips
    g.setColour (colour.withAlpha (0.7f));
    g.fillRoundedRectangle (geo.body.getRight() - 4.0f, geo.body.getCentreY() - 8.0f, 2.0f, 16.0f, 1.0f);
    if (geo.body.getWidth() > 24.0f)
        g.fillRoundedRectangle (geo.body.getX() + 2.0f, geo.body.getCentreY() - 8.0f, 2.0f, 16.0f, 1.0f);

    // Fade handles: small triangles pointing into the block
    const auto triangle = [] (juce::Point<float> c, bool pointRight)
    {
        juce::Path p;
        const float d = pointRight ? 1.0f : -1.0f;
        p.addTriangle (c.x - 4.0f * d, c.y - 4.5f, c.x - 4.0f * d, c.y + 4.5f, c.x + 4.0f * d, c.y);
        return p;
    };

    for (auto [point, handle, pointRight] : { std::tuple { geo.fadeIn, Handle::FadeIn, true },
                                              std::tuple { geo.fadeOut, Handle::FadeOut, false } })
    {
        g.setColour (handleColour (handle));
        g.fillPath (triangle (point, pointRight));
    }

    // Amplitude handle
    const auto ampArea = juce::Rectangle<float> (9.0f, 9.0f).withCentre (geo.amp);
    g.setColour (theme::bg);
    g.fillEllipse (ampArea);
    g.setColour (handleColour (Handle::Amp));
    g.drawEllipse (ampArea, 1.8f);

    // Cycles pill
    g.setColour (theme::bg.withAlpha (0.85f));
    g.fillRoundedRectangle (geo.cycles, 6.5f);
    g.setColour (handleColour (Handle::Cycles).withAlpha (0.9f));
    g.drawRoundedRectangle (geo.cycles, 6.5f, 1.0f);
    g.setFont (theme::font (9.5f, true));
    g.drawText (cyclesText (b), geo.cycles, juce::Justification::centred);
}

//==============================================================================
void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    if (pointsView()) { pointsMouseDown (e); return; }

    grabKeyboardFocus();

    const int step = stepAt (e.position.x);
    const int index = blockAtStep (step);

    if (mapContext.isMapping())
    {
        if (index >= 0 && mapContext.pick)
            showModTargetMenu (index);
        return;
    }

    if (e.mods.isPopupMenu())
    {
        select (index);
        showContextMenu (index, step);
        return;
    }

    const auto handle = handleAt (index, e.position);
    select (index);

    dragMode = index >= 0 ? handle : Handle::None;
    if (index >= 0)
    {
        dragStart = pattern.blocks[(size_t) index];
        dragStartPos = e.position;
        grabOffset = step - dragStart.start;
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (pointsView()) { pointsMouseDrag (e); return; }

    if (selected < 0 || dragMode == Handle::None || mapContext.isMapping())
        return;

    auto& b = pattern.blocks[(size_t) selected];
    const Block before = b;
    const int steps = numSteps();
    const auto startGeometry = geometryFor (dragStart);
    const float dx = e.position.x - dragStartPos.x;
    const float dy = e.position.y - dragStartPos.y;

    switch (dragMode)
    {
        case Handle::Body:
        {
            const int start = juce::jlimit (0, juce::jmax (0, steps - b.length), stepAt (e.position.x) - grabOffset);
            if (pattern.isFree (start, b.length, selected))
                b.start = start;
            break;
        }

        case Handle::ResizeRight:
        {
            const int end = juce::jlimit (b.start + 1, steps, juce::roundToInt (e.position.x / stepWidth()));
            const int maxLength = b.length + pattern.freeLengthFrom (b.start + b.length, steps - (b.start + b.length));
            b.length = juce::jmin (end - b.start, maxLength);
            break;
        }

        case Handle::ResizeLeft:
        {
            const int end = b.start + b.length;
            const int minStart = b.start - pattern.freeLengthBefore (b.start, b.start);
            const int start = juce::jlimit (minStart, end - 1, juce::roundToInt (e.position.x / stepWidth()));
            b.start = start;
            b.length = end - start;
            break;
        }

        case Handle::Amp:
        {
            float amount = juce::jlimit (-1.0f, 1.0f, dragStart.amount - dy / (startGeometry.curve.getHeight() * 0.5f));
            if (! e.mods.isShiftDown() && std::abs (std::abs (amount) - 1.0f) < 0.04f)
                amount = amount > 0.0f ? 1.0f : -1.0f;
            b.amount = amount;
            break;
        }

        case Handle::FadeIn:
            b.fadeIn = juce::jlimit (0.0f, 1.0f, dragStart.fadeIn + dx / startGeometry.curve.getWidth());
            break;

        case Handle::FadeOut:
            b.fadeOut = juce::jlimit (0.0f, 1.0f, dragStart.fadeOut - dx / startGeometry.curve.getWidth());
            break;

        case Handle::Cycles:
            b.cycles = snapCycles (b.type, dragStart.cycles + dx / (e.mods.isShiftDown() ? 60.0f : 14.0f), e.mods.isShiftDown());
            break;

        case Handle::None:
            break;
    }

    if (! sameShape (before, b))
    {
        commit();
        notifyEdited();
        repaint();
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragMode = Handle::None;
    pointDrag = PointDrag::None;
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    if (pointsView()) { pointsMouseMove (e); return; }

    const int index = blockAtStep (stepAt (e.position.x));
    const int previous = hovered;
    hovered = index;
    const auto handle = handleAt (index, e.position);

    if (index != previous || handle != hoveredHandle)
    {
        hoveredHandle = handle;
        repaint();
    }

    using MC = juce::MouseCursor;
    if (mapContext.isMapping())
    {
        setMouseCursor (index >= 0 ? MC::PointingHandCursor : MC::NormalCursor);
        return;
    }

    switch (handle)
    {
        case Handle::ResizeLeft:
        case Handle::ResizeRight:
        case Handle::FadeIn:
        case Handle::FadeOut:
        case Handle::Cycles: setMouseCursor (MC::LeftRightResizeCursor); break;
        case Handle::Amp:    setMouseCursor (MC::UpDownResizeCursor); break;
        case Handle::Body:   setMouseCursor (MC::DraggingHandCursor); break;
        case Handle::None:   setMouseCursor (MC::NormalCursor); break;
    }
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hovered = hoveredPoint = hoveredSegment = -1;
    hoveredHandle = Handle::None;
    repaint();
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pointsView()) { pointsDoubleClick (e); return; }

    if (mapContext.isMapping())
        return;

    const int step = stepAt (e.position.x);
    const int index = blockAtStep (step);

    if (index < 0)
    {
        addOrReplaceAt (lastType, step);
        return;
    }

    // Double-clicking a handle resets it.
    auto& b = pattern.blocks[(size_t) index];
    switch (handleAt (index, e.position))
    {
        case Handle::Amp:     b.amount = 1.0f; break;
        case Handle::FadeIn:  b.fadeIn = 0.0f; break;
        case Handle::FadeOut: b.fadeOut = 0.0f; break;
        case Handle::Cycles:  b.cycles = getTypeInfo (b.type).defaultCycles; break;
        default:              return;
    }

    commit();
    notifyEdited();
    repaint();
}

bool StepGrid::keyPressed (const juce::KeyPress& key)
{
    if (pointsView())
        return pointsKeyPressed (key);

    if (selected < 0)
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        pattern.remove (selected);
        select (-1);
        commit();
        return true;
    }

    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicate (selected);
        return true;
    }

    return false;
}

//==============================================================================
bool StepGrid::isInterestedInDragSource (const SourceDetails& details)
{
    const auto description = details.description.toString();
    return ! pointsView() && (description.startsWith ("module:") || description.startsWith ("userfile:"));
}

void StepGrid::itemDragMove (const SourceDetails& details)
{
    const int step = stepAt ((float) details.localPosition.x);
    if (step != hoverStep)
    {
        hoverStep = step;
        repaint();
    }
}

void StepGrid::itemDragExit (const SourceDetails&)
{
    hoverStep = -1;
    repaint();
}

void StepGrid::itemDropped (const SourceDetails& details)
{
    hoverStep = -1;
    const auto description = details.description.toString();
    const int step = stepAt ((float) details.localPosition.x);

    if (description.startsWith ("userfile:"))
    {
        Block prototype;
        if (usermodules::load (juce::File (description.fromFirstOccurrenceOf (":", false, false)), prototype))
            placePrototypeAt (prototype, step);
    }
    else
    {
        const int type = description.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (type >= 0 && type < kNumBlockTypes)
            addOrReplaceAt ((BlockType) type, step);
    }
    grabKeyboardFocus();
}

//==============================================================================
bool StepGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (midiimport::isMidiFile (f))
            return true;
    return false;
}

void StepGrid::fileDragMove (const juce::StringArray&, int x, int)
{
    if (const int step = pointsView() ? -1 : stepAt ((float) x); step != hoverStep)
    {
        hoverStep = step;
        repaint();
    }
}

void StepGrid::fileDragExit (const juce::StringArray&)
{
    hoverStep = -1;
    repaint();
}

void StepGrid::filesDropped (const juce::StringArray& files, int x, int)
{
    hoverStep = -1;
    for (const auto& f : files)
        if (midiimport::isMidiFile (f))
        {
            importMidi (juce::File (f), stepAt ((float) x));
            break;
        }
    repaint();
}

void StepGrid::importMidi (const juce::File& file, int step)
{
    const auto value = [this] (const char* name) { return proc.apvts.getRawParameterValue (params::lane (lane, name))->load(); };

    midiimport::CcCurve curve;
    if (! midiimport::read (file, (int) value ("cc"), curve))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "No CC data",
                                                file.getFileName() + " contains no MIDI CC events to import.");
        return;
    }

    // CC 0..127 maps straight back to the same CC output (bipolar orbits centre on 64).
    const bool bipolar = value ("bipolar") >= 0.5f;
    const auto toY = [bipolar] (float v) { return bipolar ? v * 2.0f - 1.0f : v; };
    const int steps = numSteps();
    const double beatsPerStep = params::stepBeats ((int) value ("rate"), (int) value ("fit"), steps, proc.barLength.load());

    if (pointsView())
    {
        // Lay the clip over the orbit at its tempo; anything past the loop is cut.
        constexpr int perStep = 32;
        std::vector<float> xs ((size_t) (steps * perStep + 1)), ys (xs.size());
        for (size_t k = 0; k < xs.size(); ++k)
        {
            xs[k] = (float) k / (float) perStep;
            ys[k] = toY (midiimport::valueAt (curve, juce::jmin ((double) xs[k] * beatsPerStep, curve.lengthBeats)));
        }

        pattern.envelope = {};
        for (const auto& point : fitPoints (xs, ys, kMaxPoints))
            pattern.envelope.points[(size_t) pattern.envelope.numPoints++] = point;

        selectedPoint = hoveredPoint = hoveredSegment = -1;
        commit();
        return;
    }

    // Blocks view: a Curve block as long as the clip (as far as there is room).
    const int existing = blockAtStep (step);
    auto block = Block::make (BlockType::Curve, step, 1);
    if (existing >= 0)
    {
        const auto& b = pattern.blocks[(size_t) existing];
        block.start = b.start;
        block.length = b.length;
        block.id = b.id;
    }
    else
    {
        const int wanted = juce::jmax (1, juce::roundToInt (curve.lengthBeats / beatsPerStep));
        block.length = pattern.freeLengthFrom (step, juce::jmin (wanted, steps - step));
        if (block.length < 1)
            return;
    }

    constexpr int samples = 512;
    std::vector<float> xs (samples + 1), ys (samples + 1);
    for (int k = 0; k <= samples; ++k)
    {
        xs[(size_t) k] = (float) k / (float) samples;
        ys[(size_t) k] = toY (midiimport::valueAt (curve, xs[(size_t) k] * curve.lengthBeats));
    }

    block.numCurvePoints = 0;
    for (const auto& point : fitPoints (xs, ys, kMaxCurvePoints))
        block.curve[(size_t) block.numCurvePoints++] = point;

    if (existing >= 0)
    {
        pattern.blocks[(size_t) existing] = block;
        commit();
        select (existing);
    }
    else if (pattern.add (block))
    {
        commit();
        select (pattern.numBlocks - 1);
    }
}

//==============================================================================
void StepGrid::addOrReplaceAt (BlockType type, int step)
{
    lastType = type;

    if (const int index = blockAtStep (step); index >= 0)
    {
        auto& b = pattern.blocks[(size_t) index];
        b = b.withType (type);
        commit();
        select (index);
        return;
    }

    const int length = pattern.freeLengthFrom (step, juce::jmin (4, numSteps() - step));
    if (length > 0 && pattern.add (Block::make (type, step, length)))
    {
        commit();
        select (pattern.numBlocks - 1);
    }
}

// A user module keeps all its settings; dropped on a block it takes that block's
// place, otherwise it gets its saved length (as far as there is room).
void StepGrid::placePrototypeAt (const Block& prototype, int step)
{
    if (const int index = blockAtStep (step); index >= 0)
    {
        auto& b = pattern.blocks[(size_t) index];
        auto replacement = prototype;
        replacement.start = b.start;
        replacement.length = b.length;
        replacement.id = b.id;
        b = replacement;
        commit();
        select (index);
        return;
    }

    auto block = prototype;
    block.start = step;
    block.length = pattern.freeLengthFrom (step, juce::jmin (juce::jmax (1, prototype.length), numSteps() - step));
    block.id = 0;
    if (block.length > 0 && pattern.add (block))
    {
        commit();
        select (pattern.numBlocks - 1);
    }
}

// Map mode on a block: choose what the LFO modulates (amplitude or one parameter).
void StepGrid::showModTargetMenu (int index)
{
    const auto& b = pattern.blocks[(size_t) index];
    const auto& info = getTypeInfo (b.type);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Modulate " + juce::String (info.name) + " @" + juce::String (b.start + 1));
    menu.addItem (1, "Amplitude");
    menu.addItem (2 + kModCycles, cyclesLabel (b.type));
    menu.addItem (2 + kModFadeIn, "Fade In");
    menu.addItem (2 + kModFadeOut, "Fade Out");
    if (info.numParams > 0)
        menu.addSeparator();
    for (int k = 0; k < info.numParams; ++k)
        menu.addItem (2 + k, info.params[(size_t) k].name);

    const int id = b.id;
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<StepGrid> (this), id] (int result)
                        {
                            if (safe == nullptr || result == 0 || ! safe->mapContext.pick)
                                return;
                            ModRoute route { result == 1 ? ModTarget::BlockAmp : ModTarget::BlockParam, safe->lane, id, 0.5f };
                            route.param = juce::jmax (0, result - 2);
                            safe->mapContext.pick (route);
                        });
}

void StepGrid::saveAsModule (const Block& block, const juce::String& suggestion)
{
    usermodules::askForName (this, "Save as module", suggestion,
                             [safe = juce::Component::SafePointer<StepGrid> (this), block] (const juce::String& name)
                             {
                                 if (usermodules::save (block, name) && safe != nullptr && safe->onUserModulesChanged)
                                     safe->onUserModulesChanged();
                             });
}

void StepGrid::duplicate (int index)
{
    if (index < 0 || index >= pattern.numBlocks)
        return;

    auto copy = pattern.blocks[(size_t) index];
    copy.id = 0;
    const int steps = numSteps();

    // Prefer the slot right after the original, otherwise the first free slot.
    for (int candidate = copy.start + copy.length, tries = 0; tries < steps; ++tries, ++candidate)
    {
        const int start = candidate % steps;
        if (start + copy.length <= steps && pattern.isFree (start, copy.length))
        {
            copy.start = start;
            if (pattern.add (copy))
            {
                commit();
                select (pattern.numBlocks - 1);
            }
            return;
        }
    }
}

void StepGrid::showContextMenu (int blockIndex, int step)
{
    juce::PopupMenu menu;

    if (blockIndex >= 0)
    {
        const auto current = pattern.blocks[(size_t) blockIndex].type;
        juce::PopupMenu types;
        for (int t = 0; t < kNumBlockTypes; ++t)
            types.addItem (100 + t, getTypeInfo ((BlockType) t).name, true, (BlockType) t == current);

        menu.addItem (1, "Delete");
        menu.addItem (2, "Duplicate");
        menu.addItem (3, "Flip polarity");
        menu.addItem (5, "Reset amplitude, fades & cycles");
        menu.addSubMenu ("Change module", types);
        menu.addSeparator();
        menu.addItem (6, "Learn trigger note (play a note)");
        menu.addItem (7, "Trigger on any note", pattern.blocks[(size_t) blockIndex].note >= 0);
        menu.addSeparator();
        menu.addItem (8, "Save as module...");
    }
    else
    {
        juce::PopupMenu add;
        for (int t = 0; t < kNumBlockTypes; ++t)
            add.addItem (200 + t, getTypeInfo ((BlockType) t).name);
        menu.addSubMenu ("Add module here", add);
    }

    menu.addSeparator();
    menu.addItem (4, "Clear orbit", pattern.numBlocks > 0);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<StepGrid> (this), blockIndex, step] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& self = *safe;
                            if (result == 1)
                            {
                                self.pattern.remove (blockIndex);
                                self.select (-1);
                                self.commit();
                            }
                            else if (result == 2)
                            {
                                self.duplicate (blockIndex);
                            }
                            else if (result == 3 || result == 5)
                            {
                                auto& b = self.pattern.blocks[(size_t) blockIndex];
                                if (result == 3)
                                {
                                    b.amount = -b.amount;
                                }
                                else
                                {
                                    b.amount = 1.0f;
                                    b.fadeIn = b.fadeOut = 0.0f;
                                    b.cycles = getTypeInfo (b.type).defaultCycles;
                                }
                                self.commit();
                                self.select (blockIndex);
                            }
                            else if (result == 4)
                            {
                                self.pattern = {};
                                self.select (-1);
                                self.commit();
                            }
                            else if (result == 6)
                            {
                                self.learnBlockId = self.pattern.blocks[(size_t) blockIndex].id;
                                self.learnSerial = self.proc.lastNoteSerial.load();
                            }
                            else if (result == 7)
                            {
                                self.pattern.blocks[(size_t) blockIndex].note = -1;
                                self.commit();
                                self.select (blockIndex);
                            }
                            else if (result == 8)
                            {
                                const auto& b = self.pattern.blocks[(size_t) blockIndex];
                                self.saveAsModule (b, "My " + juce::String (getTypeInfo (b.type).name));
                            }
                            else if (result >= 200)
                            {
                                self.addOrReplaceAt ((BlockType) (result - 200), step);
                            }
                            else if (result >= 100)
                            {
                                self.addOrReplaceAt ((BlockType) (result - 100), self.pattern.blocks[(size_t) blockIndex].start);
                            }

                            self.repaint();
                        });
}

//==============================================================================
void StepGrid::reloadFromProcessor()
{
    pattern = proc.getPattern (lane);
    selected = hovered = selectedPoint = hoveredPoint = hoveredSegment = -1;
    hoveredHandle = Handle::None;
    dragMode = Handle::None;
    repaint();
}

void StepGrid::clearSelection()
{
    if (selected >= 0)
    {
        selected = -1;
        repaint();
    }
}

void StepGrid::replaceBlock (int index, const Block& block)
{
    if (index < 0 || index >= pattern.numBlocks)
        return;

    pattern.blocks[(size_t) index] = block;
    commit();
    repaint();
}

void StepGrid::select (int index)
{
    selected = index;
    if (onSelect)
        onSelect (lane, index);
    repaint();
}

void StepGrid::notifyEdited()
{
    if (onBlockChanged)
        onBlockChanged (lane, selected);
}

void StepGrid::commit()
{
    const int version = proc.setPattern (lane, pattern);
    if (onCommitted)
        onCommitted (version);
}

} // namespace modlanes
