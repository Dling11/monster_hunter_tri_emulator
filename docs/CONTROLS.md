# Controls

## Android and shared touch controls

The existing A/B/X/Y, L/R, ZL/ZR, D-pad, stick and A+X preset remain available.
Combo 5 adds **R + X + A** by default. Select **Gear > Combos** to change any of
five slots' component buttons and visibility. Changes save automatically.

Touch contacts are tracked independently, including three-finger combinations,
sliding and two contacts holding one button. A combo contributes all of its
selected buttons for as long as it is held. Releasing one input does not release
a button still held by another input.

Pressed buttons shrink slightly and light up. A combo lights its component
buttons too. **Controls > Edit touch layout** keeps movement, size, opacity and
visibility settings available.

The touch buttons use the original Wii Classic **+** and **-** labels.
Controller/weapon settings determine what each action does in the game.
Underwater, **ZL** moves toward the surface and **ZR** moves downward; see
[Capcom's Tri manual](https://static.capcom.com/monsterhunter/tri_manual.pdf).

## Single-player game speed (dev.4)

Tap the **1x** touch button to cycle **1x > 1.25x > 1.5x > 1x**, or choose a
rate in **Gear > System > Game speed**. The speed button can be moved/resized
in the existing touch-layout editor. On a keyboard, **F3** cycles speed while
the app settings are closed.

This experimental control speeds up game simulation, including movement,
animations and game timers; changing the FPS cap alone does not do this.
Audio and movie playback retain their normal timing. Faster movement still
needs testing in real quests and is not a performance boost for slower phones.

Speed-up is available only in Moga Village/free roam and single-player quests.
The game thread checks the mode every frame. City, online quests (even alone),
Arena, title/connection screens and unknown states force 1x. Leaving solo play,
backgrounding the app or restarting resets speed to 1x; returning to solo play
does not automatically enable it again. Speed is not saved to disk.

## Save tools

Save inside Tri first; these tools copy saved files, not a live save state.

| Tool | Purpose |
| --- | --- |
| Export save | Copy for another device. |
| Restore / import | Choose a saved copy, replace this app's saved progress and restart. |
| Create backup | Keep a local safety copy; retain the newest three. |

Export and backup ask **Yes / No** and keep the current save in place. No or
Escape cancels. Taps behind a confirmation cannot create additional copies.
Backups show their creation date and local time down to seconds, for example
**25 Oct 2026, 06:45:12 PM (local time)**. That is when the copy was created;
save inside Tri first to include your latest progress.

Creating a fourth backup removes the oldest after the new copy is verified.
Automatic backups before restore count toward the same three-copy limit.
Exports are separate and are not removed by that limit.

**Restore / import** opens a list with **My backups** and **Other saves
(imports / exports)**. Select the copy you want, choose **Review selected copy**,
then confirm **Yes, restore**. Nothing is selected automatically. No returns to
the list; Cancel closes it without writing saved files. A confirmed restore is
staged, the game restarts, and the current saved progress is backed up before
replacement. Unsaved play is lost. Use Tri's normal **Continue** afterward;
this does not return to the exact screen as a save state would.

To transfer progress, export and move the entire exported folder to the other
device's **MH3_Rowell_Dev/imports** folder (create imports if needed). It appears
under **Other saves**.
Compatible folders already in **exports** appear there too. The exported
folder includes an English README. The app does not open a system file picker
for import; place the folder in imports first.

## Frame rate

**Gear > Graphics** allows 30/60/90/120 FPS in Village and solo quests. City and
online quests keep the 30 FPS cap. Dev.4 fixes an inherited check that confused
Moga Village with City. V-Sync retains the selected limit. This is a frame cap;
selecting 60 FPS does not guarantee the device can render at 60 FPS.

## Windows keyboard

| Key | Controller input |
| --- | --- |
| W / A / S / D | Move forward / left / backward / right |
| I / J / K / L | Camera up / left / down / right |
| F | A / confirm or action |
| X | X / attack |
| Space or Backspace | B / evade or cancel |
| E | Y / item or weapon action |
| Q | L |
| Left/right Shift or R | R |
| Z / C | ZL / ZR |
| Enter | + |
| Tab | - |
| Arrow keys | D-pad |
| Home | Wii Home button |
| F2 | Shared touch settings |
| F3 | Cycle solo-play speed: 1x / 1.25x / 1.5x |
| Escape | Close settings |
| F1 | Original PC settings |

Hold **Shift + X + F** for the R + X + A combination. WASD controls the character
once you are in gameplay; a title screen or an open settings menu does not move
the character. These are the defaults.

On **Windows only**, open **F2 > Controls > Keyboard bindings**. Select an action's
key slot and press a new key; each action supports up to three alternatives.
Movement, camera, all Wii buttons and the five configured combo slots can be
assigned. A combo key holds that slot's selected buttons together.

Changes save automatically in **MH3_Rowell_Dev/keyboard.cfg**, separately from
character saves, backups and exports. Missing or invalid configuration falls
back to defaults. **Reset** restores an action; **Reset all defaults** restores
everything. Conflicting assignments show a message: clear the previous slot
first. **Clear** unassigns a slot and **Escape** cancels capture. F1/F2/F3 and
Escape stay reserved for settings, speed and cancellation. The mouse can always
operate this editor. This keyboard editor is absent from Android.

## Windows mouse and gamepad

While touch controls are shown, the mouse clicks/drags those controls. You can
use a mouse on one combo and the keyboard for movement. A normal mouse does not
simulate three separate fingers.

Hide touch controls for the original mouse camera/attacks: left mouse = X,
right mouse = A and middle mouse = R. Gamepads keep their own input; the shared
Controls page offers Nintendo/Xbox layouts and shoulder/trigger options.

English app settings and the game's language remain separate settings.
