# Player 1 input timeline (personal build)

A separate, non-modal, TAStudio-inspired window for manually entering frame-perfect
Player 1 combos. Open **Tools → Player 1 Input Timeline…**, or press
**Ctrl+Alt+M** while PCSX2 is active. Opening it again raises the same window.
The main emulator window remains usable; no controller-settings dialog is needed.

This is an input editor/player, **not** a full TAS editor: there is no rewind,
rerecording, automatic input capture, or seek-to-frame emulation.

## Enter a combo

1. Select one of the 16 **Combo** slots and enter a name.
2. **Compact frames** is enabled by default. Consecutive identical inputs are
   combined into a single row: for example, Neutral for 20 frames or Down + Square
   held for 4 frames. **Frame range** shows the actual start/end frames and
   **Frames** shows the duration. Double-click **Frames** to edit that duration.
   Click button checkboxes to change the inputs for the **whole compacted row**.
   Identical neighboring rows merge automatically after editing, except for rows
   explicitly created with **Insert Frame**.
3. Use the frame-count box with **Append Neutral** or **Insert Frame**
   to add frames. **Insert Frame** always adds its own editable row after the
   final frame in the timeline, regardless of the current selection—even when
   its neutral inputs match the preceding block. With the count set to 1,
   this inserts a single standalone frame and selects it for editing. A larger
   count creates a separate block of that duration. These row separations survive
   view changes and Save/reopen; they never affect playback timing or input-group
   limits. **Append Neutral** still extends an ordinary trailing neutral block.
   Select rows and use **Delete Selected Rows** to remove their entire ranges. At least one neutral frame always remains.
   To edit or delete individual frames inside a held/neutral range, uncheck
   **Compact frames** first: every frame is then shown separately, with the same button
   controls. Switching views never changes any inputs or their timing.
4. Click **Save** to persist the selected combo to the global Player 1 macro slot.
   You can test a draft without saving it.

Example (not a guaranteed combo in any particular game):

| Frame range | Frames | Checked columns |
| --- | ---: | --- |
| 1–2 | 2 | Down |
| 3 | 1 | Down, Right |
| 4 | 1 | Right, Square |
| 5–9 (Neutral) | 5 | None |

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
- **Follow playhead:** keep the row containing the prepared input frame in view. The frame marker
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
