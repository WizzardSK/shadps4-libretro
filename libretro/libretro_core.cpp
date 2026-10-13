// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The libretro core, the part in the frontend's process. It does not run the emulator: it
// starts this same module again in a process of its own (host.cpp) and passes the frontend's
// side of things through the shared memory of shared_state.h.
//
// The emulator runs the game's code directly on the host CPU, at the addresses the game was
// built for, and has no way to stop a running game short of ending its process - upstream's
// own exit is std::quick_exit. In a process of its own it can have its address range to itself,
// closing content is ending that process, the next game is a new one, and a game that crashes
// does not take the frontend with it.
//
//   Windows   rundll32.exe "<this dll>",retro_host_main <name of the file mapping>
//   elsewhere a small launcher, built into the core and run from memory (host_main.c), which
//             loads the core and calls retro_host_main with the shared memory's descriptor
//
// Picture: the emulator's renderer reads each finished frame back and puts it in the shared
// memory; retro_run hands the newest one over as a software frame, from where it is.
// Sound: the emulator mixes its audio ports into a ring; retro_run takes one frame's worth and
// fills what is missing with silence.
// Pads: the buttons, sticks and triggers of as many ports as the Controllers option says go
// in, the motors come back.
// Core options: read when a game is loaded and passed along; the emulator puts them on top of
// its own configuration. The texts are in libretro_core_options.h, translated through Crowdin.
//
// Not there: save states (the emulator has none), and the emulator's picture without the copy
// through system memory.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "common/scm_rev.h"
#include "core/libraries/pad/pad.h"
#include "libretro.h"
#include "libretro_core_options.h"
#include "shared_state.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

// The launcher (host_main.c), embedded by embed_host.cmake.
extern "C" const unsigned char g_libretro_host[];
extern "C" const unsigned long g_libretro_host_size;
#endif

namespace {

using Libretro::SharedState;

retro_environment_t environ_cb = nullptr;
retro_video_refresh_t video_cb = nullptr;
retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_input_poll_t input_poll_cb = nullptr;
retro_input_state_t input_state_cb = nullptr;
retro_log_printf_t log_cb = nullptr;

constexpr unsigned kWidth = 1280;
constexpr unsigned kHeight = 720;
constexpr double kFps = 60.0;
constexpr int kSampleRate = 48000;
constexpr int kAudioFrames = kSampleRate / 60;

SharedState* shared = nullptr;
bool emulator_running = false;
bool shutdown_requested = false;

#ifdef _WIN32
HANDLE shared_mapping = nullptr;
HANDLE emulator_process = nullptr;
HANDLE emulator_job = nullptr;
#else
int shared_fd = -1;
pid_t emulator_pid = -1;
#endif

// The frame handed to the frontend: a slot of the shared memory, held for as long as it is
// shown (shared_state.h, frame_held), or blank_frame until the emulator has drawn one.
const void* shown_frame = nullptr;
std::vector<uint32_t> blank_frame;
unsigned shown_width = 0;
unsigned shown_height = 0;
uint32_t shown_latest = 0;

// The Output Resolution the game was loaded with: the size frames come at, and so the largest
// the frontend is told to expect. No larger - a frontend sizes its shader passes by it.
unsigned output_width = kWidth;
unsigned output_height = kHeight;

struct retro_rumble_interface rumble {};

void Log(enum retro_log_level level, const char* fmt, ...) {
    char text[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    if (log_cb) {
        log_cb(level, "[shadPS4] %s\n", text);
    } else {
        std::fprintf(stderr, "[shadPS4] %s\n", text);
    }
}

std::string SystemDirectory() {
    const char* dir = nullptr;
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir && *dir) {
        std::string path = dir;
        if (path.back() != '/' && path.back() != '\\') {
            path += '/';
        }
        return path + "shadPS4";
    }
    return {};
}

std::string OptionValue(const char* key, const char* fallback) {
    struct retro_variable variable = {key, nullptr};
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) && variable.value) {
        return variable.value;
    }
    return fallback;
}

