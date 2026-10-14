// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The handle to an image's memory that the core is sent, in the emulator's process (see
// gpu_frames.h). A file of its own, as on Windows it takes Vulkan's Win32 declarations and so
// windows.h, which the renderer's files do without.

#include <cstdint>

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "libretro_bridge.h"

namespace Libretro {

bool ExportImageMemory(void* device, std::uint64_t memory, void* get_device_proc_addr,
                       std::uint64_t& handle) {
    const auto get_device_function = reinterpret_cast<PFN_vkGetDeviceProcAddr>(get_device_proc_addr);
    const VkDevice vk_device = static_cast<VkDevice>(device);
    const VkDeviceMemory vk_memory = reinterpret_cast<VkDeviceMemory>(memory);
    if (!get_device_function) {
        return false;
    }
#if defined(_WIN32)
    const auto get_handle = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
        get_device_function(vk_device, "vkGetMemoryWin32HandleKHR"));
    VkMemoryGetWin32HandleInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    info.memory = vk_memory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    HANDLE exported = nullptr;
    if (!get_handle || get_handle(vk_device, &info, &exported) != VK_SUCCESS || !exported) {
        return false;
    }
    handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(exported));
    return true;
#elif defined(__linux__)
    const auto get_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
        get_device_function(vk_device, "vkGetMemoryFdKHR"));
    VkMemoryGetFdInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    info.memory = vk_memory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    if (!get_fd || get_fd(vk_device, &info, &fd) != VK_SUCCESS || fd < 0) {
        return false;
    }
    handle = static_cast<std::uint64_t>(fd);
    return true;
#else
    (void)vk_device;
    (void)vk_memory;
    (void)handle;
    return false;
#endif
}

void CloseImageMemory(std::uint64_t handle) {
#if defined(_WIN32)
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle)));
#elif defined(__linux__)
    close(static_cast<int>(handle));
#else
    (void)handle;
#endif
}

} // namespace Libretro
