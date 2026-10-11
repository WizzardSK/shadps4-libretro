// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

// What the emulator calls in the libretro core's side of its own process (host.cpp), which
// passes it on to the core in the frontend's process.
namespace Libretro {

/// A finished frame, read back from the GPU: 32 bits a pixel, bytes B, G, R, X, rows top to
/// bottom, no padding between them. Called from the renderer's thread.
void PushFrame(const void* pixels, std::uint32_t width, std::uint32_t height);

/// The rate the frontend is given the sound at.
inline constexpr int AUDIO_SAMPLE_RATE = 48000;

/// An audio port's SDL_AudioStream, converting to 16-bit stereo at AUDIO_SAMPLE_RATE and bound to
/// no device: the core reads it. Removed before it is destroyed.
void AddAudioStream(void* sdl_audio_stream);
void RemoveAudioStream(void* sdl_audio_stream);

/// Sets the emulator's settings from the frontend's core options, as they were when the game was
/// loaded. Called by the emulator once it has loaded its own configuration.
void ApplyCoreOptions();

/// A pad's motors, 0 to 255 each, as the game set them. The pad is the controller's index.
void SetRumble(std::uint32_t pad, std::uint8_t small_motor, std::uint8_t large_motor);

} // namespace Libretro
