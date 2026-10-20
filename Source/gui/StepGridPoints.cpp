// Points view of the step grid: a breakpoint envelope with draggable points
// and bendable segments, rendered from the block layout on demand.

#include "StepGrid.h"
#include "Params.h"

namespace modlanes
{

namespace
{
constexpr float kPointRadius = 4.5f;
constexpr float kHitRadius = 8.0f;

float snapX (float x, bool fine) { return fine ? x : std::round (x * 4.0f) / 4.0f; } // quarter steps

float snapY (float y, bool fine)
{
    if (! fine)
        for (float target : { -1.0f, 0.0f, 1.0f })
            if (std::abs (y - target) < 0.03f)
                return target;
    return y;
}
} // namespace

void StepGrid::resized()
{
    renderButton.setBounds (getLocalBounds().removeFromTop (22).removeFromRight (86).reduced (3));
}

juce::Point<float> StepGrid::pointPosition (const EnvPoint& p) const
{
    const float mid = (curveTop() + curveBottom()) * 0.5f, half = (curveBottom() - curveTop()) * 0.5f;
    return { p.x * stepWidth(), mid - p.y * half };
}

// Handle on the curve halfway along segment i (from point i to the next, wrapping).
juce::Point<float> StepGrid::segmentHandle (int segment) const
{
    const auto& e = pattern.envelope;
    const int steps = numSteps();
    const auto& a = e.points[(size_t) segment];
    float bx = segment + 1 < e.numPoints ? e.points[(size_t) segment + 1].x : e.points[0].x + (float) steps;
    const float x = (a.x + bx) * 0.5f;
    const float mid = (curveTop() + curveBottom()) * 0.5f, half = (curveBottom() - curveTop()) * 0.5f;
    const float v = evalPoints (e, steps, std::fmod ((double) x, (double) steps));
    return { std::fmod (x, (float) steps) * stepWidth(), mid - v * half };
}

int StepGrid::pointAt (juce::Point<float> p) const
{
    for (int i = 0; i < pattern.envelope.numPoints; ++i)
        if (pattern.envelope.points[(size_t) i].x <= (float) numSteps()
            && pointPosition (pattern.envelope.points[(size_t) i]).getDistanceFrom (p) < kHitRadius)
            return i;

    return -1;
}

int StepGrid::segmentAt (juce::Point<float> p) const
{
    if (pattern.envelope.numPoints < 2)
        return -1;

    for (int i = 0; i < pattern.envelope.numPoints; ++i)
        if (segmentHandle (i).getDistanceFrom (p) < kHitRadius)
            return i;

    return -1;
}

//==============================================================================
void StepGrid::paintPoints (juce::Graphics& g)
{
    const auto& e = pattern.envelope;
    const int steps = numSteps();
    const float sw = stepWidth();
    const float mid = (curveTop() + curveBottom()) * 0.5f, half = (curveBottom() - curveTop()) * 0.5f;
    const auto colour = theme::laneColour (lane);

    if (e.numPoints == 0)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        g.drawText ("Click to add points, or use \"From blocks\"", getLocalBounds(), juce::Justification::centred);
        return;
    }

