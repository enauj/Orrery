# Orrery

*Formerly ModLanes. Orbits are the step lanes (still called lanes in the code), and the Alignment is the master length after which all orbits restart together, like planets lining up.*

A CLAP (and VST3) MIDI plugin that turns step-sequenced envelopes and LFO shapes into MIDI CC.

- **3 independent orbits.** Each orbit has its own step count (1–32), step rate (1/64 to 2 bars, including triplets), MIDI channel and CC number. Different step counts per orbit give you polymeters.
- **Modules** that you drag from the palette onto the step grid. One block can cover several steps:
  Envelope (ADSR + curve), Ramp (raw / unsculpted), Inverted Envelope (goes negative), Sine, Triangle,
  Saw Up/Down, Square, Sample & Hold, Wavetable (5 morphing tables), Line.
- **Per-orbit output:** Depth, Offset, Smooth (ms), and a Bipolar switch (0 = CC 64, so negative shapes swing below the centre).
- **3 global LFOs** (synced or free-running Hz) with Phase, Skew, Smooth, Unipolar and 11 shapes (Sine, Triangle, Saw Up/Down, Square, S&H, Smooth Random, Exp Rise, Exp Fall, Stairs, Bump). Each one can be mapped to an orbit's Depth, Offset or Rate, or to a block's amplitude.
- Follows the host transport. *Run when stopped* keeps it running at the host tempo while the transport is stopped.

See [TESTING.md](TESTING.md) for the checklist of things still to verify in Bitwig.

## Build (Windows)

You need Visual Studio Build Tools with the **Desktop development with C++** workload,
**including a Windows 11 SDK**, plus CMake ≥ 3.22. JUCE 8 and clap-juce-extensions are downloaded automatically.

```sh
cmake -B build
cmake --build build --config Release --target ModLanes_CLAP
```

The `.clap` (`Orrery.clap`) is copied to `%LOCALAPPDATA%\Programs\Common\CLAP`, which Bitwig scans.
Turn the copy off with `-DMODLANES_INSTALL_CLAP=OFF`. The VST3 and Standalone targets are `ModLanes_VST3` and `ModLanes_Standalone`.

## Using it in Bitwig

1. Put Orrery on an instrument track in front of a synth, or on its own track. It is an audio FX with note in/out.
   For audio following, use its sidechain input or put it on the audio track you want to follow.
2. Map the CCs (20, 21 and 22 by default) to the parameters you want to modulate. In Bitwig you can do this with the
   synth's MIDI learn, or with a *MIDI CC* / *Expressions* modulator on the target device.
3. Press play. The playheads follow the transport.

## Editing

| Action | How |
| --- | --- |
| Add a module | Drag from the palette onto an empty step, or double-click an empty step |
| Swap a module | Drop a module onto an existing block |
| Move / resize | Drag the block body / its left or right edge |
| Amplitude | Drag the centre dot up or down (below the centre line flips the polarity) |
| Fade in / out | Drag the triangles in the top corners |
| Cycles / repeats | Drag the `x1` pill at the bottom left or right (`Shift` for fine steps) |
| Reset a handle | Double-click it |
| Delete / duplicate | Select the block, then press `Delete` / `Ctrl+D`, or right-click |
| Other parameters | Select a block, then use the Module inspector at the bottom left |

## Modules

| Module | Shape | Cycles knob |
| --- | --- | --- |
| Envelope / Inverted Env | ADSR with curve, positive or negative | Repeats |
| Ramp (raw) | Unsculpted rise to a peak and fall | Repeats |
| Sine, Triangle, Saw Up/Down, Square | Classic LFO shapes with phase, width, uni/bipolar | Cycles |
| Sample & Hold | Random steps with smoothing and seed | Steps |
| Wavetable | 5 morphing tables (Harmonics, Pulses, Stairs, Bounce, Wobble) | Cycles |
| Line | Straight segment from Start to End | Repeats |
| Spring | Damped sine that rings out | Cycles |
| Bounce | Bouncing ball: each bounce shorter and lower (Elasticity) | Bounces |
| Euclid | Euclidean rhythm: Hits spread over the steps, each a decaying pulse; Rotate shifts it | Steps |
| Drift | Smooth random wandering, with Rough adding finer detail | Points |
| Stairs | Quantised ramp: up, down or up-down | Levels |
| Chaos | Logistic map: from orderly (3.5) to chaotic (4.0) values | Values |
| Heartbeat | Double pulse per beat | Beats |
| Formula | Your own function (see below) | Cycles |
| Steps | Step sequencer: one value per step, drawn in the inspector (right-click: randomize, ramps, invert); Glide slides between steps | Steps |
| *User modules* | Anything you saved (see below), listed under **USER** in the palette | - |

