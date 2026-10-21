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

Changes in v0.1.4: log.txt includes a crash report.
Changes in v0.1.3: writes log.txt for bug reports.
Changes in v0.1.2: Match Race no longer crashes at character select, board
select or when the race loads (tested under Wine and Linux).
