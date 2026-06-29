#ifndef VULKAN_SHADER_LOADER_H
#define VULKAN_SHADER_LOADER_H

#include "volk.h"

/*
 * Load a SPIR-V shader file from disk.
 * 
 * Looks for the file in these directories (in order):
 *   1. SPV_SEARCH_PATH environment variable (if set)
 *   2. "spv/" relative to working directory
 *   3. Directory of the executable (build/tests/Debug, etc.)
 *   4. "../spv/" relative to executable
 *
 * The .spv extension is appended automatically if not present.
 *
 * On success: returns VK_NULL_HANDLE-less true, *out_data is malloc'd 
 *             (caller must free), *out_size is byte count.
 * On failure: returns VK_NULL_HANDLE, out_data/out_size untouched.
 */
VkShaderModule load_shader_module(VkDevice device, const char *name);

#endif