### User modules

Right-click a block and choose **Save as module...** to keep it with all its settings (a tuned Euclid, a formula,
a step pattern). In the Points view, right-click and choose **Save curve as module...** to turn the drawn envelope
into a reusable curve. Saved modules appear under **USER** in the palette and are dragged in like any other; a
curve keeps its saved length when there is room. They are files in `Documents/ModLanes/Modules`; right-click one in
the palette to delete it.

### Formula

Type an expression in the inspector, or pick one from **Examples**. Variables: `t` (0..1 across the block),
`x` (phase within the current cycle), `c` (cycle number), `a` and `b` (the A and B knobs, so the formula can be
tweaked and modulated by hand), `pi`, `e`. Operators `+ - * / % ^` and brackets. Functions: `sin cos tan abs sqrt exp
log floor fract sign tri saw sqr noise` and `min max pow` (two arguments). The result is always limited to -1..1 by
the **Clip** knob (Hard, Soft = tanh, Fold). Invalid text turns the field red and the last valid formula keeps playing.

## Rate

Each orbit (and each LFO) has a **Rate** knob. With **Sync** on, it steps through note values at the host tempo.
With Sync off, it is a free rate (steps per second for orbits, Hz for LFOs) that ignores the tempo and restarts
with the transport. The orbit Rate knobs also show how often the whole orbit loops, in Hz.

## Phase, Fit and polyrhythms

- **Phase** (per orbit) shifts where its loop starts, 0-100% of its length.
- **Fit** squeezes all of an orbit's steps into exactly 1/2, 1, 2, 3, 4 or 8 bars, whatever the step count.
  Give two orbits the same Fit and different step counts (say 5 and 4) and they play a 5:4 polyrhythm.
  Different step counts with a normal Rate give polymeters (e.g. 16 against 12 steps).
- The **Orrery** view (bottom left) shows the orbits as planets, each circling once per loop, with a gold mark where
  they line up. It shows when they naturally realign; click the text to set the **Alignment** to that.

## MIDI CC in

Drop a MIDI file with CC data (for example a clip dragged out of the DAW) onto an orbit. In Blocks view it becomes a
**Curve** block as long as the clip (right-click it to save it as a module); in Points view it replaces the
envelope. The orbit's own CC number is used if the file has it, otherwise the CC with the most events. CC values map
straight back to the same output (bipolar orbits centre on 64).

## Blocks and Points

**Blocks** is the sketching view: drag modules in and shape them. **Points** is a breakpoint envelope.
Switching to Points the first time renders the blocks (including Blend) into points. **From blocks**, or
right-click > *Render from blocks*, re-renders and replaces the points. The orbit plays whichever view is selected.

| In Points view | How |
| --- | --- |
| Add a point | Click on empty space (and keep dragging to place it) |
| Move a point | Drag it. It snaps to 1/4 steps and to 0 / ±100%; hold `Shift` to place it freely |
| Bend a segment | Drag the diamond halfway along the segment up or down |
| Delete / straighten | Double-click a point / a diamond, or press `Delete` with a point selected |

## Export to the arrangement