void ReadCoreOptions(Libretro::SharedOptions& options) {
    const auto enabled = [](const char* key, const char* fallback) -> uint32_t {
        return OptionValue(key, fallback) == "enabled" ? 1 : 0;
    };
    options.neo_mode = enabled("shadps4_neo_mode", "disabled");
    options.circle_enter = OptionValue("shadps4_confirm_button", "cross") == "circle" ? 1 : 0;
    options.network = enabled("shadps4_network", "disabled");
    options.show_splash = enabled("shadps4_show_splash", "disabled");

    options.width = kWidth;
    options.height = kHeight;
    unsigned width = 0;
    unsigned height = 0;
    const std::string resolution = OptionValue("shadps4_resolution", "1280x720");
    if (std::sscanf(resolution.c_str(), "%ux%u", &width, &height) == 2 && width >= 640 &&
        width <= Libretro::MAX_WIDTH && height >= 360 && height <= Libretro::MAX_HEIGHT) {
        options.width = width;
        options.height = height;
    }

    options.fsr = enabled("shadps4_fsr", "disabled");
    options.rcas = enabled("shadps4_rcas", "enabled");
    options.vblank_frequency =
        static_cast<uint32_t>(std::atoi(OptionValue("shadps4_vblank_frequency", "60").c_str()));
    const std::string readbacks = OptionValue("shadps4_readbacks", "disabled");
    options.readbacks = readbacks == "precise" ? 2 : readbacks == "relaxed" ? 1 : 0;
    options.direct_memory_access = enabled("shadps4_direct_memory_access", "disabled");
    options.show_fps = enabled("shadps4_show_fps", "disabled");
    options.volume = std::clamp(std::atoi(OptionValue("shadps4_volume", "100").c_str()), 0, 500);
    options.pads = static_cast<uint32_t>(std::clamp(
        std::atoi(OptionValue("shadps4_controllers", "1").c_str()), 1,
        static_cast<int>(Libretro::MAX_PADS)));

    options.console_language =
        std::clamp(std::atoi(OptionValue("shadps4_console_language", "1").c_str()), 0, 30);
    options.dev_kit = enabled("shadps4_dev_kit", "disabled");
    options.extra_dmem_mbytes =
        std::clamp(std::atoi(OptionValue("shadps4_extra_memory", "0").c_str()), 0, 20000);
    options.rcas_attenuation =
        std::clamp(std::atoi(OptionValue("shadps4_rcas_attenuation", "250").c_str()), 0, 3000);
    options.readback_linear_images = enabled("shadps4_readback_linear_images", "disabled");
    options.shader_cache = enabled("shadps4_shader_cache", "disabled");
    options.copy_gpu_buffers = enabled("shadps4_copy_gpu_buffers", "enabled");
    options.trophy_popup = enabled("shadps4_trophy_notifications", "enabled");
    const std::string side = OptionValue("shadps4_trophy_position", "right");
    options.trophy_side = side == "left" ? 0 : side == "top" ? 2 : side == "bottom" ? 3 : 1;
    options.trophy_seconds = static_cast<uint32_t>(
        std::clamp(std::atoi(OptionValue("shadps4_trophy_duration", "6").c_str()), 1, 10));
    options.log_enable = enabled("shadps4_log", "enabled");
    options.log_separate = enabled("shadps4_log_separate", "disabled");
    options.log_sync = enabled("shadps4_log_sync", "enabled");
}

void Notify(const char* text) {
    struct retro_message_ext message {};
    message.msg = text;
    message.duration = 6000;
    message.priority = 3;
    message.level = RETRO_LOG_ERROR;
    message.target = RETRO_MESSAGE_TARGET_ALL;
    message.type = RETRO_MESSAGE_TYPE_NOTIFICATION;
    message.progress = -1;
    unsigned version = 0;
    if (environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &version) && version >= 1) {
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &message);
    } else {
        struct retro_message old = {text, 360};
        environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &old);
    }
    Log(RETRO_LOG_ERROR, "%s", text);
}

// ---- The shared memory and the emulator's process, per platform ----

#ifdef _WIN32

std::wstring ModulePath() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&retro_api_version), &module)) {
        return {};
    }
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

