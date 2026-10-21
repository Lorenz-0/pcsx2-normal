# Player 1 input timeline (personal build)

A separate, non-modal, TAStudio-inspired window for manually entering frame-perfect
Player 1 combos. Open **Tools → Player 1 Input Timeline…**, or press
**Ctrl+Alt+M** while PCSX2 is active. Opening it again raises the same window.
The main emulator window remains usable; no controller-settings dialog is needed.

This is an input editor/player, **not** a full TAS editor: there is no rewind,
rerecording, automatic input capture, or seek-to-frame emulation.

## Enter a combo

1. Select one of the 16 **Combo** slots and enter a name.
2. Each grid row is **one emulated frame**. Click a button cell to toggle its
   checkbox, or select it and press Space. Check multiple columns for a chord.
   An entirely unchecked row is **Neutral**.
3. Use the frame-count box with **Append Neutral** or **Insert Before Selection**
   to add rows. Select cells across a range of rows and use **Delete Selected
   Frames** to remove those frames. At least one neutral frame always remains.
4. Click **Save** to persist the selected combo to the global Player 1 macro slot.
   You can test a draft without saving it.

Example (not a guaranteed combo in any particular game):

| Frame | Checked columns |
| ---: | --- |
| 1 | Down |
| 2 | Down |
| 3 | Down, Right |
| 4 | Right, Square |
| 5–9 | None (Neutral) |

Supported buttons: `Up`, `Right`, `Down`, `Left`, `Triangle`, `Circle`, `Cross`,
`Square`, `Select`, `Start`, `L1`, `L2`, `R1`, `R2`, `L3`, `R3`. `Cross` is the
PlayStation X button. Neutral releases all digital buttons and centers both
sticks. Inputs are full-pressure digital presses, not analog stick paths.
Directions are **absolute**: Right does not automatically become Left when
characters switch sides.

## Test from the same window

Start a game with a Player 1 DualShock 2 controller, then use:

- **Arm + Pause:** prepare frame 1 immediately and pause emulation. Click
  **Frame Advance** to consume one emulated frame at a time.
- **Play / Restart:** start the displayed timeline from frame 1 and resume
  emulation. This does not rewind the game: use your normal savestate controls
  beforehand if you want to repeat from the same game state.
- **Pause / Resume:** pause or continue emulation without restarting the combo.
- **Stop:** cancel the override and restore live Player 1 input. This does not
  change whether emulation is paused.
- **Follow playhead:** keep the prepared input frame in view. The frame marker
  and status are refreshed asynchronously; they do not control input timing.

Edits during playback apply only on the next Arm/Play. Closing the tool cancels
playback and hides the window, retaining its current draft until you reopen it.
Save before exiting PCSX2; unsaved drafts do not survive application exit or
main-window recreation. Switching combo slots asks to Save, Discard, or Cancel.

## Optional bound trigger and input profiles

Saving enables sequence mode on the corresponding **global** Player 1 macro slot.
To bind a trigger, open **Settings → Controllers → Controller Port 1 → Macros**
and set that slot's existing **Trigger**. The old **Edit Frame Sequence…** button
also opens this same standalone window.

- Press the trigger once to play. Release does not cancel or repeat the combo;
  holding it does not loop.
- Press the same trigger again to cancel; another sequence trigger replaces it.
- **Stop Input Sequence** is also available under **Hotkeys → Speed**.
- Existing simultaneous-button, pressure and repeat-frequency settings remain
  stored but are not used while sequence mode is enabled. Disable sequence mode
  in the controller macro settings to return to a regular simultaneous macro.

The tool edits global slots, even when opened from an input-profile editor.
An active input profile can override the saved slot's bound trigger or sequence.
The tool's **Arm/Play always previews its displayed draft directly**, independently
of that profile. Saving reloads settings and therefore stops current playback.

## Timing, safety and limits

- Timing uses emulated VSync boundaries, never elapsed wall-clock time. The first
  input is armed immediately, including while paused. Frame Advance hold/repeat
  delays do not change the stored input durations.
- Player 1 live input is suppressed during playback; its latest state is restored
  on completion or cancellation. **Player 2 is not overridden.**
- Loading a state, resetting/stopping a game, reloading bindings/settings, clearing
  input state on focus loss, or disconnecting an input device cancels playback.
  Sequence progress is not saved in savestates.
- RetroAchievements Hardcore Mode and input recording/playback block sequences.
  Enabling either also cancels an already running sequence.
- Up to **36,000 frames** and **256 consecutive input groups** are supported per
  combo. Identical consecutive rows are stored as one group; editing into more
  than 256 groups produces a Save/Play error, not partial playback.

These are PCSX2 VSync frames, not necessarily the game's animation frames. A
30 FPS game may sample inputs differently from a 60 FPS game. Combo success still
depends on game state, orientation, hit-stop and input windows.