Drag the small curve icon above an orbit's meter into your DAW. It writes one loop of the orbit as a MIDI file with
CC data (using the orbit's channel, CC, Depth, Offset, Bipolar, Invert and Blend, at the current tempo) and drops it
as a clip. LFO modulation is not included. How the CCs show up depends on the DAW.

## Transport, scenes and presets

The bar under the header holds the global settings:

| Control | What it does |
| --- | --- |
| **Clock** | *Host* follows the DAW transport (plus *Run when stopped*). *Internal* uses its own **BPM** and **Play / Stop**; starting always begins at bar 1 |
| **Scene 1-10** | Each scene is a full pattern: all orbits (blocks and points), LFO routings and every orbit/LFO setting. Click to switch; an empty scene starts as a copy of the current one. Right-click: copy current here, clear, load a preset, save as preset |
| **Quantize** | When a scene change takes effect while playing: *Instant*, *Next Bar* or *Alignment*. The target pad blinks until then |
| **Alignment** | Every N bars all orbits and LFOs restart together, however slow they are, and the phrase indicator shows one square per bar. Off = everything loops on its own |
| **Presets** | Load a factory or user preset into the current scene, or save the current scene. User presets are files in `Documents\ModLanes\Presets` |

The scene number is also an automatable parameter, so the DAW can switch scenes.
Scene changes happen on the UI thread right after the boundary, so they can land a few milliseconds late.

## Phrase indicator

The squares in the header show where you are in the pattern, Elektron style: one square per bar of the master
length (4 when Length is off), and the current square flashes on every beat (brightest on the downbeat). The
readout shows bar.beat. It follows the time signature and keeps running with Live view off.

## Interface size

Right-click any empty space (the window background, the header, or the empty parts of a panel) and pick an
**Interface size** from 75% to 200%. It is saved with the project and becomes the default for new instances.

## Live view

**Live view** in the header animates the playheads, value dots, meters and LFO dots. Turn it off to save CPU;
the modulation output keeps running.

## Orbit modes

| Mode | What drives the orbit |
| --- | --- |
| Sequence | The host transport loops through the steps |
| MIDI Trigger | Each incoming note plays one block once. A block with a **Trig Note** (inspector, or right-click > *Learn trigger note*) only responds to that note; unmapped notes step round-robin through the blocks set to *Any*. **Velocity** sets how much note velocity scales the shape |
| Audio Trigger | Hits above **Thresh** in the audio play the blocks round-robin. **Hold** sets how quickly the detector releases |
| Audio Follow | Outputs the audio level (Thresh to 0 dB = 0 to 1) with **Attack** / **Release** |

Audio comes from the main input or the **Sidechain** input (per orbit), and passes through unchanged.
Use **Invert** with Offset at 100% for ducking.
**Blend** crossfades between neighbouring blocks (in steps). In the trigger modes it also crossfades on a retrigger and back to rest at the end.

## LFO modulation

Click **Map** on an LFO, then click a target (Esc cancels):

- an orbit's **Depth**, **Offset** or **Rate**;
- a **block** on a grid, then pick what to modulate from the menu: Amplitude, Cycles/Repeats, Fade In, Fade Out,
  or any of the module's own parameters (Attack, Morph, Smooth, Hits, Rotate, Formula A / B, ...);
- or a **knob of the selected module** in the inspector (select the block first; in map mode the knobs light up).

Each LFO can have up to 4 targets, each with its own amount (drag the bar, double-click to reset). Block parameters
move around their current setting by the amount times their full range; Cycles moves by up to +-2 octaves; stepped
values (Hits, Table, Seed, ...) stay whole numbers. Coloured dots show which controls, blocks and inspector knobs are
modulated. Rate modulation speeds an orbit up or slows it down by up to 2 octaves.

While Live view is on you see the modulation as it happens: modulated knobs get an inner arc and dot in the LFO's
colour showing where the value is pushed right now, and modulated blocks draw their live curve over the static one,
with the orbit's dot riding it.

## Layout

```
Source/
  ModuleTypes.h      block types, parameter metadata, LanePattern
  Shapes.h           envelope / LFO / wavetable evaluation (shared by DSP and GUI)
  Params.h           parameter IDs and rate tables
  PluginProcessor.*  transport sync, lane evaluation, LFO modulation, CC output, state
  PluginEditor.*     window layout
  gui/               palette, step grid, lane strip, inspector, LFO panel, look & feel
```

## Performance

`cmake -B build -DMODLANES_BENCH=ON` then build `ModLanesBench` and run it to measure. Results on the development
machine (48 kHz, 512-sample blocks, UI at 2x scale):

| | Cost |
| --- | --- |
| Audio, heaviest case (3 x 32 blocks, Blend, 9 LFO routes, dense Points) | ~11 us per block, about 0.1 % of real time |
| UI frame with Live view on | ~2.8 ms, about 8 % of one core at 30 frames/s |
| UI frame with Live view off | ~0.6 ms, under 2 % of one core |
| First paint / resize | ~45 ms |

What keeps the UI cheap: grids, meters and LFO waves are opaque so nothing behind them repaints; each grid
caches everything but the playhead and only repaints the columns the playhead moves through; meters, the phrase
indicator and the scene pads repaint only when their value changes; and all cached images are CPU (software)
images, because the window uses the software renderer.

## Ideas for next steps

- Pages: orbits longer than 32 steps, shown 16 steps per page (Elektron style), with page buttons that follow the playhead

- 14-bit CC (MSB/LSB) output for smoother modulation
- LFOs targeting block parameters (e.g. the morph of a wavetable block)
- Retrigger orbits from incoming MIDI notes
- Per-step probability / ratchets
- Drawing custom wavetables
