// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

#include "libretro.h"

namespace Libretro {
struct SharedState;
}

// The picture handed to the frontend on the GPU, in the frontend's process (gpu_frames.cpp):
// the emulator's finished images are imported into the frontend's Vulkan device and shown from
// there, in place of being read back into system memory and uploaded again. Linux, and a
// frontend whose video driver is Vulkan.
namespace Libretro::GpuFrames {

/// Asks the frontend for a Vulkan context, in retro_load_game. False when it gives none: the
/// picture then goes through system memory, as it does without this.
bool Request(retro_environment_t environment, retro_log_printf_t log);

/// The memory shared with the emulator, in which it is told which GPU the frontend is on.
/// nullptr once that memory is gone.
void SetShared(SharedState* shared);

/// Takes what the emulator has sent over the socket: the descriptors of its images.
void Receive(int socket);

/// Shows the image in a slot, or the last one again when there is none to show yet.
void Show(retro_video_refresh_t video, std::uint32_t slot, bool have_frame, unsigned width,
          unsigned height);

/// Why the picture cannot be handed over on the GPU, once that is known; nullptr otherwise.
const char* Failure();

/// Content closed: everything imported is let go.
void Close();

} // namespace Libretro::GpuFrames