    juce::Path curve;
    for (int x = 0; x <= getWidth(); ++x)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, evalPoints (e, steps, juce::jlimit (0.0, steps - 1.0e-4, (double) x / sw)));
        if (x == 0) curve.startNewSubPath ((float) x, mid - v * half);
        else        curve.lineTo ((float) x, mid - v * half);
    }

    auto fill = curve;
    fill.lineTo ((float) getWidth(), mid);
    fill.lineTo (0.0f, mid);
    fill.closeSubPath();

    juce::ColourGradient gradient (colour.withAlpha (0.35f), 0.0f, curveTop(), colour.withAlpha (0.35f), 0.0f, curveBottom(), false);
    gradient.addColour (0.5, colour.withAlpha (0.02f));
    g.setGradientFill (gradient);
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bend handles: the hovered segment, and the one leaving the selected point.
    for (int i : { hoveredSegment, selectedPoint })
        if (i >= 0 && i < e.numPoints && e.numPoints > 1 && ! mapContext.isMapping())
        {
            const auto c = segmentHandle (i);
            juce::Path diamond;
            diamond.addQuadrilateral (c.x, c.y - 5.0f, c.x + 5.0f, c.y, c.x, c.y + 5.0f, c.x - 5.0f, c.y);
            g.setColour (theme::bg);
            g.fillPath (diamond);
            g.setColour (i == hoveredSegment ? juce::Colours::white : colour);
            g.strokePath (diamond, juce::PathStrokeType (1.4f));
        }

    // Dense envelopes (e.g. rendered LFOs) get smaller points so the curve stays readable.
    const float pointsPerStep = (float) e.numPoints / (float) steps;
    const float radius = pointsPerStep > 2.5f ? 2.5f : kPointRadius;

    for (int i = 0; i < e.numPoints; ++i)
    {
        const auto& p = e.points[(size_t) i];
        if (p.x > (float) steps)
            continue;

        const bool isSelected = i == selectedPoint, isHovered = i == hoveredPoint;
        const float r = isSelected || isHovered ? kPointRadius : radius;
        const auto c = pointPosition (p);
        const auto area = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);

        g.setColour (isSelected ? juce::Colours::white : theme::bg);
        g.fillEllipse (area);
        g.setColour (isSelected || isHovered ? juce::Colours::white : colour);
        g.drawEllipse (area, 1.6f);
    }

    // Readout for the selected point
    if (selectedPoint >= 0 && selectedPoint < e.numPoints)
    {
        const auto& p = e.points[(size_t) selectedPoint];
        const auto c = pointPosition (p);
        const juce::String text = "step " + juce::String (p.x + 1.0f, 2) + "   " + juce::String (juce::roundToInt (p.y * 100.0f)) + "%";
        auto box = juce::Rectangle<float> (92.0f, 15.0f).withPosition (c.x + 8.0f, c.y - 22.0f);
        box = box.constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));
        g.setColour (theme::bg.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (10.0f));
        g.drawText (text, box, juce::Justification::centred);
    }
}

//==============================================================================
void StepGrid::pointsMouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (mapContext.isMapping())
        return;

    const int point = pointAt (e.position);
    const int segment = point < 0 ? segmentAt (e.position) : -1;

    if (e.mods.isPopupMenu())
    {
        selectedPoint = point;
        showPointsMenu (point, segment);
        repaint();
        return;
    }

    if (point >= 0)
    {
        selectedPoint = point;
        pointDrag = PointDrag::Point;
    }
    else if (segment >= 0)
    {
        dragSegment = segment;
        dragStartBend = pattern.envelope.points[(size_t) segment].bend;
        pointDrag = PointDrag::Bend;
    }
    else
    {
        // Click on empty space adds a point and starts dragging it.
        const float mid = (curveTop() + curveBottom()) * 0.5f, half = (curveBottom() - curveTop()) * 0.5f;
        const bool fine = e.mods.isShiftDown();
        const EnvPoint p { juce::jlimit (0.0f, (float) numSteps(), snapX (e.position.x / stepWidth(), fine)),
                           snapY (juce::jlimit (-1.0f, 1.0f, (mid - e.position.y) / half), fine), 0.0f };
        selectedPoint = pattern.envelope.insert (p);
        pointDrag = selectedPoint >= 0 ? PointDrag::Point : PointDrag::None;
        commit();
    }

    repaint();
}

