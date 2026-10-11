# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream shadPS4 are in upstream's own history; a merge is noted here with the upstream commit it brought in.

## Test build 1 - 2026-10-11

The first build of the core. Based on upstream shadPS4 3a9f572. It has been built for Windows and Linux x86_64 and not yet run on real hardware by anyone: games may not start at all.

### The core

- The emulator builds as a libretro core, `shadps4_libretro`. Loading a dumped game's `eboot.bin` starts it.
- The core does not run the emulator inside the frontend's process. It starts itself again in a second process and trades with it through shared memory: the picture and the sound come out, the pads and the core options go in, the pads' motors come back. On Windows the second process is `rundll32.exe` loading the core; on Linux it is a small launcher carried inside the core and run from memory.
- Closing content ends that process and leaves the frontend running. Another game can be loaded afterwards.
- A game that crashes, or quits on its own, does not take the frontend with it: the frontend is told to close the content, and after a crash a message says so.
- The emulator's process ends with the frontend however the frontend ends.
- Everything the emulator writes (settings, logs, saves, shader cache) goes under the frontend's system directory, in `shadPS4/`.

### Picture, sound and pads

- The picture is shown by the frontend. The renderer draws into images of its own in place of a swapchain, and each finished frame is read back from the GPU and handed over as a software frame, so it does not depend on the frontend's video driver. The emulator's own window is still created, but hidden.
- The picture is the Output Resolution exactly, and that is also the largest picture the frontend is told to expect.
- The sound is played by the frontend. Each of the game's audio ports is converted to 16-bit stereo at 48 kHz and they are mixed; every frame the frontend gets one frame's worth, with silence for what is missing.
- Up to four controllers: the Controllers option says how many of the frontend's ports are connected to the console (one by default). Buttons, both sticks, analog triggers and rumble; Select is the touchpad button. SDL's own gamepad handling is off in the core, so a pad is not read twice.

### Core options

- Twenty-five options in five categories, set on top of the emulator's own configuration when a game starts, with the standalone's defaults. System: PS4 Pro Mode, Console Language, Devkit Console Mode, Additional Memory, Confirm Button, Connected to Network, Controllers, Show Splash Screen. Video: Output Resolution, FSR Upscaling, FSR Sharpening, FSR Sharpening Strength, VBlank Frequency, GPU Readbacks, Direct Memory Access, Readback Linear Images, Shader Cache, Show FPS Counter. Audio: Volume. Trophies: Trophy Notifications, their position and duration. Logging: Logging, Separate Log Files, Log Sync.
- The option texts can be translated through libretro's Crowdin project: `intl/` scripts, the upload and sync workflows, and `libretro_core_options_intl.h` (all English until the first sync).

### Repository and builds

- The repository holds the core only: upstream's standalone entry point, packaging files, tests, documents and contributor files are removed (`.upstream-excluded`), and the README describes the core.
- CI builds the core for Linux and Windows x86_64 (`.github/workflows/libretro.yml`), in place of upstream's workflow.
- A recipe for libretro's buildbot (`.gitlab-ci.yml`), Linux x64, and a GitHub workflow that replays it (`gitlab-preflight.yml`). The core info file is in libretro's full format.

### Known limits

- x86-64 only: the emulator runs the game's code directly on the host CPU.
- The picture is copied through system memory on its way to the frontend, which costs speed at high output resolutions.
- No save states: the emulator has none.
- Core options take effect when the next game is started, not while one runs. A setting without a core option is in the emulator's own config files under `system/shadPS4/`, and a per-game setting there wins over the core option.
