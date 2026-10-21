# Player 1 frame sequences (personal build)

Frame sequences play a manually entered combo once using emulated frame boundaries,
not wall-clock timers. The first version supports Player 1 with a DualShock 2
controller and digital buttons/D-pad only. It does not record inputs.

## Create a combo

1. Open **Settings → Controllers → Controller Port 1 → Macros**.
2. Select a macro slot and click **Edit Frame Sequence…**.
3. Give the combo a name and check **Use this frame sequence instead of the
   simultaneous-button macro**.
4. Click **Add Step** for each input state. Enter button names joined with `&`,
   or select a row and click **Choose Buttons…**. Set the number of frames for
   that row. An empty button cell is saved as `Neutral`.
5. Save, then bind a separate keyboard key or digital controller button using
   the macro slot's existing **Trigger** control.

Example (not a guaranteed combo in any particular game):

| Buttons | Frames |
| --- | ---: |
| Down | 2 |
| Down & Right | 1 |
| Right & Square | 1 |
| Neutral | 5 |

Supported names: `Up`, `Right`, `Down`, `Left`, `Triangle`, `Circle`, `Cross`,
`Square`, `Select`, `Start`, `L1`, `L2`, `R1`, `R2`, `L3`, `R3`. Use `Cross`
for the PlayStation X button. `Neutral` releases all digital buttons and centers
both sticks. Inputs are full-pressure digital presses, not analog stick paths.

Directions are **absolute**: `Right` does not automatically become `Left` when
characters switch sides.

## Playback and cancellation

- Press the trigger once to play the combo. Releasing it does not cancel playback
  or cause a repeat. Holding the trigger does not loop the combo.
- Press the same trigger again to cancel. Triggering another sequence replaces
  the current sequence.
- An optional **Stop Input Sequence** hotkey is available under **Hotkeys → Speed**.
- Pausing emulation also pauses sequence progression. Frame Advance advances it
  one emulated frame at a time; its configurable hold/repeat delays do not affect
  the durations stored in the combo.
- The first input is armed immediately, including while paused. Each input remains
  active for its configured number of emulated VSync frames. No elapsed-time
  catch-up is performed.
- Player 1 live input is suppressed during playback, but its latest state is kept
  and restored on completion/cancellation. Player 2 is not overridden.
- Loading a state, resetting/stopping a game, reloading bindings/settings, or
  clearing input state on focus loss or disconnecting an input device cancels playback. Sequence progress is not
  stored in savestates.
- Playback is blocked during RetroAchievements Hardcore Mode and input
  recording/playback. If either becomes active, any running sequence is canceled.

The existing simultaneous-button macro, pressure and repeat-frequency settings
are preserved, but are not used while sequence mode is enabled for that slot.
Disable sequence mode to use the slot as a regular macro again. The trigger is
shared by both modes. Names and sequences are saved with controller settings and
copied with input profiles.

There are up to 16 macro slots, at most 256 steps per sequence, and at most 36,000
emulated frames in total. A sequence uses PCSX2's VSync frames, not the game's
internal animation frames: a 30 FPS game may sample inputs differently from a
60 FPS game. Combo success still depends on the game's state, orientation,
hit-stop and input windows.
