#include "shader_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <linux/limits.h>
#endif

static void get_exe_dir(char *buf, size_t bufsize)
{
#ifdef _WIN32
    GetModuleFileNameA(NULL, buf, (DWORD)bufsize);
    char *last = strrchr(buf, '\\');
    if (last) *last = '\0';
#else
    ssize_t len = readlink("/proc/self/exe", buf, bufsize - 1);
    if (len > 0) {
        buf[len] = '\0';
        char *last = strrchr(buf, '/');
        if (last) *last = '\0';
    }
#endif
}

static int try_load_file(const char *path, uint32_t **out_data, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || (sz % 4) != 0) {
        fclose(f);
        return 0;
    }

    uint32_t *data = (uint32_t *)malloc(sz);
    if (!data) {
        fclose(f);
        return 0;
    }

    if (fread(data, 1, sz, f) != (size_t)sz) {
        free(data);
        fclose(f);
        return 0;
    }

    fclose(f);
    *out_data = data;
    *out_size = (size_t)sz;
    return 1;
}

VkShaderModule load_shader_module(VkDevice device, const char *name)
{
    char path[1024];

    /* Ensure .spv extension */
    char full_name[512];
    if (strstr(name, ".spv"))
        snprintf(full_name, sizeof(full_name), "%s", name);
    else
        snprintf(full_name, sizeof(full_name), "%s.spv", name);

    uint32_t *spv_data = NULL;
    size_t spv_size = 0;

    /* 1. SPV_SEARCH_PATH env var */
    const char *env = getenv("SPV_SEARCH_PATH");
    if (env) {
        snprintf(path, sizeof(path), "%s/%s", env, full_name);
        if (try_load_file(path, &spv_data, &spv_size)) goto found;
    }

    /* 2. "spv/" relative to CWD */
    snprintf(path, sizeof(path), "spv/%s", full_name);
    if (try_load_file(path, &spv_data, &spv_size)) goto found;

    /* 3. Exe directory */
    {
        char exe_dir[1024] = {0};
        get_exe_dir(exe_dir, sizeof(exe_dir));
        snprintf(path, sizeof(path), "%s/%s", exe_dir, full_name);
        if (try_load_file(path, &spv_data, &spv_size)) goto found;

        /* 4. ../spv/ relative to exe */
        snprintf(path, sizeof(path), "%s/../spv/%s", exe_dir, full_name);
        if (try_load_file(path, &spv_data, &spv_size)) goto found;

        /* 5. ../../spv/ relative to exe (build/tests/Debug -> build/spv) */
        snprintf(path, sizeof(path), "%s/../../spv/%s", exe_dir, full_name);
        if (try_load_file(path, &spv_data, &spv_size)) goto found;

        /* 6. build dir relative to exe (build/spv) */
        snprintf(path, sizeof(path), "%s/spv/%s", exe_dir, full_name);
        if (try_load_file(path, &spv_data, &spv_size)) goto found;
    }

    /* Not found */
    fprintf(stderr, "[shader_loader] ERROR: Cannot find shader '%s'\n", full_name);
    fprintf(stderr, "[shader_loader] Set SPV_SEARCH_PATH env var or place .spv files in spv/ directory\n");
    return VK_NULL_HANDLE;

found:
    {
        VkShaderModuleCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spv_size;
        ci.pCode = spv_data;

        VkShaderModule mod;
        VkResult result = vkCreateShaderModule(device, &ci, NULL, &mod);
        free(spv_data);

        if (result != VK_SUCCESS) {
            fprintf(stderr, "[shader_loader] vkCreateShaderModule failed for '%s' (0x%x)\n", full_name, result);
            return VK_NULL_HANDLE;
        }
        return mod;
    }
}
