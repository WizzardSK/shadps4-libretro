# shadPS4 libretro core

A [libretro](https://www.libretro.com/) core of [shadPS4](https://shadps4.net), the PlayStation 4 emulator, for RetroArch and other libretro frontends.

The emulator is upstream's: this repository follows [shadps4-emu/shadPS4](https://github.com/shadps4-emu/shadPS4) and adds the libretro core in `libretro/`. It holds the core only; upstream's standalone entry point, packaging, tests, documents and CI are not part of it (the paths are listed in `.upstream-excluded`). What changed in the core is in [CHANGELOG.md](CHANGELOG.md).

**State: experimental.** The core builds for Windows and Linux; it is at the stage of first tests, and games may not start at all.

## What it needs

- **A 64-bit x86 CPU with AVX2.** The emulator runs the game's code directly on the host CPU and has no recompiler, so there is no ARM build, and a native ARM frontend cannot load this core.
- **A GPU with Vulkan 1.3.**
- **Windows 10 or newer, or Linux (glibc).** The core loads into any libretro frontend there; the frontend's video driver does not matter.

## Downloads

Test builds are on the [Releases](https://github.com/WizzardSK/shadps4-libretro/releases) page; each run of the [Build libretro core](https://github.com/WizzardSK/shadps4-libretro/actions/workflows/libretro.yml) workflow also has the builds as artifacts.

- **Windows:** `shadps4_libretro.dll`
- **Linux:** `shadps4_libretro.so`

Put the core into the frontend's `cores` folder and `shadps4_libretro.info` into its `info` folder.

## Content

A dumped PS4 game as a folder; load its `eboot.bin`.

## Files

Everything the emulator reads and writes is under the frontend's system directory, in `system/shadPS4/`: its settings, logs (`log/shadps4.log`), save data, shader cache, and the folders for dumped system modules and system fonts. They are the same files and folders as in standalone shadPS4's user folder, so what is set up there can be copied over. Without dumped system fonts some games show no text or are unstable.

## How it works

The core does not run the emulator inside the frontend's process. It starts itself again in a second process and trades with it through shared memory: the picture and the sound come out, the pad and the core options go in. On Windows the second process is `rundll32.exe` loading the core; on Linux it is a small launcher carried inside the core and run from memory.

That is because the emulator needs the address range the game was built for to itself, and cannot stop a running game other than by ending its process. This way closing content ends the emulator and leaves the frontend running, another game can be loaded afterwards, and a game that crashes does not take the frontend with it.

- **Picture:** each finished frame is read back from the GPU and handed to the frontend as a software frame, at the Output Resolution set in the core options.
- **Sound:** 48 kHz stereo, the game's audio ports mixed.
- **Pads:** the frontend's ports 1 to 4 are the console's controllers, as many as the Controllers option says (one by default): buttons, both sticks, analog triggers and rumble. Select is the touchpad button.

## Core options

- **System:** PS4 Pro Mode, Console Language, Devkit Console Mode, Additional Memory, Confirm Button, Connected to Network, Controllers, Show Splash Screen
- **Video:** Output Resolution, FSR Upscaling, FSR Sharpening, FSR Sharpening Strength, VBlank Frequency, GPU Readbacks, Direct Memory Access, Readback Linear Images, Shader Cache, Show FPS Counter
- **Audio:** Volume
- **Trophies:** Trophy Notifications, Trophy Notification Position, Trophy Notification Duration
- **Logging:** Logging, Separate Log Files, Log Sync

They are the settings of standalone shadPS4's settings dialog that apply to a core, with its defaults, and take effect when the next game is started. Settings without a core option are in the emulator's config files under `system/shadPS4/`; a per-game setting there wins over the core option.

The option texts are translated through libretro's [Crowdin project](https://crowdin.com/project/libretro).

## Limits

- No save states: the emulator has none.
- The picture is copied through system memory on its way to the frontend, which costs speed at high output resolutions.

## Building

Linux, with upstream's build dependencies installed (see `.github/workflows/libretro.yml` for the list):

```
git clone --recursive https://github.com/WizzardSK/shadps4-libretro.git
cd shadps4-libretro
cmake -B build -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build --parallel
```

The standalone emulator does not build from this repository; it is upstream's.

## License

GPL-2.0-or-later, as shadPS4. See [LICENSE](LICENSE).