bool CreateShared(std::string& argument) {
    static unsigned counter = 0;
    char name[96];
    std::snprintf(name, sizeof(name), "Local\\shadps4_libretro_%lu_%u",
                  static_cast<unsigned long>(GetCurrentProcessId()), ++counter);
    const ULONGLONG size = Libretro::SHARED_BYTES;
    shared_mapping =
        CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                           static_cast<DWORD>(size >> 32), static_cast<DWORD>(size), name);
    if (!shared_mapping) {
        return false;
    }
    shared = static_cast<SharedState*>(
        MapViewOfFile(shared_mapping, FILE_MAP_ALL_ACCESS, 0, 0, Libretro::SHARED_BYTES));
    argument = name;
    return shared != nullptr;
}

void DestroyShared() {
    if (shared) {
        UnmapViewOfFile(shared);
        shared = nullptr;
    }
    if (shared_mapping) {
        CloseHandle(shared_mapping);
        shared_mapping = nullptr;
    }
}

bool StartEmulator(const std::string& argument) {
    const std::wstring module = ModulePath();
    if (module.empty()) {
        Log(RETRO_LOG_ERROR, "The core's own path could not be found");
        return false;
    }
    std::wstring rundll(MAX_PATH, L'\0');
    rundll.resize(GetSystemDirectoryW(rundll.data(), MAX_PATH));
    rundll += L"\\rundll32.exe";

    std::wstring command = L"\"" + rundll + L"\" \"" + module + L"\",retro_host_main ";
    command.append(argument.begin(), argument.end());

    // Ended with the frontend, however the frontend ends.
    emulator_job = CreateJobObjectW(nullptr, nullptr);
    if (emulator_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(emulator_job, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(rundll.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                        &process)) {
        Log(RETRO_LOG_ERROR, "rundll32 could not be started (error %lu)",
            static_cast<unsigned long>(GetLastError()));
        return false;
    }
    if (emulator_job) {
        AssignProcessToJobObject(emulator_job, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    emulator_process = process.hProcess;
    return true;
}

// Whether the emulator's process has ended, with what, and whether it was killed rather than
// ended by itself. An unhandled exception leaves the status of the exception behind, which has
// one of the two top severity bits set; a status a program exits with is a small number.
bool EmulatorEnded(int& code, bool& crashed) {
    if (!emulator_process || WaitForSingleObject(emulator_process, 0) != WAIT_OBJECT_0) {
        return false;
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(emulator_process, &exit_code);
    crashed = (exit_code >> 30) >= 2;
    code = static_cast<int>(exit_code);
    return true;
}

void StopEmulator() {
    if (emulator_process) {
        // It ends itself on the quit flag; what has not within three seconds is ended.
        if (WaitForSingleObject(emulator_process, 3000) != WAIT_OBJECT_0) {
            TerminateProcess(emulator_process, 1);
            WaitForSingleObject(emulator_process, 2000);
        }
        CloseHandle(emulator_process);
        emulator_process = nullptr;
    }
    if (emulator_job) {
        CloseHandle(emulator_job);
        emulator_job = nullptr;
    }
}

#else

std::string ModulePath() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&retro_api_version), &info) || !info.dli_fname) {
        return {};
    }
    char resolved[PATH_MAX];
    if (realpath(info.dli_fname, resolved)) {
        return resolved;
    }
    return info.dli_fname;
}

bool CreateShared(std::string& argument) {
    // Without MFD_CLOEXEC: the emulator's process inherits the descriptor.
    shared_fd = memfd_create("shadps4_libretro_shared", 0);
    if (shared_fd < 0 || ftruncate(shared_fd, static_cast<off_t>(Libretro::SHARED_BYTES)) != 0) {
        return false;
    }
    void* view = mmap(nullptr, Libretro::SHARED_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED,
                      shared_fd, 0);
    if (view == MAP_FAILED) {
        return false;
    }
    shared = static_cast<SharedState*>(view);
    argument = std::to_string(shared_fd);
    return true;
}

void DestroyShared() {
    if (shared) {
        munmap(shared, Libretro::SHARED_BYTES);
        shared = nullptr;
    }
    if (shared_fd >= 0) {
        close(shared_fd);
        shared_fd = -1;
    }
}

