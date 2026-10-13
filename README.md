# shadPS4 libretro core

A [libretro](https://www.libretro.com/) core of [shadPS4](https://shadps4.net), the PlayStation 4 emulator, for RetroArch and other libretro frontends.

The emulator is upstream's: this repository follows [shadps4-emu/shadPS4](https://github.com/shadps4-emu/shadPS4) and adds the libretro core in `libretro/`. It holds the core only; upstream's standalone entry point, packaging, tests, documents and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's; the core is currently on upstream shadPS4 0.19.1 WIP, commit [3a9f572](https://github.com/shadps4-emu/shadPS4/commit/3a9f57265d28975658f1ca941daa5c879c7a74bd) of 11 October 2026. What changed in the core is in [CHANGELOG.md](CHANGELOG.md).

The core is experimental: it is at the stage of first tests, and games may not start at all.

## Downloads

Builds for Windows and Linux (x86_64) are on the [Releases](https://github.com/WizzardSK/shadps4-libretro/releases) page.

- **Windows:** `shadps4_libretro.dll`, Windows 10 or newer.
- **Linux:** `shadps4_libretro.so`, glibc 2.39 or newer.

Put the core into RetroArch's `cores` folder and `shadps4_libretro.info` into its `info` folder.

There are no other builds, and there will not be: the emulator runs the game's code directly on the host CPU and has no recompiler, so it needs a 64-bit x86 CPU, with AVX2. A native ARM frontend cannot load this core.

## Video

The emulator renders with Vulkan and needs a GPU with Vulkan 1.3. RetroArch's video driver does not matter: the core hands over finished frames, at the size set by the core option Output Resolution, so it runs under `vulkan`, `glcore`, `gl` or `d3d11` alike.

The frames are copied through system memory on their way to RetroArch, which costs speed at high output resolutions.

On Linux the core option GPU Frame Hand-over does away with that: RetroArch draws the emulator's finished image itself, and nothing is copied. It needs RetroArch's `vulkan` video driver; the emulator then uses the GPU RetroArch is on, where there are several. It is off by default, because where it cannot be done there is no picture at all - a message then says why, and the option has to be turned off again.

## Content

A dumped PS4 game as a folder; load its `eboot.bin`.

## System modules and fonts

The PS4 has no BIOS file, and the core starts without one. What takes its place are system modules and fonts dumped from a console's firmware, and some games do not start without them:

- **System modules** (`.sprx` files) go into `system/shadPS4/sys_modules/`.
- **Fonts** go into `system/shadPS4/fonts/font/` and `system/shadPS4/fonts/font2/`, the console's two font folders. Without them some games show no text or are unstable.

The core creates these folders the first time a game is started. They have to be dumped from your own console; the core does not download them.

The emulator has its own replacement for some modules and uses it when the dumped file is missing: `libSceLibcInternal`, `libSceNgs2`, `libSceAvPlayer`, `libSceRtc`, `libSceJpegEnc`, `libScePngDec`, `libScePngEnc`, `libSceFont`, `libSceFontFt`, `libSceRudp`, `libSceSystemGesture`. The dumped module is still the better choice where there is one.

For the others there is no replacement, and a game that calls into one that is missing usually stops: `libSceUlt`, `libSceJpegDec`, `libSceJson`, `libSceJson2`, `libSceCesCs`, `libSceFreeTypeOt`, `libSceFreeTypeOl`, `libSceFreeTypeOptOl`, `libSceAvPlayerStreaming`, `libSceAt9Enc`, `libSceAudiodec` and its `libSceAudiodecCpu*` modules, `libSceBeisobmf`, `libSceBemp2sys`, `libSceWkFontConfig`, `libScePsmKitSystem`, `libSceDepth`, `libScePadTracker`, `libSceMoveTracker`, `libSceXml`.

When a game stops at start, look in `system/shadPS4/log/shad_log.txt` for these lines. They name what is missing:

```
No HLE available for libSceJpegDec.sprx module
Can't Load libSceLibcInternal.sprx switching to HLE
No dumped system fonts, expect missing text or instability
```

## Files

Everything the core keeps is in `system/shadPS4/`, laid out as standalone shadPS4's user folder, so what is set up there can be copied over: settings, save data, the shader cache, the folders for dumped system modules and fonts, and the emulator's logs in `log/`: `shadps4.log` covers the start, and from the point a game is loaded the log is `shad_log.txt` (or a file named after the game's ID with the core option Separate Log Files).

## Controls

RetroArch's ports 1 to 4 are the console's controllers, as many as the core option Controllers says (one by default): buttons, both sticks, analog triggers and rumble. Select is the touchpad button. RetroArch's Controls menu shows the buttons under their PS4 names, for remapping.

## Core options

- **System:** PS4 Pro Mode, Console Language, Devkit Console Mode, Additional Memory, Confirm Button, Connected to Network, Controllers, Show Splash Screen
- **Video:** Output Resolution, FSR Upscaling, FSR Sharpening, FSR Sharpening Strength, VBlank Frequency, GPU Readbacks, Direct Memory Access, Readback Linear Images, Copy GPU Buffers, GPU Frame Hand-over, Shader Cache, Show FPS Counter
- **Audio:** Volume
- **Trophies:** Trophy Notifications, Trophy Notification Position, Trophy Notification Duration
- **Logging:** Logging, Separate Log Files, Log Sync

They are the settings of standalone shadPS4's settings dialog that apply to a core, with its defaults, and take effect when the next game is started. The one default that differs is Copy GPU Buffers, on in the core: its picture reaches RetroArch a frame or two late, which makes the fault that setting gets around a regular one. Settings without a core option are in the emulator's config files under `system/shadPS4/`; a per-game setting there wins over the core option. The option texts are translated through libretro's [Crowdin project](https://crowdin.com/project/libretro).

There are no save states: the emulator has none.

## How it works

The core does not run the emulator inside RetroArch's process. It starts itself again in a second process and trades with it through shared memory: the picture and the sound come out, the pads and the core options go in. On Windows the second process is `rundll32.exe` loading the core; on Linux it is a small launcher carried inside the core and run from memory.

That is because the emulator needs the address range the game was built for to itself, and cannot stop a running game other than by ending its process. This way closing content ends the emulator and leaves RetroArch running, another game can be loaded afterwards, and a game that crashes does not take RetroArch with it.

## Building

Linux, with upstream's build dependencies (the list is in `.github/workflows/libretro.yml`), from a checkout with its submodules:

```
git clone --recursive https://github.com/WizzardSK/shadps4-libretro.git
cd shadps4-libretro
cmake -B build -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel
```

The CI workflow `.github/workflows/libretro.yml` builds both platforms. `.gitlab-ci.yml` is the recipe for libretro's buildbot, and `.github/workflows/gitlab-preflight.yml` replays it.

## License

GPL-2.0-or-later, as shadPS4. See [LICENSE](LICENSE).
