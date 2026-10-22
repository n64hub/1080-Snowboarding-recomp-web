1080 Snowboarding: Recompiled - Windows test build (early)

1. Unzip this folder anywhere (use a new folder, not over an older version).
2. Run 1080SnowboardingRecompiled.exe.
3. Choose "Load ROM" and pick your own 1080 Snowboarding (USA) .z64 ROM.
   It is checked against the expected hash and copied into the app's data folder.
4. Choose "Start Game".

Settings, controls and saves live in %LOCALAPPDATA%\1080SnowboardingRecompiled.
To keep everything next to the exe instead, create an empty file named portable.txt here.

Reporting a problem: each run writes log.txt to that same folder
(%LOCALAPPDATA%\1080SnowboardingRecompiled\log.txt). Send that file after the
problem happens. 1080SnowboardingRecompiled-log.exe shows the same log live in a
console window instead.

Needs Windows 10 or 11 (64-bit) and a GPU with Vulkan (the default) or Direct3D 12 (selectable in Settings > Graphics).
The ROM is not included.

Changes in v0.1.8: Fixed the in-between frames made for 60 and 120 fps. The game
moves its near and far planes every frame, and blending them made single frames
show the hills fogged over or distant scenery drawn in front, which looked like
the background flickering in the fog.

Changes in v0.1.7: Draw Distance defaults to Maximum (the whole course is drawn,
so nothing builds in the distance and the far edge no longer flickers through
the fog). Extended now reaches further too. If v0.1.6 saved Extended, it moves
to Maximum once.
Changes in v0.1.6: fixed the crash (black screen) when a race loads: the
renderer could write a late frame over memory the game had just reused.
60 fps is the default again (Settings > Graphics > Framerate,
also set once for anyone v0.1.5 left on Original). Camera cuts no longer show
blended or broken in-between frames. New Draw Distance setting in Settings >
Graphics: Extended (default) draws about three times as far, Maximum draws the
whole course with almost no fog, Original matches the N64. A bad audio frame
no longer closes the game.
Changes in v0.1.5: Framerate defaults to Original, which removes the bad frames
on camera cuts. If you set it before, your setting is kept.
Changes in v0.1.4: log.txt includes a crash report.
Changes in v0.1.3: writes log.txt for bug reports.
Changes in v0.1.2: Match Race no longer crashes at character select, board
select or when the race loads (tested under Wine and Linux).