// The launcher as a file in memory, opened only for reading - a file open for writing cannot
// be run.
int LauncherDescriptor() {
    const int fd = memfd_create("shadps4_libretro_host", 0);
    if (fd < 0) {
        return -1;
    }
    unsigned long done = 0;
    while (done < g_libretro_host_size) {
        const ssize_t wrote = write(fd, g_libretro_host + done, g_libretro_host_size - done);
        if (wrote <= 0) {
            close(fd);
            return -1;
        }
        done += static_cast<unsigned long>(wrote);
    }
    fchmod(fd, 0700);
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    const int read_only = open(path, O_RDONLY);
    close(fd);
    return read_only;
}

bool StartEmulator(const std::string& argument) {
    const std::string module = ModulePath();
    if (module.empty()) {
        Log(RETRO_LOG_ERROR, "The core's own path could not be found");
        return false;
    }
    const int launcher = LauncherDescriptor();
    if (launcher < 0) {
        Log(RETRO_LOG_ERROR, "The launcher could not be put in memory: %s", std::strerror(errno));
        return false;
    }
    char launcher_path[64];
    std::snprintf(launcher_path, sizeof(launcher_path), "/proc/self/fd/%d", launcher);

    std::string name = "shadps4_libretro_host";
    std::string module_argument = module;
    std::string shared_argument = argument;
    char* const arguments[] = {name.data(), module_argument.data(), shared_argument.data(),
                               nullptr};
    pid_t pid = -1;
    const int result = posix_spawn(&pid, launcher_path, nullptr, nullptr, arguments, environ);
    close(launcher);
    if (result != 0) {
        Log(RETRO_LOG_ERROR, "The launcher could not be started: %s", std::strerror(result));
        return false;
    }
    emulator_pid = pid;
    return true;
}

// Whether the emulator's process has ended, with what, and whether it was killed rather than
// ended by itself: a process that ends by itself leaves the status it exited with, one that is
// killed leaves the signal that killed it.
bool EmulatorEnded(int& code, bool& crashed) {
    if (emulator_pid <= 0) {
        return false;
    }
    int status = 0;
    if (waitpid(emulator_pid, &status, WNOHANG) != emulator_pid) {
        return false;
    }
    crashed = !WIFEXITED(status);
    code = crashed ? WTERMSIG(status) : WEXITSTATUS(status);
    emulator_pid = -1;
    return true;
}

void StopEmulator() {
    if (emulator_pid <= 0) {
        return;
    }
    // It ends itself on the quit flag; what has not within three seconds is ended.
    int status = 0;
    for (int i = 0; i < 300; ++i) {
        if (waitpid(emulator_pid, &status, WNOHANG) == emulator_pid) {
            emulator_pid = -1;
            return;
        }
        usleep(10 * 1000);
    }
    kill(emulator_pid, SIGKILL);
    waitpid(emulator_pid, &status, 0);
    emulator_pid = -1;
}

#endif

// ---- The frontend's side of the shared memory ----