void StepGrid::pointsMouseDrag (const juce::MouseEvent& e)
{
    auto& env = pattern.envelope;

    if (pointDrag == PointDrag::Point && selectedPoint >= 0 && selectedPoint < env.numPoints)
    {
        const float mid = (curveTop() + curveBottom()) * 0.5f, half = (curveBottom() - curveTop()) * 0.5f;
        const bool fine = e.mods.isShiftDown();
        const float lo = selectedPoint > 0 ? env.points[(size_t) selectedPoint - 1].x : 0.0f;
        const float hi = selectedPoint + 1 < env.numPoints ? env.points[(size_t) selectedPoint + 1].x : (float) numSteps();

        auto& p = env.points[(size_t) selectedPoint];
        p.x = juce::jlimit (lo, hi, snapX (e.position.x / stepWidth(), fine));
        p.y = snapY (juce::jlimit (-1.0f, 1.0f, (mid - e.position.y) / half), fine);
        commit();
        repaint();
    }
    else if (pointDrag == PointDrag::Bend && dragSegment >= 0 && dragSegment < env.numPoints)
    {
        const float sensitivity = e.mods.isShiftDown() ? 400.0f : 120.0f;
        env.points[(size_t) dragSegment].bend = juce::jlimit (-1.0f, 1.0f, dragStartBend - (float) e.getDistanceFromDragStartY() / sensitivity);
        commit();
        repaint();
    }
}

void StepGrid::pointsMouseMove (const juce::MouseEvent& e)
{
    const int point = pointAt (e.position);
    const int segment = point < 0 ? segmentAt (e.position) : -1;

    if (point != hoveredPoint || segment != hoveredSegment)
    {
        hoveredPoint = point;
        hoveredSegment = segment;
        repaint();
    }

    using MC = juce::MouseCursor;
    setMouseCursor (point >= 0 ? MC::DraggingHandCursor : segment >= 0 ? MC::UpDownResizeCursor : MC::CrosshairCursor);
}

void StepGrid::pointsDoubleClick (const juce::MouseEvent& e)
{
    if (const int point = pointAt (e.position); point >= 0)
    {
        pattern.envelope.remove (point);
        selectedPoint = hoveredPoint = -1;
        commit();
    }
    else if (const int segment = segmentAt (e.position); segment >= 0)
    {
        pattern.envelope.points[(size_t) segment].bend = 0.0f;
        commit();
    }

    pointDrag = PointDrag::None;
    repaint();
}

bool StepGrid::pointsKeyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedPoint >= 0)
    {
        pattern.envelope.remove (selectedPoint);
        selectedPoint = hoveredPoint = -1;
        commit();
        repaint();
        return true;
    }

    return false;
}

void StepGrid::showPointsMenu (int point, int segment)
{
    juce::PopupMenu menu;
    if (point >= 0)
        menu.addItem (1, "Delete point");
    if (segment >= 0)
        menu.addItem (2, "Straighten segment");
    if (point >= 0 || segment >= 0)
        menu.addSeparator();
    menu.addItem (3, "Render from blocks (replaces points)");
    menu.addItem (4, "Clear points", pattern.envelope.numPoints > 0);
    menu.addSeparator();
    menu.addItem (5, "Save curve as module...", pattern.envelope.numPoints > 0);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<StepGrid> (this), point, segment] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& self = *safe;
                            auto& env = self.pattern.envelope;
                            if (result == 1)       env.remove (point);
                            else if (result == 2)  env.points[(size_t) segment].bend = 0.0f;
                            else if (result == 3)  { self.renderFromBlocks(); return; }
                            else if (result == 4)  env = {};
                            else if (result == 5)  { self.saveAsModule (curveBlockFromEnvelope (env, self.numSteps()), "My Curve"); return; }

                            self.selectedPoint = self.hoveredPoint = self.hoveredSegment = -1;
                            self.commit();
                            self.repaint();
                        });
}

void StepGrid::renderFromBlocks()
{
    pattern.envelope = renderBlocksToPoints (pattern, numSteps(), blendParam->load());
    selectedPoint = hoveredPoint = hoveredSegment = -1;
    commit();
    repaint();
}

} // namespace modlanes
