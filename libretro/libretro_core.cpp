// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The libretro core, first stage: the emulator runs inside the frontend's
// process, started by retro_load_game on a thread of its own as main() starts
// it, and still opens its own window and its own audio and input devices. The
// frontend gets a black frame and silence. What it proves is that the
// emulator lives in a host process it did not start: the guest's fixed
// address ranges, its thread-local storage and its signal handlers.
//
// Not there yet: the picture, the sound and the pads through the frontend,
// core options, and closing content. The emulator has no way to stop a
// running game short of ending the process - upstream's own exit is
// std::quick_exit - so closing content ends the frontend too, for now.

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include "common/key_manager.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "common/scm_rev.h"
#include "common/singleton.h"
#include "core/emulator_settings.h"
#include "core/ipc/ipc.h"
#include "core/user_settings.h"
#include "emulator.h"
#include "libretro.h"

namespace {

retro_environment_t environ_cb = nullptr;
retro_video_refresh_t video_cb = nullptr;
retro_audio_sample_batch_t audio_batch_cb = nullptr;
retro_input_poll_t input_poll_cb = nullptr;
retro_log_printf_t log_cb = nullptr;

std::thread emulator_thread;
std::atomic<bool> emulator_started{false};

constexpr unsigned kWidth = 1280;
constexpr unsigned kHeight = 720;
constexpr double kFps = 60.0;
constexpr double kSampleRate = 48000.0;

void Log(enum retro_log_level level, const char* fmt, ...) {
    char text[1024];
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

std::filesystem::path SystemDirectory() {
    const char* dir = nullptr;
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir && *dir) {
        return std::filesystem::path(dir) / "shadPS4";
    }
    return {};
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

RETRO_API void retro_set_input_state(retro_input_state_t) {}

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
    info->geometry.base_width = kWidth;
    info->geometry.base_height = kHeight;
    info->geometry.max_width = kWidth;
    info->geometry.max_height = kHeight;
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
    if (emulator_started.exchange(true)) {
        Log(RETRO_LOG_ERROR, "A game was already started in this process; restart the frontend");
        return false;
    }

    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format);

    const std::filesystem::path eboot{
        std::u8string_view(reinterpret_cast<const char8_t*>(game->path))};
    const std::filesystem::path user_dir = SystemDirectory();
    if (user_dir.empty()) {
        Log(RETRO_LOG_ERROR, "The frontend gave no system directory");
        emulator_started = false;
        return false;
    }
    Common::FS::SetLibretroUserDir(user_dir);

    Log(RETRO_LOG_INFO, "shadPS4 %s (%s), user directory %s", Common::g_version,
        Common::g_scm_rev, user_dir.string().c_str());
    Log(RETRO_LOG_INFO, "Starting %s", eboot.string().c_str());

    // What main() does between its command line and Emulator::Run. Run does
    // not return: it ends in the window's event loop and std::quick_exit.
    emulator_thread = std::thread([eboot] {
        Common::Log::Setup("shadps4.log");
        IPC::Instance().Init();
        KeyManager::GetInstance()->LoadFromFile();
        EmulatorSettings.Load();
        UserSettings.Load();

        auto* emulator = Common::Singleton<Core::Emulator>::Instance();
        emulator->executableName = "shadps4_libretro";
        emulator->Run(eboot);
    });
    emulator_thread.detach();
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t) {
    return false;
}

RETRO_API void retro_unload_game(void) {
    if (emulator_started) {
        // See the note at the top: there is no stopping a running game yet.
        Log(RETRO_LOG_WARN, "Closing content ends the process: the game cannot be stopped yet");
        std::quick_exit(0);
    }
}

RETRO_API void retro_run(void) {
    static uint32_t black[kWidth * kHeight];
    static int16_t silence[2 * 800];

    if (input_poll_cb) {
        input_poll_cb();
    }
    if (video_cb) {
        video_cb(black, kWidth, kHeight, kWidth * sizeof(uint32_t));
    }
    if (audio_batch_cb) {
        audio_batch_cb(silence, 800);
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
