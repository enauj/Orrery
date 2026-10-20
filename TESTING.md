# Orrery test checklist

Everything that has not been confirmed yet, mostly because it can only be checked in Bitwig or by clicking
through the interface. Tick items off as you go, and note anything odd next to them.

Before you start: the plugin is now called **Orrery** (same plugin ID, so existing projects still find it).
Also: restart Bitwig (or rescan plug-ins) so it loads the latest `Orrery.clap` from
`%LOCALAPPDATA%\Programs\Common\CLAP`. Existing ModLanes devices in old projects may need to be re-added,
because the plugin changed from a note effect to an audio effect.

## 1. Bitwig basics (most important)

- [ ] **Orrery** shows up in Bitwig's browser after a rescan, and there is no leftover "ModLanes" entry.
- [ ] A project saved with ModLanes opens with the device now called Orrery, with its settings intact.
- [ ] Presets and user modules saved earlier are still there (their folder moved from `Documents\ModLanes` to `Documents\Orrery`).
- [ ] **CC output reaches the next device.** Put Orrery in front of a synth on an instrument track,
      press play, and MIDI-learn a synth parameter from an orbit (default CCs 20 / 21 / 22). This is the big one
      since Orrery became an audio FX.
- [ ] Incoming notes still pass through to the synth when *MIDI thru* is on.
- [ ] Audio passes through unchanged.
- [ ] The **sidechain input** appears in Bitwig, so another track can feed it.
- [ ] No more **flickering squares** when moving knobs. (The renderer was switched to the software renderer,
      and knob value pop-ups now draw inside the window.)
- [ ] The window can be resized.
- [ ] **Interface size:** right-click empty space (background, header, empty panel areas) > 75 % ... 200 %.
      Everything scales and stays sharp, clicks still land in the right place, the astrolabe ornament lines up,
      and the size is remembered for the project and for new instances.

## 2. Exporting to the arrangement

- [ ] Dragging the small curve icon above an orbit's meter into the arrangement creates a clip.
- [ ] The clip contains the CC data on the right CC number and channel, and its length is one loop of the orbit.
- [ ] Note how Bitwig shows the CCs (as clip automation, MIDI CC orbits, or not at all), so the export can be
      adjusted if needed.

## 3. Transport, scenes and presets

- [ ] The **play button** shows a play triangle when stopped and a stop square (green) while playing.
- [ ] **Alignment LEDs** are grouped in fours; above 8 bars they stack in two rows (try 12 and 16 bars).
- [ ] **Internal clock:** Clock = Internal, set the BPM, press Play. The orbits run at that tempo, start at bar 1,
      and stop with Stop.
- [ ] **Scene switching:** click scene pads while playing. With Quantize = *Next Bar* the target pad blinks and
      the change happens on the next bar; with *Alignment* at the end of the Alignment; with *Instant*
      straight away. Changes may land a few milliseconds after the boundary.
- [ ] An empty scene starts as a copy of the current one; used scenes show a dot.
- [ ] Right-click a pad: *Copy current scene here*, *Clear scene*, *Load preset into scene*, *Save scene as preset*.
- [ ] **Scenes are saved with the project:** set up a few scenes, save, close and reopen. Everything comes back,
      including which scene was active.
- [ ] Automating the **Scene** parameter from Bitwig switches scenes.
- [ ] **Presets:** load the factory presets (Init, Quarter Pump, Slow Wobble, Random Steps, Triplet Ramps),
      save your own (it appears under Presets > User), and load it again.

## 4. Alignment

- [ ] **ALIGNMENT** (in the scene bar) set to e.g. 8 bars: the phrase indicator shows 8 squares.
- [ ] A very slow orbit (e.g. Rate 2 Bars, 16 steps) restarts after 8 bars instead of running its full length.
- [ ] Synced and free LFOs also restart at the Alignment (watch the dot on the LFO waveform).
- [ ] Orbits with Sync off (free rate) restart too.

## 5. Orbits and blocks

- [ ] **Shape handles** on a block: amplitude dot (drag below the centre to flip it), fade triangles,
      cycles pill (Shift for fine steps), left and right edges. Double-clicking a handle resets it.
- [ ] **Blend** crossfades between neighbouring blocks (white line on the grid), including the wrap from the last
      step back to the first.
- [ ] **Rate knobs** turn slower to the left and faster to the right, both synced and free. The value pop-up
      shows the loop rate in Hz.
- [ ] Projects saved before the rate order was reversed still play at the same rate.
- [ ] The **phrase indicator** squares flash on each beat, brightest on the downbeat, and show bar.beat.
- [ ] **Live view** off: playheads, value dots and LFO dots stop moving, the CC output keeps running, CPU use
      drops, and the **output meters on the right still move**.
- [ ] Bitwig's CPU / UI feels lighter than before with the window open (the UI was optimised: about 8 % of one
      core with Live view on, under 2 % with it off, measured with the benchmark).
- [ ] An orbit switched **off** (its On button) shows LANE OFF, fades its controls and stops sending CC.
- [ ] An **LFO switched off** (On button in the LFO panel) dims, its dot stops, and its routes stop modulating.

## 5b. Phase, Fit, polyrhythms and the Orrery

- [ ] **Phase** on an orbit shifts its loop start (the playhead and planet move by that much).
- [ ] **Fit**: two orbits with Fit = 1 bar and 5 vs 4 steps play a 5:4 polyrhythm; the Rate knob fades to "(Fit)".
- [ ] The **Orrery** (bottom left): planets circle once per loop, the text shows when orbits realign, and clicking it
      sets Alignment. The tooltip lists each orbit's loop length.
