# Physics Simulator - User Guide

A 2D simulator of masses, springs, dampers and gravity, with collisions and bouncing walls. Simulations
run on the CPU or on an NVIDIA GPU (CUDA), and you can build your own in the built-in editor and save
them as XML.

## Contents

1. [Requirements and starting the program](#requirements-and-starting-the-program)
2. [Running a simulation](#running-a-simulation)
3. [CPU and CUDA engines](#cpu-and-cuda-engines)
4. [The editor](#the-editor)
5. [How the simulation behaves](#how-the-simulation-behaves)
6. [The XML file format](#the-xml-file-format)
7. [Troubleshooting](#troubleshooting)

---

## Requirements and starting the program

- Windows 10 or 11, 64-bit (a 32-bit build also exists, CPU only).
- For the CUDA engine: an NVIDIA GPU and a current driver. Nothing else is needed at run time.
- To build from source: Visual Studio 2022 and the CUDA Toolkit 12.6 (see the [code guide](CODE_GUIDE.md)).

Run `physics.exe` from the build output folder (for example `x64\Release`). The `models` folder next to
the executable holds the bundled simulations; keep it there. If `simulation1.xml` cannot be found the
program shows an error and exits.

The window title shows the engine in use, and the editor state when you are editing.

## Running a simulation

Pick one from the **Simulation** menu:

| Menu item | What you see |
|---|---|
| Unstable orbits | Planet-sized bodies pulling on each other |
| Will crash sim | A heavy body and a small square of spring-linked masses |
| Tearing Curtain | A cloth of masses, springs and dampers that tears under load |
| Super Gravity | Thousands of heavy bodies attracting each other |
| Snooker | Seven balls bouncing and colliding |
| Bouncy thing | A cloth of springs and dampers with other masses |
| Falling balls | A grid of several hundred balls plus one heavy body |
| Cradles of Newton | Rows of pendulum-like balls on springs |
| Thing | A few hundred masses with no springs |
| Solar system (10 Oct 2026) | The Sun, the eight planets and 21 moons, as they were on 2026-10-10 |

### The solar system model

`solar_system.xml` holds the Sun, Mercury to Neptune and 21 of the larger moons (the Moon; Phobos and Deimos;
Io, Europa, Ganymede and Callisto; Mimas, Enceladus, Tethys, Dione, Rhea, Titan and Iapetus; Miranda, Ariel,
Umbriel, Titania and Oberon; Triton and Proteus). Saturn's rings, Pluto and the minor moons are not included.

- Positions and velocities are the NASA/JPL Horizons values for 2026-10-10 00:00 TDB. Masses and radii are
  real (metres and kilograms), so every body is far smaller than a pixel until you zoom in: use the mouse
  wheel on the inner planets or on a gas giant to see its moons.
- The simulator is 2D, so each orbit is laid flat into the ecliptic plane (see the comment at the top of the
  file). Distances, speeds and where each body is along its orbit are preserved; the tilt of orbits and of
  the moon systems is not.
- It runs at 100 s per step and 100 steps per frame, about 7 days of simulated time per second on screen.
  Change *Time step* and *Steps per frame* in the editor to go faster or slower. Phobos (a 7.6 hour orbit) is
  the limit on the time step: much more than 200 s makes it unstable.
- The system is shifted to its centre of mass, so it does not drift. The world is 10^13 m across with no
  bouncing walls.
- Like the other bundled simulations, it can be opened as an editable copy from *Editor > Open bundled
  simulation*.

**Mouse**

- **Mouse wheel** zooms in and out around the cursor.
- **Left-drag** pans the view.

Choosing another simulation replaces the current one and resets the zoom and pan.

**Info label:** the top-left corner shows the engine in use, the displayed frames per second, and how much
simulated time has passed since the simulation started, for example `CUDA  |  60 FPS  |  Simulation time
28.01 d`. Time is shown in ms, s, min, h, d or yr as suits its size. In the editor the label shows `Editor`
and the engine instead.

**Colours:** masses are red, or blue if they are fixed in place; springs are green. A mass or spring with
a custom colour is drawn in that colour. Very small masses are drawn as tiny squares.

## CPU and CUDA engines

The **Engine** menu switches between **CPU** and **CUDA**. The simulation carries on from where it was
when you switch.

- If there is no usable NVIDIA GPU, the CUDA item is greyed out.
- If CUDA fails to start (or fails while running), the program shows a message and continues on the CPU.
- With more than one GPU, the one with the best double-precision speed is used (for example a Titan V is
  preferred over a GeForce card). To force a particular GPU, set the environment variable
  `PHYSICS_CUDA_DEVICE` to its index (0, 1, ...) before starting.
- CPU and CUDA produce identical results, so the choice only affects speed. CUDA is much faster for
  simulations with many bodies (for example Super Gravity).

The program never runs faster than your screen: each displayed frame advances the simulation by a fixed
number of steps (the "steps per frame" setting), so a fast engine mainly means smoother frames on big
simulations rather than a faster simulation.

## The editor

Press **F5** to open the editor, and **F5** again to run what you built. The simulation is paused while
you edit, and running never changes your document, so F5 always returns you to it as you left it.

The menu bar gains **Editor** and **Edit** menus, and a floating **Properties** window appears. The
Properties window can be moved anywhere and closed with its X; bring it back from
*Editor > Properties window*.

### Getting started

1. Press **F5** (an empty document opens), or use *Editor > New*, *Open...* or *Open bundled simulation*.
2. Press **M** and click on the canvas to place masses.
3. Press **S** (spring) and click two masses to join them. Press **V** to go back to selecting.
4. Click a mass and set its properties in the Properties window.
5. Press **F5** to run it. Press **F5** again to return, and *Editor > Save As...* (**Ctrl+Shift+S**) to keep it.

The canvas shows the world as a rectangle. Edges that masses bounce off are drawn thick and orange.

### Tools

| Key | Tool | Use |
|---|---|---|
| **V** or **Esc** | Select | Select, move and inspect things |
| **M** | Place mass | Click to add a mass using the defaults |
| **S** | Spring | Click two masses to join them with a spring |
| **D** | Damper | Click two masses to join them with a damper |
| **B** | Spring + damper | Click two masses to add both |

A new spring starts at its natural length (the current distance between the masses), so it begins with no
force. Joining a pair that already has a spring or damper only adds what is missing.

### Selecting and moving

| Action | Result |
|---|---|
| Click a mass | Select it |
| **Ctrl**+click a mass | Add it to or remove it from the selection |
| Click a spring or damper line | Select that connection (the spring and damper on that pair) |
| Drag on empty space | Draw a box to select the masses inside (hold **Ctrl** to add to the selection) |
| Click on empty space | Clear the selection |
| Drag a selected mass | Move all selected masses |
| **Shift**+drag from a mass | Set its initial velocity: drag out an arrow |
| Drag the tip of a velocity arrow | Change the velocity of that mass |
| **Middle-drag** | Pan the view |
| **Mouse wheel** | Zoom around the cursor |
| **Right-click** | Context menu: cut, copy, paste, duplicate, delete |

A velocity arrow shows where the mass will be after one second of running at 60 frames per second.
Arrows are only drawn for selected masses.

### Cut, copy, paste, duplicate, delete

| Shortcut | Action |
|---|---|
| **Ctrl+C** | Copy the selected masses and the springs and dampers between them |
| **Ctrl+X** | Cut |
| **Ctrl+V** | Paste; the copy is centred on the mouse (or on the view centre if the mouse is not over the canvas) and becomes the selection |
| **Ctrl+D** | Duplicate the selection, slightly offset |
| **Delete** | Delete the selection (connections attached to deleted masses go with them) |
| **Ctrl+A** | Select all masses |

A connection is only copied when both its masses are copied. The clipboard holds ordinary simulation XML
text, so you can paste between two running copies of the program, or into a text editor to look at it.

### Undo and redo

**Ctrl+Z** undoes and **Ctrl+Y** (or **Ctrl+Shift+Z**) redoes. A whole drag, or one edit in the
Properties window, is a single step. The last 50 steps are kept.

### The Properties window

Edit a value, then press **Enter**, press **Tab**, or click elsewhere to apply it. **Esc** cancels the edit.
A line at the bottom confirms what was changed or tells you what was wrong. A field that is blank shows
that the selected items have different values; typing a value sets them all.

**Selected masses** (changes every selected mass)

| Field | Meaning |
|---|---|
| Mass (kg) | Must be greater than 0 |
| Radius | Used for drawing, collisions and bouncing |
| Elasticity | How bouncy collisions are (1 = fully elastic; two masses multiply their values) |
| x, y | Position |
| Velocity x, Velocity y | Initial velocity |
| Fixed in place | The mass does not move under forces |
| Colour | Click the swatch to choose from the palette and spectrum; **Default** goes back to red/blue |

**Selected spring / damper**

| Field | Meaning |
|---|---|
| Spring k | Stiffness |
| Rest length | Natural length of the spring |
| Break below / above (x length) | The spring breaks if it is compressed below, or stretched beyond, this multiple of its rest length. Defaults: 0.5 and 1.5 |
| Colour | Colour of the spring line |
| Damper k | Damping strength |

**World**

| Field | Meaning |
|---|---|
| x min / x max / y min / y max | The edges of the world. y max is the bottom of the screen |
| Time step | Simulated seconds per step |
| Steps per frame | How many steps run for each displayed frame |
| Gravity min mass | Gravity only acts between two masses whose combined mass is at least this (default 10000); lower it to include small masses, raise it to ignore them |
| Bounce off these edges | Left, Right, Top, Bottom. Only Bottom is on by default |

**Defaults for newly placed items** set the mass, radius, elasticity, spring k and damper k used by the
next masses and connections you add. They do not change anything already placed. (To resize an existing
mass, select it and change *Radius* in the *Selected masses* section.)

### Files

| Command | Shortcut | What it does |
|---|---|---|
| New | Ctrl+N | An empty document |
| Open... | Ctrl+O | Open any simulation XML file |
| Open bundled simulation | | Opens one of the nine bundled simulations as an editable copy |
| Save | Ctrl+S | Save to the current file (asks for a name the first time) |
| Save As... | Ctrl+Shift+S | Save under a new name |

The title bar shows the file name, with a `*` when there are unsaved changes. The program asks before
discarding changes. Bundled simulations open as copies, so saving never overwrites them.

### Shortcut summary

| Key | Action |
|---|---|
| F5 | Run / return to the editor |
| V, M, S, D, B, Esc | Tools |
| Ctrl+N / O / S / Shift+S | New / Open / Save / Save As |
| Ctrl+Z / Y | Undo / redo |
| Ctrl+X / C / V / D / A | Cut / copy / paste / duplicate / select all |
| Delete | Delete selection |

Keyboard shortcuts work while the canvas has focus. After typing in the Properties window, click the
canvas (or press **Esc** in the field) to give focus back.

## How the simulation behaves

Everything uses plain numbers; pick any consistent units (the bundled simulations use metres, kilograms
and seconds). Each step does the following, in order:

1. **Breaking:** a spring whose length is outside its break limits breaks permanently, and so do any
   dampers on the same pair of masses.
2. **Gravity:** every pair of masses attracts with `G m1 m2 / d^2`, where `G = 6.6743e-11`, but only if
   their combined mass is at least *Gravity min mass*.
3. **Springs:** a spring pulls or pushes with a force `k (length - rest length)` along its line.
4. **Dampers:** a damper resists the relative velocity of its two masses along the line between them,
   with strength `k`.
5. **Motion:** acceleration is force divided by mass; then velocity is updated, then position
   (so the position uses the new velocity). Fixed masses do not move.
6. **Collisions:** masses that overlap are pushed apart and exchange momentum in an elastic collision
   scaled by their elasticities. All overlaps in a step are judged from the positions at the start of the
   collision stage.
7. **Walls:** for each enabled edge, a mass that has crossed it has its velocity pointing outward reversed
   and reduced by 8%, and the velocity along the wall reduced by 20%. The mass is not moved back; it simply
   cannot keep going outward.

Things to know:

- Only the bottom edge bounced in the original simulations, which is why it is the default.
- Fixed masses ignore forces but can still be nudged by collisions.
- Very large time steps make springs and fast collisions unstable. If a simulation explodes, reduce the
  time step or increase the steps per frame instead.
- Broken springs disappear from the display and never re-form.

## The XML file format

A simulation is one `<simulation>` element. Optional attributes may be left out.

```xml
<?xml version="1.0" encoding="utf-8"?>
<simulation xmin="-1000" xmax="1000" ymin="-1000" ymax="1000" dt="0.1" iterations="10"
            min_gravitational_mass="10000"
            bounce_left="true" bounce_right="true" bounce_top="true" bounce_bottom="true">
  <masses>
    <mass m="100" r="20" x="0" y="0" e="0.95" vx="5" vy="0" fixed="false" colour="#FF8000"/>
    <mass m="100" r="20" x="300" y="0"/>
  </masses>
  <springs>
    <spring a="0" b="1" k="10" l0="300" min="0.5" max="1.5" colour="#00FF00"/>
  </springs>
  <dampers>
    <damper a="0" b="1" k="0.5" l0="300"/>
  </dampers>
</simulation>
```

| Element / attribute | Required | Default | Notes |
|---|---|---|---|
| `xmin xmax ymin ymax` | yes | | World edges; needs max > min. y grows downward on screen |
| `dt` | yes | | Time step, greater than 0 |
| `iterations` | yes | | Steps per frame, a whole number of at least 1 |
| `min_gravitational_mass` | no | 10000 | |
| `bounce_left/right/top` | no | false | `true`/`false`/`1`/`0` |
| `bounce_bottom` | no | true | |
| mass `m r x y` | yes | | `m` greater than 0, `r` 0 or more |
| mass `e vx vy fixed colour` | no | 1, 0, 0, false, none | `colour` is `#RRGGBB` |
| spring `a b k l0` | yes | | `a`, `b` are zero-based indices into `<masses>` |
| spring `min max colour` | no | 0.5, 1.5, none | Break limits as multiples of `l0` |
| damper `a b k` | yes | | |
| damper `l0` | no | 0 | Stored but not used by the forces |

The loader is strict: unknown elements or attributes, bad numbers, mass indices out of range, or a
spring or damper joining a mass to itself are rejected with the file name and line number.

## Troubleshooting

| Problem | Try |
|---|---|
| "Cannot load simulation" at start | Make sure the `models` folder sits next to `physics.exe` |
| CUDA is greyed out | No NVIDIA GPU or driver was found |
| CUDA failed and the program switched to CPU | Update the GPU driver; set `PHYSICS_CUDA_DEVICE` to choose another GPU |
| Keyboard shortcuts do nothing in the editor | Click the canvas first; focus may be in the Properties window |
| A radius or mass change "did nothing" | Check you edited the *Selected masses* section, not the *Defaults* section, and pressed Enter or Tab |
| A simulation explodes | Reduce the time step, or raise steps per frame, or soften springs (lower k) |
| An XML file will not open | The message names the line and attribute that failed |