void SendInput() {
    using Libraries::Pad::OrbisPadButtonDataOffset;

    if (!input_state_cb) {
        return;
    }
    static const struct {
        unsigned id;
        OrbisPadButtonDataOffset button;
    } buttons[] = {
        {RETRO_DEVICE_ID_JOYPAD_B, OrbisPadButtonDataOffset::Cross},
        {RETRO_DEVICE_ID_JOYPAD_A, OrbisPadButtonDataOffset::Circle},
        {RETRO_DEVICE_ID_JOYPAD_Y, OrbisPadButtonDataOffset::Square},
        {RETRO_DEVICE_ID_JOYPAD_X, OrbisPadButtonDataOffset::Triangle},
        {RETRO_DEVICE_ID_JOYPAD_UP, OrbisPadButtonDataOffset::Up},
        {RETRO_DEVICE_ID_JOYPAD_DOWN, OrbisPadButtonDataOffset::Down},
        {RETRO_DEVICE_ID_JOYPAD_LEFT, OrbisPadButtonDataOffset::Left},
        {RETRO_DEVICE_ID_JOYPAD_RIGHT, OrbisPadButtonDataOffset::Right},
        {RETRO_DEVICE_ID_JOYPAD_L, OrbisPadButtonDataOffset::L1},
        {RETRO_DEVICE_ID_JOYPAD_R, OrbisPadButtonDataOffset::R1},
        {RETRO_DEVICE_ID_JOYPAD_L2, OrbisPadButtonDataOffset::L2},
        {RETRO_DEVICE_ID_JOYPAD_R2, OrbisPadButtonDataOffset::R2},
        {RETRO_DEVICE_ID_JOYPAD_L3, OrbisPadButtonDataOffset::L3},
        {RETRO_DEVICE_ID_JOYPAD_R3, OrbisPadButtonDataOffset::R3},
        {RETRO_DEVICE_ID_JOYPAD_START, OrbisPadButtonDataOffset::Options},
        {RETRO_DEVICE_ID_JOYPAD_SELECT, OrbisPadButtonDataOffset::TouchPad},
    };
    for (unsigned pad = 0; pad < shared->options.pads && pad < Libretro::MAX_PADS; ++pad) {
        uint32_t held = 0;
        for (const auto& entry : buttons) {
            if (input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, entry.id)) {
                held |= static_cast<uint32_t>(entry.button);
            }
        }
        shared->buttons[pad].store(held, std::memory_order_release);

        // libretro's -0x8000..0x7fff to the pad's 0..255.
        const auto stick = [pad](unsigned index, unsigned id) -> uint32_t {
            const int raw = input_state_cb(pad, RETRO_DEVICE_ANALOG, index, id);
            return static_cast<uint32_t>(std::clamp((raw + 0x8000) * 255 / 0xffff, 0, 255));
        };
        auto& axes = shared->axes[pad];
        axes[Libretro::SHARED_AXIS_LEFT_X] =
            stick(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        axes[Libretro::SHARED_AXIS_LEFT_Y] =
            stick(RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        axes[Libretro::SHARED_AXIS_RIGHT_X] =
            stick(RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        axes[Libretro::SHARED_AXIS_RIGHT_Y] =
            stick(RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

        // Triggers: analog where the frontend has it, else the digital button.
        const auto trigger = [pad](unsigned id) -> uint32_t {
            int raw =
                input_state_cb(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, id);
            if (raw == 0 && input_state_cb(pad, RETRO_DEVICE_JOYPAD, 0, id)) {
                raw = 0x7fff;
            }
            return static_cast<uint32_t>(std::clamp(raw * 255 / 0x7fff, 0, 255));
        };
        axes[Libretro::SHARED_AXIS_TRIGGER_LEFT] = trigger(RETRO_DEVICE_ID_JOYPAD_L2);
        axes[Libretro::SHARED_AXIS_TRIGGER_RIGHT] = trigger(RETRO_DEVICE_ID_JOYPAD_R2);
    }
}

void ApplyRumble() {
    static uint32_t applied[Libretro::MAX_PADS] = {};
    if (!rumble.set_rumble_state) {
        return;
    }
    for (unsigned pad = 0; pad < Libretro::MAX_PADS; ++pad) {
        const uint32_t wanted = shared->rumble[pad].load(std::memory_order_acquire);
        if (wanted == applied[pad]) {
            continue;
        }
        applied[pad] = wanted;
        const uint32_t small_motor = wanted & 0xff;
        const uint32_t large_motor = (wanted >> 8) & 0xff;
        rumble.set_rumble_state(pad, RETRO_RUMBLE_WEAK,
                                static_cast<uint16_t>(small_motor * 0x101));
        rumble.set_rumble_state(pad, RETRO_RUMBLE_STRONG,
                                static_cast<uint16_t>(large_motor * 0x101));
    }
}

// The newest frame, if there is one newer than the one shown. A frame stays whole until the
// emulator begins the second one after it, so the copy is taken only if nothing was published
// while it was made; else the newer one is tried.
void TakeFrame() {
    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint32_t latest = shared->frame_latest.load();
        if (latest == 0 || latest == shown_latest) {
            return;
        }
        // Held first, then looked at again: once the slot is held and still the newest, the
        // emulator does not write into it, and it is shown from where it is.
        const uint32_t slot = latest & 3;
        if (slot >= Libretro::FRAME_SLOTS) {
            return;
        }
        shared->frame_held.store(slot);
        if (shared->frame_latest.load() != latest) {
            continue;
        }
        const uint32_t width = shared->frame_size[slot].width;
        const uint32_t height = shared->frame_size[slot].height;
        if (width == 0 || height == 0 || width > Libretro::MAX_WIDTH ||
            height > Libretro::MAX_HEIGHT) {
            return;
        }
        shown_frame = Libretro::SharedFrame(shared, slot);
        shown_latest = latest;
        if (width != shown_width || height != shown_height) {
            shown_width = width;
            shown_height = height;
            struct retro_game_geometry geometry {};
            geometry.base_width = width;
            geometry.base_height = height;
            geometry.max_width = std::max(output_width, width);
            geometry.max_height = std::max(output_height, height);
            geometry.aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
            environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
        }
        return;
    }
}

// One frame's worth of sound out of the ring; what is not there is silence.
void TakeAudio(int16_t* out) {
    const uint32_t read = shared->audio_read.load(std::memory_order_relaxed);
    const uint32_t written = shared->audio_written.load(std::memory_order_acquire);
    const uint32_t frames = std::min<uint32_t>(written - read, kAudioFrames);
    for (uint32_t i = 0; i < frames; ++i) {
        const uint32_t at = (read + i) & (Libretro::AUDIO_RING_FRAMES - 1);
        out[i * 2 + 0] = shared->audio[at * 2 + 0];
        out[i * 2 + 1] = shared->audio[at * 2 + 1];
    }
    std::memset(out + frames * 2, 0, (kAudioFrames - frames) * 2 * sizeof(int16_t));
    shared->audio_read.store(read + frames, std::memory_order_release);
}

// The names of the pad's buttons and sticks, for the ports that are controllers: what the
// frontend shows on its remapping screen. They follow SendInput.
void DescribeInput(std::uint32_t pads) {
    static constexpr struct {
        unsigned device;
        unsigned index;
        unsigned id;
        const char* name;
    } controls[] = {
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "Cross"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "Circle"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Square"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Triangle"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L1"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R1"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "L2"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "R2"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "L3"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "R3"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Options"},
        {RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Touchpad Button"},
        {RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,
         "Left Stick X"},
        {RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,
         "Left Stick Y"},
        {RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X,
         "Right Stick X"},
        {RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y,
         "Right Stick Y"},
    };
    // Kept: the frontend may hold on to the pointer.
    static std::vector<retro_input_descriptor> descriptors;
    descriptors.clear();
    for (unsigned pad = 0; pad < pads && pad < Libretro::MAX_PADS; ++pad) {
        for (const auto& control : controls) {
            descriptors.push_back({pad, control.device, control.index, control.id, control.name});
        }
    }
    descriptors.push_back({});
    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors.data());
}

} // namespace

extern "C" {

RETRO_API unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

RETRO_API void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;

    struct retro_log_callback logging {};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) {
        log_cb = logging.log;
    }

    bool categories_supported = false;
    libretro_set_core_options(cb, &categories_supported);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) {
    video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb) {
    input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

RETRO_API void retro_init(void) {}

RETRO_API void retro_deinit(void) {}

RETRO_API void retro_get_system_info(struct retro_system_info* info) {
    std::memset(info, 0, sizeof(*info));
    info->library_name = "shadPS4";
    info->library_version = Common::g_version;
    info->valid_extensions = "bin";
    info->need_fullpath = true;
    info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info* info) {
    std::memset(info, 0, sizeof(*info));
    info->geometry.base_width = output_width;
    info->geometry.base_height = output_height;
    info->geometry.max_width = output_width;
    info->geometry.max_height = output_height;
    info->geometry.aspect_ratio = 16.0f / 9.0f;
    info->timing.fps = kFps;
    info->timing.sample_rate = kSampleRate;
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API void retro_reset(void) {}

RETRO_API bool retro_load_game(const struct retro_game_info* game) {
    if (!game || !game->path || !*game->path) {
        Log(RETRO_LOG_ERROR, "No content: load a game's eboot.bin");
        return false;
    }
    const std::string user_dir = SystemDirectory();
    if (user_dir.empty()) {
        Log(RETRO_LOG_ERROR, "The frontend gave no system directory");
        return false;
    }
    if (std::strlen(game->path) >= Libretro::PATH_BYTES ||
        user_dir.size() >= Libretro::PATH_BYTES) {
        Log(RETRO_LOG_ERROR, "The game's path or the system directory is too long");
        return false;
    }

    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);
    if (!environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble)) {
        rumble = {};
    }

    std::string argument;
    if (!CreateShared(argument)) {
        Log(RETRO_LOG_ERROR, "The memory shared with the emulator could not be made");
        DestroyShared();
        return false;
    }
    // New memory is zeroed: the counters and flags start there.
    shared->magic = Libretro::SHARED_MAGIC;
    shared->version = Libretro::SHARED_VERSION;
    std::strcpy(shared->eboot_path, game->path);
    std::strcpy(shared->user_dir, user_dir.c_str());
    ReadCoreOptions(shared->options);
    DescribeInput(shared->options.pads);
    output_width = shared->options.width;
    output_height = shared->options.height;
    for (auto& axes : shared->axes) {
        for (auto& axis : axes) {
            axis = 128;
        }
        axes[Libretro::SHARED_AXIS_TRIGGER_LEFT] = 0;
        axes[Libretro::SHARED_AXIS_TRIGGER_RIGHT] = 0;
    }

    shared->frame_held.store(Libretro::FRAME_SLOT_NONE);
    shown_frame = nullptr;
    shown_width = 0;
    shown_height = 0;
    shown_latest = 0;
    shutdown_requested = false;

    Log(RETRO_LOG_INFO, "shadPS4 %s (%s), user directory %s", Common::g_version,
        Common::g_scm_rev, user_dir.c_str());
    Log(RETRO_LOG_INFO, "Starting %s", game->path);
    if (!StartEmulator(argument)) {
        DestroyShared();
        return false;
    }
    emulator_running = true;
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t) {
    return false;
}

