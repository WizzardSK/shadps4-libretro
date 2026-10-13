// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// The memory the libretro core (libretro_core.cpp, in the frontend's process) shares with the
// emulator it runs in a process of its own (host.cpp). One block: this header, then two frames.
namespace Libretro {

inline constexpr std::uint32_t SHARED_MAGIC = 0x34505368; // "hSP4"
inline constexpr std::uint32_t SHARED_VERSION = 4;

inline constexpr std::uint32_t MAX_WIDTH = 3840;
inline constexpr std::uint32_t MAX_HEIGHT = 2160;
inline constexpr std::size_t FRAME_BYTES = static_cast<std::size_t>(MAX_WIDTH) * MAX_HEIGHT * 4;

/// Frames of 16-bit stereo the audio ring holds. A power of two.
inline constexpr std::uint32_t AUDIO_RING_FRAMES = 16384;
/// Frames the emulator keeps in the ring: two of the frontend's frames' worth at 60 per
/// second, about 33 ms. The frontend takes one at a time, so less would run dry; more would
/// only be latency.
inline constexpr std::uint32_t AUDIO_RING_TARGET_FRAMES = 1600;

inline constexpr std::size_t PATH_BYTES = 4096;

/// The frontend's ports that can be pads of the console.
inline constexpr std::uint32_t MAX_PADS = 4;

/// A pad's axes, 0 to 255. Its buttons are OrbisPadButtonDataOffset bits.
enum SharedAxis : std::uint32_t {
    SHARED_AXIS_LEFT_X,
    SHARED_AXIS_LEFT_Y,
    SHARED_AXIS_RIGHT_X,
    SHARED_AXIS_RIGHT_Y,
    SHARED_AXIS_TRIGGER_LEFT,
    SHARED_AXIS_TRIGGER_RIGHT,
    SHARED_AXIS_COUNT,
};

/// The core options as they were when the game was loaded.
struct SharedOptions {
    std::uint32_t neo_mode;
    std::uint32_t circle_enter;
    std::uint32_t network;
    std::uint32_t show_splash;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t fsr;
    std::uint32_t rcas;
    std::uint32_t vblank_frequency;
    std::uint32_t readbacks;
    std::uint32_t direct_memory_access;
    std::uint32_t show_fps;
    std::int32_t volume;
    /// How many of the frontend's ports are connected as pads, 1 to MAX_PADS.
    std::uint32_t pads;
    std::int32_t console_language;
    std::uint32_t dev_kit;
    std::int32_t extra_dmem_mbytes;
    /// In thousandths, as the emulator keeps it.
    std::int32_t rcas_attenuation;
    std::uint32_t readback_linear_images;
    std::uint32_t shader_cache;
    std::uint32_t copy_gpu_buffers;
    std::uint32_t trophy_popup;
    /// 0 left, 1 right, 2 top, 3 bottom.
    std::uint32_t trophy_side;
    std::uint32_t trophy_seconds;
    std::uint32_t log_enable;
    std::uint32_t log_separate;
    std::uint32_t log_sync;
};

struct SharedState {
    std::uint32_t magic;
    std::uint32_t version;

    // Written by the core before the emulator is started.
    char eboot_path[PATH_BYTES]; // UTF-8
    char user_dir[PATH_BYTES];   // UTF-8
    SharedOptions options;

    /// Set by the core when content is closed: the emulator ends itself.
    std::atomic<std::uint32_t> quit;

    // Core to emulator, per pad.
    std::atomic<std::uint32_t> buttons[MAX_PADS];
    std::atomic<std::uint32_t> axes[MAX_PADS][SHARED_AXIS_COUNT];

    // Emulator to core.
    /// A pad's motors, the small one in the low byte and the large one above it.
    std::atomic<std::uint32_t> rumble[MAX_PADS];

    /// Frames published so far. Frame n is in slot n & 1 and stays whole until frame n + 2 is
    /// begun: the core copies it and takes the copy only if nothing was published meanwhile.
    std::atomic<std::uint32_t> frame_count;
    struct {
        std::uint32_t width;
        std::uint32_t height;
    } frame_size[2];

    /// A ring of interleaved 16-bit stereo, one writer (the emulator) and one reader (the core).
    /// The indices count frames and only grow; a position is index & (AUDIO_RING_FRAMES - 1).
    std::atomic<std::uint32_t> audio_written;
    std::atomic<std::uint32_t> audio_read;
    std::int16_t audio[AUDIO_RING_FRAMES * 2];
};

inline constexpr std::size_t SHARED_FRAMES_OFFSET = (sizeof(SharedState) + 4095) & ~std::size_t{4095};
inline constexpr std::size_t SHARED_BYTES = SHARED_FRAMES_OFFSET + 2 * FRAME_BYTES;

inline std::uint8_t* SharedFrame(SharedState* state, std::uint32_t slot) {
    return reinterpret_cast<std::uint8_t*>(state) + SHARED_FRAMES_OFFSET + slot * FRAME_BYTES;
}

} // namespace Libretro