- [ ] The **orbit playhead dot moves smoothly** (third fix: the real bug was that the dot was only repainted
      when it crossed into the next step; measured now at ~60 paints/s instead of ~25 with 100 ms gaps).
      Check in the Standalone app and in Bitwig, also on a high-refresh display.
- [ ] **Phase, Skew, Smooth** knobs and the **Uni** switch on each LFO; the waveform picture follows Skew.
- [ ] The new shapes: Smooth Random, Exp Rise, Exp Fall, Stairs, Bump.

## 5d. MIDI CC in

- [ ] Dragging a MIDI file with CC data (from Explorer, or a clip out of Bitwig) onto an orbit in **Blocks** view
      creates a Curve block as long as the clip; right-click > *Save as module* keeps it.
- [ ] The same in **Points** view replaces the envelope.
- [ ] A file without CC data shows a short message instead.
- [ ] Note whether Bitwig lets you drag clips (or CC automation) straight from the arrangement into the plugin.

## 6. Points view

- [ ] Switching an orbit to **Points** the first time renders the blocks into points.
- [ ] Click to add a point, drag to move it (snaps to 1/4 steps; Shift for free placement), drag the diamond
      on a segment to bend it, double-click or Delete to remove a point.
- [ ] **From blocks** (top right) re-renders the points from the blocks.
- [ ] The orbit plays the Points envelope while Points is selected, and the blocks again when switched back.

## 7. Modules

- [ ] **Formula:** typing in the field under the inspector preview changes the shape live. Invalid text turns
      the field red, the tooltip explains the error, and the last valid formula keeps playing.
- [ ] Formula **Examples** menu, and the A / B knobs changing the formula.
- [ ] Formula **Clip** modes (Hard / Soft / Fold) keep the output in range.
- [ ] **Steps:** drawing bar values in the inspector preview; right-click for Randomize, Ramp up/down, Invert,
      All to 50%; Glide; Bipolar.
- [ ] **Euclid:** useful as a rhythmic CC source? (Hits, Steps, Rotate, Decay)
- [ ] The other new modules feel right: Spring, Bounce, Drift, Stairs, Chaos, Heartbeat.

## 8. User modules

- [ ] Right-click a block > **Save as module...**: it appears in gold under **USER** in the palette.
- [ ] In Points view, right-click > **Save curve as module...**: the curve appears under USER.
- [ ] Dragging a user module onto an orbit recreates it with all its settings. A curve keeps its saved length
      when there is room; dropped on an existing block it takes that block's place.
- [ ] Right-click a user module in the palette > *Delete module* / *Show in folder*.
- [ ] The palette scrolls when the list is longer than the window.

## 9. Orbit modes

- [ ] **MIDI Trigger:** each note plays one block once. A block with a trigger note (inspector *Trig Note*,
      or right-click > *Learn trigger note* and play a key) only responds to that note. Other notes step through
      the blocks set to *Any*. **Velocity** scales the shape. Works with the transport stopped.
- [ ] MIDI Trigger in **Points** view plays the whole envelope once per note.
- [ ] **Audio Trigger:** hits above **Thresh** play the blocks in turn (main input or sidechain).
- [ ] **Audio Follow:** the orbit follows the audio level (Thresh, Attack, Release); the grid shows a live trace.
- [ ] **Invert** with Offset at 100% gives ducking.

## 10. LFO mapping

- [ ] Click **Map** on an LFO, then click an orbit's Depth, Offset or Rate: the route appears in the LFO panel with
      its own amount, and coloured dots mark the modulated controls. Esc cancels mapping.
- [ ] Map onto a **block** on the grid: a menu offers Amplitude, Cycles, Fade In/Out and the module's own
      parameters; the chosen one is modulated (e.g. a Wavetable's Morph, a Euclid's Hits, a Formula's A).
- [ ] Select a block, click Map, then click a **knob in the inspector**: that parameter is mapped. Modulated knobs
      show LFO-coloured dots.
- [ ] Deleting a block removes its routes; the LFO panel names routes like "O2 EUCL @1 Hits".
- [ ] Rate modulation speeds the orbit up and slows it down, and it snaps back to the grid when the route is removed.
- [ ] **Live modulation view:** a modulated knob (orbit Depth/Offset/Rate, inspector knobs) shows an inner arc and dot
      in the LFO's colour that moves to where the LFO pushes the value right now.
- [ ] A modulated block draws its live curve as a bright LFO-coloured line over the normal curve (e.g. map an LFO to a
      Sine's Cycles and watch it squeeze), and the orbit's white dot rides that live curve.
- [ ] With **Live view** off the arcs and live curves disappear; with the LFO switched off they settle on the set value.

## 11. Look

- [ ] The washed-out gold accents (title, global buttons, scene pads, phrase LEDs, panel frames).
- [ ] The **astrolabe ornament** (rings, radial lines and ticks coming in from the bottom left) in the empty
      areas and the background: too faint, too strong, wrong place? The previous diamond-lattice style is still in
      the code (`paintLattice` in `Source/gui/Theme.h`) if you want to go back to it.

## Known limits (not bugs)

- A plugin cannot set Bitwig's tempo or start its transport; Internal BPM and Play only drive Orrery' own clock.
- Scene changes happen on the UI thread just after the boundary, so they can be a few milliseconds late.
- The MIDI export does not include LFO modulation (it does not repeat with the loop).
- Pages (orbits longer than 32 steps, shown 16 at a time) are planned for later.
- In the Standalone app, orbit 2's *Sync* was switched off during a test - turn it back on if you did not do that.