RETRO_API void retro_unload_game(void) {
    if (shared) {
        shared->quit.store(1, std::memory_order_release);
    }
    StopEmulator();
    // The frame shown was in the shared memory.
    shown_frame = nullptr;
    DestroyShared();
    emulator_running = false;
}

RETRO_API void retro_run(void) {
    static int16_t sound[kAudioFrames * 2];

    if (input_poll_cb) {
        input_poll_cb();
    }

    if (shared) {
        // The emulator ended on its own: the game quit, or it crashed. Either way there is
        // nothing left to show, and the frontend is asked to close the content.
        int code = 0;
        bool crashed = false;
        if (emulator_running && EmulatorEnded(code, crashed)) {
            emulator_running = false;
            char text[160];
            if (crashed) {
                std::snprintf(text, sizeof(text),
                              "shadPS4: the emulator stopped unexpectedly (%d); see shadps4.log",
                              code);
                Notify(text);
            } else if (code != 0) {
                // The game ended itself, with an error of its own: not the emulator's crash.
                std::snprintf(text, sizeof(text),
                              "shadPS4: the game ended with an error (%d); see shadps4.log", code);
                Notify(text);
            } else {
                Log(RETRO_LOG_INFO, "The emulator ended");
            }
        }
        if (!emulator_running && !shutdown_requested) {
            shutdown_requested = true;
            environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
        }

        SendInput();
        ApplyRumble();
        TakeFrame();
        TakeAudio(sound);
    } else {
        std::memset(sound, 0, sizeof(sound));
    }

    if (video_cb) {
        if (!shown_frame) {
            // Nothing drawn yet: black, at the size declared.
            blank_frame.assign(static_cast<std::size_t>(output_width) * output_height, 0);
            shown_frame = blank_frame.data();
            shown_width = output_width;
            shown_height = output_height;
        }
        video_cb(shown_frame, shown_width, shown_height, shown_width * sizeof(uint32_t));
    }
    if (audio_batch_cb) {
        audio_batch_cb(sound, kAudioFrames);
    }
}

RETRO_API unsigned retro_get_region(void) {
    return RETRO_REGION_NTSC;
}

RETRO_API size_t retro_serialize_size(void) {
    return 0;
}

RETRO_API bool retro_serialize(void*, size_t) {
    return false;
}

RETRO_API bool retro_unserialize(const void*, size_t) {
    return false;
}

RETRO_API void retro_cheat_reset(void) {}

RETRO_API void retro_cheat_set(unsigned, bool, const char*) {}

RETRO_API void* retro_get_memory_data(unsigned) {
    return nullptr;
}

RETRO_API size_t retro_get_memory_size(unsigned) {
    return 0;
}

} // extern "C"
