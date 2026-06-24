#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include "spv_pattern_vert.h"
#include "spv_pattern_frag.h"
#include "spv_gradient_vert.h"
#include "spv_gradient_frag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vk_context_t g_vk_ctx = {0};

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *user_data)
{
    (void)type; (void)user_data;
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}

static VkResult create_debug_messenger(VkInstance inst, VkDebugUtilsMessengerEXT *out)
{
    VkDebugUtilsMessengerCreateInfoEXT ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debug_callback;
    PFN_vkCreateDebugUtilsMessengerEXT fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
    if (!fn) return VK_ERROR_EXTENSION_NOT_PRESENT;
    return fn(inst, &ci, NULL, out);
}

static void destroy_debug_messenger(VkInstance inst, VkDebugUtilsMessengerEXT messenger)
{
    if (!messenger) return;
    PFN_vkDestroyDebugUtilsMessengerEXT fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT");
    if (fn) fn(inst, messenger, NULL);
}

int32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return (int32_t)i;
    }
    return -1;
}

vg_lite_error_t vg_lite_vulkan_init(void)
{
    VkResult res;
    memset(&g_vk_ctx, 0, sizeof(g_vk_ctx));

    const char *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };
    int enable_validation = 0;
    uint32_t layer_count = 0;
    VK_CHECK(vkEnumerateInstanceLayerProperties(&layer_count, NULL));
    if (layer_count > 0) {
        VkLayerProperties *layers = malloc(layer_count * sizeof(VkLayerProperties));
        VK_CHECK(vkEnumerateInstanceLayerProperties(&layer_count, layers));
        for (uint32_t i = 0; i < layer_count; i++) {
            if (strcmp(layers[i].layerName, validation_layers[0]) == 0) { enable_validation = 1; break; }
        }
        free(layers);
    }

    const char *wanted_extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    int ext_count = 0;
    const char *enabled_extensions[3];

    {
        uint32_t avail_count = 0;
        VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &avail_count, NULL));
        VkExtensionProperties *avail_exts = malloc(avail_count * sizeof(VkExtensionProperties));
        VK_CHECK(vkEnumerateInstanceExtensionProperties(NULL, &avail_count, avail_exts));
        for (int w = 0; w < 3; w++) {
            for (uint32_t a = 0; a < avail_count; a++) {
                if (strcmp(wanted_extensions[w], avail_exts[a].extensionName) == 0) {
                    enabled_extensions[ext_count++] = wanted_extensions[w];
                    break;
                }
            }
        }
        free(avail_exts);
    }

    VkApplicationInfo app_info = {0};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "vglite_vulkan";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "vg_lite";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo inst_ci = {0};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.pApplicationInfo = &app_info;
    inst_ci.enabledExtensionCount = ext_count;
    inst_ci.ppEnabledExtensionNames = enabled_extensions;

    VkDebugUtilsMessengerCreateInfoEXT debug_ci = {0};
    if (enable_validation) {
        inst_ci.enabledLayerCount = 1;
        inst_ci.ppEnabledLayerNames = validation_layers;
        debug_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_ci.pfnUserCallback = debug_callback;
        inst_ci.pNext = &debug_ci;
    }

    res = vkCreateInstance(&inst_ci, NULL, &g_vk_ctx.instance);
    if (res != VK_SUCCESS) { fprintf(stderr, "vkCreateInstance failed: %d\n", res); return VG_LITE_NO_CONTEXT; }
    if (enable_validation) create_debug_messenger(g_vk_ctx.instance, &g_vk_ctx.debug_messenger);

    uint32_t gpu_count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(g_vk_ctx.instance, &gpu_count, NULL));
    if (gpu_count == 0) return VG_LITE_NO_CONTEXT;
    VkPhysicalDevice *gpus = malloc(gpu_count * sizeof(VkPhysicalDevice));
    VK_CHECK(vkEnumeratePhysicalDevices(g_vk_ctx.instance, &gpu_count, gpus));
    g_vk_ctx.physical_device = gpus[0];
    free(gpus);

    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk_ctx.physical_device, &qf_count, NULL);
    VkQueueFamilyProperties *qf_props = malloc(qf_count * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk_ctx.physical_device, &qf_count, qf_props);
    g_vk_ctx.queue_family_index = 0;
    for (uint32_t i = 0; i < qf_count; i++) {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_vk_ctx.queue_family_index = i; break; }
    }
    free(qf_props);

    float qpriority = 1.0f;
    VkDeviceQueueCreateInfo q_ci = {0};
    q_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    q_ci.queueFamilyIndex = g_vk_ctx.queue_family_index;
    q_ci.queueCount = 1;
    q_ci.pQueuePriorities = &qpriority;

    VkDeviceCreateInfo dev_ci = {0};
    dev_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_ci.queueCreateInfoCount = 1;
    dev_ci.pQueueCreateInfos = &q_ci;
    dev_ci.enabledExtensionCount = 0;
    dev_ci.ppEnabledExtensionNames = NULL;

    VkPhysicalDeviceFeatures enabled_features = {0};
    enabled_features.multiViewport = VK_TRUE;
    dev_ci.pEnabledFeatures = &enabled_features;

    if (enable_validation) { dev_ci.enabledLayerCount = 1; dev_ci.ppEnabledLayerNames = validation_layers; }

    res = vkCreateDevice(g_vk_ctx.physical_device, &dev_ci, NULL, &g_vk_ctx.device);
    if (res != VK_SUCCESS) return VG_LITE_NO_CONTEXT;
    vkGetDeviceQueue(g_vk_ctx.device, g_vk_ctx.queue_family_index, 0, &g_vk_ctx.queue);

    VkCommandPoolCreateInfo pool_ci = {0};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = g_vk_ctx.queue_family_index;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(g_vk_ctx.device, &pool_ci, NULL, &g_vk_ctx.command_pool));

    VkCommandBufferAllocateInfo cmd_ai = {0};
    cmd_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ai.commandPool = g_vk_ctx.command_pool;
    cmd_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(g_vk_ctx.device, &cmd_ai, &g_vk_ctx.cmd_buf));

    VkFenceCreateInfo fence_ci = {0};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(g_vk_ctx.device, &fence_ci, NULL, &g_vk_ctx.fence));

    VkDescriptorPoolSize ds_pool_sizes[] = { 
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 }
    };
    VkDescriptorPoolCreateInfo ds_pool_ci = {0};
    ds_pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ds_pool_ci.maxSets = 64;
    ds_pool_ci.poolSizeCount = 2;
    ds_pool_ci.pPoolSizes = ds_pool_sizes;
    ds_pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    VK_CHECK(vkCreateDescriptorPool(g_vk_ctx.device, &ds_pool_ci, NULL, &g_vk_ctx.descriptor_pool));

    VkBufferCreateInfo ssbo_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ssbo_ci.size = 80;
    ssbo_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    ssbo_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &ssbo_ci, NULL, &g_vk_ctx.blit_ssbo_buffer));

    VkMemoryRequirements ssbo_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_vk_ctx.blit_ssbo_buffer, &ssbo_req);
    VkMemoryAllocateInfo ssbo_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ssbo_alloc.allocationSize = ssbo_req.size;
    int32_t ssbo_mem_type = find_memory_type(ssbo_req.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ssbo_mem_type < 0) {
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.blit_ssbo_buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }
    ssbo_alloc.memoryTypeIndex = (uint32_t)ssbo_mem_type;
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &ssbo_alloc, NULL, &g_vk_ctx.blit_ssbo_memory));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, g_vk_ctx.blit_ssbo_buffer, g_vk_ctx.blit_ssbo_memory, 0));
    VK_CHECK(vkMapMemory(g_vk_ctx.device, g_vk_ctx.blit_ssbo_memory, 0, VK_WHOLE_SIZE, 0, &g_vk_ctx.blit_ssbo_mapped));

    VkBufferCreateInfo clut_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    clut_ci.size = 256 * 4;
    clut_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    clut_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &clut_ci, NULL, &g_vk_ctx.clut_buffer));

    VkMemoryRequirements clut_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_vk_ctx.clut_buffer, &clut_req);
    VkMemoryAllocateInfo clut_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    clut_alloc.allocationSize = clut_req.size;
    int32_t clut_mem_type = find_memory_type(clut_req.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (clut_mem_type < 0) {
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.clut_buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }
    clut_alloc.memoryTypeIndex = (uint32_t)clut_mem_type;
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &clut_alloc, NULL, &g_vk_ctx.clut_memory));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, g_vk_ctx.clut_buffer, g_vk_ctx.clut_memory, 0));
    VK_CHECK(vkMapMemory(g_vk_ctx.device, g_vk_ctx.clut_memory, 0, VK_WHOLE_SIZE, 0, &g_vk_ctx.clut_mapped));

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_destroy(void)
{
    if (!g_vk_ctx.device) return VG_LITE_SUCCESS;
    VK_CHECK(vkDeviceWaitIdle(g_vk_ctx.device));
    vg_lite_vulkan_destroy_pipelines();
    if (g_vk_ctx.clut_mapped) { vkUnmapMemory(g_vk_ctx.device, g_vk_ctx.clut_memory); g_vk_ctx.clut_mapped = NULL; }
    if (g_vk_ctx.clut_buffer) { vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.clut_buffer, NULL); g_vk_ctx.clut_buffer = VK_NULL_HANDLE; }
    if (g_vk_ctx.clut_memory) { vkFreeMemory(g_vk_ctx.device, g_vk_ctx.clut_memory, NULL); g_vk_ctx.clut_memory = VK_NULL_HANDLE; }
    if (g_vk_ctx.descriptor_pool) { vkDestroyDescriptorPool(g_vk_ctx.device, g_vk_ctx.descriptor_pool, NULL); g_vk_ctx.descriptor_pool = VK_NULL_HANDLE; }
    if (g_vk_ctx.fence) { vkDestroyFence(g_vk_ctx.device, g_vk_ctx.fence, NULL); g_vk_ctx.fence = VK_NULL_HANDLE; }
    if (g_vk_ctx.command_pool) { vkFreeCommandBuffers(g_vk_ctx.device, g_vk_ctx.command_pool, 1, &g_vk_ctx.cmd_buf); vkDestroyCommandPool(g_vk_ctx.device, g_vk_ctx.command_pool, NULL); g_vk_ctx.command_pool = VK_NULL_HANDLE; }
    if (g_vk_ctx.device) { vkDestroyDevice(g_vk_ctx.device, NULL); g_vk_ctx.device = VK_NULL_HANDLE; }
    if (g_vk_ctx.debug_messenger) { destroy_debug_messenger(g_vk_ctx.instance, g_vk_ctx.debug_messenger); g_vk_ctx.debug_messenger = VK_NULL_HANDLE; }
    if (g_vk_ctx.instance) { vkDestroyInstance(g_vk_ctx.instance, NULL); g_vk_ctx.instance = VK_NULL_HANDLE; }
    memset(&g_vk_ctx, 0, sizeof(g_vk_ctx));
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_begin_command(void)
{
    if (g_vk_ctx.cmd_buf_recording) return VG_LITE_SUCCESS;
    if (g_vk_ctx.cmd_buf_submitted) { VK_CHECK(vkResetCommandBuffer(g_vk_ctx.cmd_buf, 0)); g_vk_ctx.cmd_buf_submitted = 0; }
    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(g_vk_ctx.cmd_buf, &bi));
    g_vk_ctx.cmd_buf_recording = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_submit_command(int wait)
{
    if (!g_vk_ctx.cmd_buf_recording) return VG_LITE_SUCCESS;
    VK_CHECK(vkEndCommandBuffer(g_vk_ctx.cmd_buf));
    g_vk_ctx.cmd_buf_recording = 0;
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_vk_ctx.cmd_buf;
    VK_CHECK(vkResetFences(g_vk_ctx.device, 1, &g_vk_ctx.fence));
    VK_CHECK(vkQueueSubmit(g_vk_ctx.queue, 1, &si, g_vk_ctx.fence));
    g_vk_ctx.cmd_buf_submitted = 1;
    if (wait) VK_CHECK(vkWaitForFences(g_vk_ctx.device, 1, &g_vk_ctx.fence, VK_TRUE, UINT64_MAX));
    for (int i = 0; i < g_vk_ctx.pending_fb_count; i++) {
        vkDestroyFramebuffer(g_vk_ctx.device, g_vk_ctx.pending_fb[i], NULL);
    }
    g_vk_ctx.pending_fb_count = 0;
    for (int i = 0; i < g_vk_ctx.pending_rp_count; i++) {
        vkDestroyRenderPass(g_vk_ctx.device, g_vk_ctx.pending_rp[i], NULL);
    }
    g_vk_ctx.pending_rp_count = 0;
    return VG_LITE_SUCCESS;
}

VkRenderPass vg_lite_vulkan_create_render_pass(VkFormat format)
{
    VkAttachmentDescription attachments[3] = {0};
    /* MSAA color attachment */
    attachments[0].format = format;
    attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    /* Resolve attachment (final output) */
    attachments[1].format = format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    /* MSAA depth/stencil attachment */
    attachments[2].format = VK_FORMAT_D24_UNORM_S8_UINT;
    attachments[2].samples = VK_SAMPLE_COUNT_4_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference resolve_ref = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference stencil_ref = {2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pResolveAttachments = &resolve_ref;
    sub.pDepthStencilAttachment = &stencil_ref;
    
    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 3;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 0;
    ci.pDependencies = NULL;
    
    VkRenderPass rp;
    if (vkCreateRenderPass(g_vk_ctx.device, &ci, NULL, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    return rp;
}

static int create_attachment(
    VkImage *out_image, VkDeviceMemory *out_memory, VkImageView *out_view,
    uint32_t width, uint32_t height,
    VkFormat format, VkSampleCountFlagBits samples,
    VkImageUsageFlags usage, VkImageAspectFlags aspect,
    VkAccessFlags dst_access, VkPipelineStageFlags dst_stage,
    VkImageLayout new_layout)
{
    VkImageCreateInfo img_ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = format;
    img_ci.extent.width = width;
    img_ci.extent.height = height;
    img_ci.extent.depth = 1;
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = samples;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = usage;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_vk_ctx.device, &img_ci, NULL, out_image) != VK_SUCCESS)
        return -1;

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(g_vk_ctx.device, *out_image, &mem_req);
    VkMemoryAllocateInfo alloc_ci = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_ci.allocationSize = mem_req.size;
    int mem_type = find_memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type < 0) mem_type = 0;
    alloc_ci.memoryTypeIndex = (uint32_t)mem_type;
    if (vkAllocateMemory(g_vk_ctx.device, &alloc_ci, NULL, out_memory) != VK_SUCCESS)
        return -1;
    VK_CHECK(vkBindImageMemory(g_vk_ctx.device, *out_image, *out_memory, 0));

    VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_ci.image = *out_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange.aspectMask = aspect;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, out_view) != VK_SUCCESS)
        return -1;

    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = *out_image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst_stage,
        0, 0, NULL, 0, NULL, 1, &barrier);
    return 0;
}

vg_lite_error_t vg_lite_vulkan_set_render_target(vg_lite_buffer_t *target)
{
    if (!target->handle) return VG_LITE_INVALID_ARGUMENT;
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;

    if (g_vk_ctx.current_fb_image == internal->image) return VG_LITE_SUCCESS;
    
    if (g_vk_ctx.current_fb) { vg_lite_vulkan_end_render_pass(); }
    
    if (internal->render_pass == VK_NULL_HANDLE) {
        VkFormat vkfmt = vg_lite_format_to_vk(target->format);
        internal->render_pass = vg_lite_vulkan_create_render_pass(vkfmt);
    }
    
    if (internal->msaa_color_image == VK_NULL_HANDLE) {
        VkFormat vkfmt = vg_lite_format_to_vk(target->format);
        if (create_attachment(&internal->msaa_color_image, &internal->msaa_color_memory, &internal->msaa_color_view,
                target->width, target->height, vkfmt, VK_SAMPLE_COUNT_4_BIT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) < 0)
            return VG_LITE_OUT_OF_MEMORY;
    }
    
    if (internal->msaa_depth_image == VK_NULL_HANDLE) {
        if (create_attachment(&internal->msaa_depth_image, &internal->msaa_depth_memory, &internal->msaa_depth_view,
                target->width, target->height, VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_4_BIT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) < 0)
            return VG_LITE_OUT_OF_MEMORY;
    }
    
    if (internal->depth_stencil_image == VK_NULL_HANDLE) {
        if (create_attachment(&internal->depth_stencil_image, &internal->depth_stencil_memory, &internal->depth_stencil_view,
                target->width, target->height, VK_FORMAT_D24_UNORM_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) < 0)
            return VG_LITE_OUT_OF_MEMORY;
    }
    
    VkImageView fb_views[3] = {internal->msaa_color_view, internal->view, internal->msaa_depth_view};
    VkFramebufferCreateInfo fb_ci = {0};
    fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass = internal->render_pass;
    fb_ci.attachmentCount = 3;
    fb_ci.pAttachments = fb_views;
    fb_ci.width = target->width;
    fb_ci.height = target->height;
    fb_ci.layers = 1;
    VkFramebuffer fb;
    if (vkCreateFramebuffer(g_vk_ctx.device, &fb_ci, NULL, &fb) != VK_SUCCESS) return VG_LITE_OUT_OF_MEMORY;
    
    g_vk_ctx.current_fb = fb;
    g_vk_ctx.current_fb_image = internal->image;
    g_vk_ctx.current_fb_view = internal->view;
    g_vk_ctx.current_fb_width = target->width;
    g_vk_ctx.current_fb_height = target->height;
    
    VkClearValue clear_values[3] = {0};
    clear_values[0].color.float32[0] = 0.0f;
    clear_values[0].color.float32[1] = 0.0f;
    clear_values[0].color.float32[2] = 0.0f;
    clear_values[0].color.float32[3] = 0.0f;
    clear_values[2].depthStencil.depth = 0.0f;
    clear_values[2].depthStencil.stencil = 0;
    
    VkRenderPassBeginInfo rpbi = {0};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = internal->render_pass;
    rpbi.framebuffer = fb;
    rpbi.renderArea.offset.x = 0;
    rpbi.renderArea.offset.y = 0;
    rpbi.renderArea.extent.width = target->width;
    rpbi.renderArea.extent.height = target->height;
    rpbi.clearValueCount = 3;
    rpbi.pClearValues = clear_values;
    
    vkCmdBeginRenderPass(g_vk_ctx.cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_end_render_pass(void)
{
    if (g_vk_ctx.current_fb) {
        vkCmdEndRenderPass(g_vk_ctx.cmd_buf);
        
        VkImageMemoryBarrier host_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        host_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        host_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        host_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host_barrier.image = g_vk_ctx.current_fb_image;
        host_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        host_barrier.subresourceRange.levelCount = 1;
        host_barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1, &host_barrier);
        
        if (g_vk_ctx.pending_fb_count < MAX_PENDING_FB) {
            g_vk_ctx.pending_fb[g_vk_ctx.pending_fb_count++] = g_vk_ctx.current_fb;
        } else {
            vkDestroyFramebuffer(g_vk_ctx.device, g_vk_ctx.current_fb, NULL);
        }
        g_vk_ctx.current_fb = VK_NULL_HANDLE;
        g_vk_ctx.current_fb_image = VK_NULL_HANDLE;
    }
    return VG_LITE_SUCCESS;
}

static VkShaderModule create_shader_module(const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size * sizeof(uint32_t);
    ci.pCode = code;
    VkShaderModule mod;
    if (vkCreateShaderModule(g_vk_ctx.device, &ci, NULL, &mod) != VK_SUCCESS) return VK_NULL_HANDLE;
    return mod;
}

int vg_lite_blend_to_group(vg_lite_blend_t blend)
{
    switch (blend) {
    case VG_LITE_BLEND_NONE:
        return BG_NONE;
    case VG_LITE_BLEND_SRC_OVER:
    case VG_LITE_BLEND_NORMAL_LVGL:
        return BG_SRC_OVER;
    case VG_LITE_BLEND_DST_OVER:
        return BG_DST_OVER;
    case VG_LITE_BLEND_ADDITIVE:
        return BG_ADDITIVE;
    case VG_LITE_BLEND_SUBTRACT:
        return BG_SUBTRACT;
    default:
        return BG_SHADER;
    }
}

static void get_blend_attachment_state(int blend_group, VkPipelineColorBlendAttachmentState *cba)
{
    memset(cba, 0, sizeof(*cba));
    cba->colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    switch (blend_group) {
    case BG_NONE:
        break;
    case BG_SRC_OVER:
        cba->blendEnable = VK_TRUE;
        cba->colorBlendOp = VK_BLEND_OP_ADD;
        cba->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BG_DST_OVER:
        cba->blendEnable = VK_TRUE;
        cba->colorBlendOp = VK_BLEND_OP_ADD;
        cba->srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        cba->dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        cba->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case BG_ADDITIVE:
        cba->blendEnable = VK_TRUE;
        cba->colorBlendOp = VK_BLEND_OP_ADD;
        cba->srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba->dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        cba->srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    case BG_SUBTRACT:
        cba->blendEnable = VK_TRUE;
        cba->colorBlendOp = VK_BLEND_OP_ADD;
        cba->srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba->dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba->srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        cba->dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    default:
        break;
    }
}

static VkRenderPass create_render_pass_no_msaa(VkFormat format);

static VkPipeline create_blit_pipeline_internal(VkFormat format, int blend_group, int no_msaa)
{
    if (no_msaa) {
        extern const uint32_t g_vert_spv_data[];
        extern const uint32_t g_native_frag_spv_data[];
        extern const uint32_t g_vert_spv_size;
        extern const uint32_t g_native_frag_spv_size;
        if (!g_vk_ctx.vert_shader)
            g_vk_ctx.vert_shader = create_shader_module(g_vert_spv_data, (size_t)g_vert_spv_size);
        if (!g_vk_ctx.native_frag_shader)
            g_vk_ctx.native_frag_shader = create_shader_module(g_native_frag_spv_data, (size_t)g_native_frag_spv_size);
        if (!g_vk_ctx.native_pipeline_layout) {
            VkDescriptorSetLayoutBinding bindings[2] = {
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
            };
            VkDescriptorSetLayoutCreateInfo ds_ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            ds_ci.bindingCount = 2;
            ds_ci.pBindings = bindings;
            VK_CHECK(vkCreateDescriptorSetLayout(g_vk_ctx.device, &ds_ci, NULL, &g_vk_ctx.native_descriptor_layout));
            VkPipelineLayoutCreateInfo pl_ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl_ci.setLayoutCount = 1;
            pl_ci.pSetLayouts = &g_vk_ctx.native_descriptor_layout;
            VK_CHECK(vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL, &g_vk_ctx.native_pipeline_layout));
        }
    } else {
        if (!g_vk_ctx.vert_shader) {
            extern const uint32_t g_vert_spv_data[];
            extern const uint32_t g_frag_spv_data[];
            extern const uint32_t g_vert_spv_size;
            extern const uint32_t g_frag_spv_size;
            g_vk_ctx.vert_shader = create_shader_module(g_vert_spv_data, (size_t)g_vert_spv_size);
            g_vk_ctx.frag_shader = create_shader_module(g_frag_spv_data, (size_t)g_frag_spv_size);
        }
        if (!g_vk_ctx.blit_pipeline_layout) {
            VkDescriptorSetLayoutBinding bindings[4] = {
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
            };
            VkDescriptorSetLayoutCreateInfo ds_ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            ds_ci.bindingCount = 4;
            ds_ci.pBindings = bindings;
            VK_CHECK(vkCreateDescriptorSetLayout(g_vk_ctx.device, &ds_ci, NULL, &g_vk_ctx.blit_descriptor_layout));
            VkPipelineLayoutCreateInfo pl_ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            pl_ci.setLayoutCount = 1;
            pl_ci.pSetLayouts = &g_vk_ctx.blit_descriptor_layout;
            VK_CHECK(vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL, &g_vk_ctx.blit_pipeline_layout));
        }
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_vk_ctx.vert_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         no_msaa ? g_vk_ctx.native_frag_shader : g_vk_ctx.frag_shader, "main", NULL},
    };
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = no_msaa ? VK_SAMPLE_COUNT_1_BIT : VK_SAMPLE_COUNT_4_BIT;
    if (!no_msaa) ms.minSampleShading = 1.0f;

    VkPipelineColorBlendAttachmentState cba;
    get_blend_attachment_state(blend_group, &cba);
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkRenderPass rp = no_msaa ? create_render_pass_no_msaa(format) : vg_lite_vulkan_create_render_pass(format);

    VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn_ci = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2; dyn_ci.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp_ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_ci.stageCount = 2; gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi; gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vs;
    gp_ci.pRasterizationState = &rs; gp_ci.pMultisampleState = &ms;
    gp_ci.pColorBlendState = &cb; gp_ci.pDepthStencilState = &ds; gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = no_msaa ? g_vk_ctx.native_pipeline_layout : g_vk_ctx.blit_pipeline_layout;
    gp_ci.renderPass = rp; gp_ci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &pipeline));
    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
    return pipeline;
}

static VkPipeline create_blit_pipeline(VkFormat format, int blend_group)
{
    return create_blit_pipeline_internal(format, blend_group, 0);
}

static VkPipeline create_blit_pipeline_no_msaa(VkFormat format, int blend_group)
{
    return create_blit_pipeline_internal(format, blend_group, 1);
}

VkPipeline vg_lite_vulkan_get_pipeline(VkFormat format, int blend_group)
{
    for (int i = 0; i < g_vk_ctx.pipeline_cache_count; i++) {
        if (g_vk_ctx.pipeline_cache[i].format == format &&
            g_vk_ctx.pipeline_cache[i].blend_group == blend_group)
            return g_vk_ctx.pipeline_cache[i].pipeline;
    }

    VkPipeline pipeline = create_blit_pipeline(format, blend_group);
    if (pipeline && g_vk_ctx.pipeline_cache_count < MAX_PIPELINE_CACHE) {
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].pipeline = pipeline;
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].format = format;
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].blend_group = blend_group;
        g_vk_ctx.pipeline_cache_count++;
    }
    return pipeline;
}

static VkRenderPass create_render_pass_no_msaa(VkFormat format)
{
    VkAttachmentDescription attachment = {0};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &attachment;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;

    VkRenderPass rp;
    if (vkCreateRenderPass(g_vk_ctx.device, &ci, NULL, &rp) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return rp;
}

VkPipeline vg_lite_vulkan_get_pipeline_no_msaa(VkFormat format, int blend_group)
{
    for (int i = 0; i < g_vk_ctx.pipeline_cache_count; i++) {
        if (g_vk_ctx.pipeline_cache[i].format == format &&
            g_vk_ctx.pipeline_cache[i].blend_group == blend_group &&
            g_vk_ctx.pipeline_cache[i].no_msaa)
            return g_vk_ctx.pipeline_cache[i].pipeline;
    }

    VkPipeline pipeline = create_blit_pipeline_no_msaa(format, blend_group);
    if (pipeline && g_vk_ctx.pipeline_cache_count < MAX_PIPELINE_CACHE) {
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].pipeline = pipeline;
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].format = format;
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].blend_group = blend_group;
        g_vk_ctx.pipeline_cache[g_vk_ctx.pipeline_cache_count].no_msaa = 1;
        g_vk_ctx.pipeline_cache_count++;
    }
    return pipeline;
}

vg_lite_error_t vg_lite_vulkan_set_render_target_no_msaa(vg_lite_buffer_t *target)
{
    if (!target->handle) return VG_LITE_INVALID_ARGUMENT;
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;

    if (g_vk_ctx.current_fb) { vg_lite_vulkan_end_render_pass(); }

    VkFormat vkfmt = vg_lite_format_to_vk(target->format);
    VkRenderPass rp = create_render_pass_no_msaa(vkfmt);
    if (!rp) return VG_LITE_OUT_OF_MEMORY;

    VkImageView fb_view = internal->view;
    VkFramebufferCreateInfo fb_ci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_ci.renderPass = rp;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments = &fb_view;
    fb_ci.width = target->width;
    fb_ci.height = target->height;
    fb_ci.layers = 1;
    VkFramebuffer fb;
    if (vkCreateFramebuffer(g_vk_ctx.device, &fb_ci, NULL, &fb) != VK_SUCCESS) {
        vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }

    g_vk_ctx.current_fb = fb;
    g_vk_ctx.current_fb_image = internal->image;
    g_vk_ctx.current_fb_view = internal->view;
    g_vk_ctx.current_fb_width = target->width;
    g_vk_ctx.current_fb_height = target->height;

    {
        VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = internal->image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, NULL, 0, NULL, 1, &barrier);
    }

    VkRenderPassBeginInfo rpbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = rp;
    rpbi.framebuffer = fb;
    rpbi.renderArea.extent.width = target->width;
    rpbi.renderArea.extent.height = target->height;
    rpbi.clearValueCount = 0;

    vkCmdBeginRenderPass(g_vk_ctx.cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    if (g_vk_ctx.pending_rp_count < MAX_PENDING_FB)
        g_vk_ctx.pending_rp[g_vk_ctx.pending_rp_count++] = rp;
    else
        vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
    return VG_LITE_SUCCESS;
}

void vg_lite_vulkan_destroy_pipelines(void)
{
    for (int i = 0; i < g_vk_ctx.pipeline_cache_count; i++) {
        if (g_vk_ctx.pipeline_cache[i].pipeline)
            vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.pipeline_cache[i].pipeline, NULL);
    }
    g_vk_ctx.pipeline_cache_count = 0;
    if (g_vk_ctx.blit_pipeline_layout) { vkDestroyPipelineLayout(g_vk_ctx.device, g_vk_ctx.blit_pipeline_layout, NULL); g_vk_ctx.blit_pipeline_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.blit_descriptor_layout) { vkDestroyDescriptorSetLayout(g_vk_ctx.device, g_vk_ctx.blit_descriptor_layout, NULL); g_vk_ctx.blit_descriptor_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.vert_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.vert_shader, NULL); g_vk_ctx.vert_shader = VK_NULL_HANDLE; }
    if (g_vk_ctx.frag_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.frag_shader, NULL); g_vk_ctx.frag_shader = VK_NULL_HANDLE; }
    if (g_vk_ctx.native_frag_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.native_frag_shader, NULL); g_vk_ctx.native_frag_shader = VK_NULL_HANDLE; }
    if (g_vk_ctx.native_pipeline_layout) { vkDestroyPipelineLayout(g_vk_ctx.device, g_vk_ctx.native_pipeline_layout, NULL); g_vk_ctx.native_pipeline_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.native_descriptor_layout) { vkDestroyDescriptorSetLayout(g_vk_ctx.device, g_vk_ctx.native_descriptor_layout, NULL); g_vk_ctx.native_descriptor_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.blit_ssbo_buffer) {
        vkUnmapMemory(g_vk_ctx.device, g_vk_ctx.blit_ssbo_memory);
        vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.blit_ssbo_buffer, NULL);
        vkFreeMemory(g_vk_ctx.device, g_vk_ctx.blit_ssbo_memory, NULL);
        g_vk_ctx.blit_ssbo_buffer = VK_NULL_HANDLE;
        g_vk_ctx.blit_ssbo_memory = VK_NULL_HANDLE;
        g_vk_ctx.blit_ssbo_mapped = NULL;
    }

    for (int i = 0; i < g_vk_ctx.pattern_pipeline_cache_count; i++) {
        if (g_vk_ctx.pattern_pipeline_cache[i].pipeline)
            vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.pattern_pipeline_cache[i].pipeline, NULL);
    }
    g_vk_ctx.pattern_pipeline_cache_count = 0;
    if (g_vk_ctx.pattern_pipeline_layout) { vkDestroyPipelineLayout(g_vk_ctx.device, g_vk_ctx.pattern_pipeline_layout, NULL); g_vk_ctx.pattern_pipeline_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.pattern_descriptor_layout) { vkDestroyDescriptorSetLayout(g_vk_ctx.device, g_vk_ctx.pattern_descriptor_layout, NULL); g_vk_ctx.pattern_descriptor_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.pattern_vert_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.pattern_vert_shader, NULL); g_vk_ctx.pattern_vert_shader = VK_NULL_HANDLE; }
    if (g_vk_ctx.pattern_frag_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.pattern_frag_shader, NULL); g_vk_ctx.pattern_frag_shader = VK_NULL_HANDLE; }

    for (int i = 0; i < g_vk_ctx.grad_pipeline_cache_count; i++) {
        if (g_vk_ctx.grad_pipeline_cache[i].pipeline)
            vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.grad_pipeline_cache[i].pipeline, NULL);
    }
    g_vk_ctx.grad_pipeline_cache_count = 0;
    if (g_vk_ctx.grad_stencil_pipeline) { vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.grad_stencil_pipeline, NULL); g_vk_ctx.grad_stencil_pipeline = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_cover_pipeline) { vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.grad_cover_pipeline, NULL); g_vk_ctx.grad_cover_pipeline = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_cover_vbo) { vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.grad_cover_vbo, NULL); g_vk_ctx.grad_cover_vbo = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_cover_vbo_mem) { vkFreeMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_vbo_mem, NULL); g_vk_ctx.grad_cover_vbo_mem = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_cover_ibo) { vkDestroyBuffer(g_vk_ctx.device, g_vk_ctx.grad_cover_ibo, NULL); g_vk_ctx.grad_cover_ibo = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_cover_ibo_mem) { vkFreeMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_ibo_mem, NULL); g_vk_ctx.grad_cover_ibo_mem = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_pipeline_layout) { vkDestroyPipelineLayout(g_vk_ctx.device, g_vk_ctx.grad_pipeline_layout, NULL); g_vk_ctx.grad_pipeline_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_descriptor_layout) { vkDestroyDescriptorSetLayout(g_vk_ctx.device, g_vk_ctx.grad_descriptor_layout, NULL); g_vk_ctx.grad_descriptor_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_vert_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.grad_vert_shader, NULL); g_vk_ctx.grad_vert_shader = VK_NULL_HANDLE; }
    if (g_vk_ctx.grad_frag_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.grad_frag_shader, NULL); g_vk_ctx.grad_frag_shader = VK_NULL_HANDLE; }
}

static VkPipeline create_pattern_pipeline(VkFormat format, int blend_group)
{
    if (!g_vk_ctx.pattern_vert_shader) {
        g_vk_ctx.pattern_vert_shader = create_shader_module(g_pattern_vert_spv_data, (size_t)g_pattern_vert_spv_size);
        g_vk_ctx.pattern_frag_shader = create_shader_module(g_pattern_frag_spv_data, (size_t)g_pattern_frag_spv_size);
    }

    if (!g_vk_ctx.pattern_pipeline_layout) {
        VkPushConstantRange pc_range = {0};
        pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pc_range.offset = 0;
        pc_range.size = 124;

        VkDescriptorSetLayoutBinding binding = {0};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        binding.pImmutableSamplers = NULL;

        VkDescriptorSetLayoutCreateInfo ds_ci = {0};
        ds_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ds_ci.bindingCount = 1;
        ds_ci.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(g_vk_ctx.device, &ds_ci, NULL, &g_vk_ctx.pattern_descriptor_layout));

        VkPipelineLayoutCreateInfo pl_ci = {0};
        pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl_ci.setLayoutCount = 1;
        pl_ci.pSetLayouts = &g_vk_ctx.pattern_descriptor_layout;
        pl_ci.pushConstantRangeCount = 1;
        pl_ci.pPushConstantRanges = &pc_range;
        VK_CHECK(vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL, &g_vk_ctx.pattern_pipeline_layout));
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_vk_ctx.pattern_vert_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_vk_ctx.pattern_frag_shader, "main", NULL},
    };

    VkVertexInputBindingDescription binding = {0};
    binding.binding = 0;
    binding.stride = 8;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attr = {0};
    attr.binding = 0;
    attr.location = 0;
    attr.format = VK_FORMAT_R32G32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
    ms.minSampleShading = 1.0f;

    VkPipelineColorBlendAttachmentState cba;
    get_blend_attachment_state(blend_group, &cba);
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkRenderPass rp = vg_lite_vulkan_create_render_pass(format);

    VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn_ci = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp_ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_ci.stageCount = 2;
    gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi;
    gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vs;
    gp_ci.pRasterizationState = &rs;
    gp_ci.pMultisampleState = &ms;
    gp_ci.pColorBlendState = &cb;
    gp_ci.pDepthStencilState = &ds;
    gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = g_vk_ctx.pattern_pipeline_layout;
    gp_ci.renderPass = rp;
    gp_ci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &pipeline));
    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
    return pipeline;
}

VkPipeline vg_lite_vulkan_get_pattern_pipeline(VkFormat format, int blend_group)
{
    for (int i = 0; i < g_vk_ctx.pattern_pipeline_cache_count; i++) {
        if (g_vk_ctx.pattern_pipeline_cache[i].format == format &&
            g_vk_ctx.pattern_pipeline_cache[i].blend_group == blend_group)
            return g_vk_ctx.pattern_pipeline_cache[i].pipeline;
    }

    VkPipeline pipeline = create_pattern_pipeline(format, blend_group);
    if (pipeline && g_vk_ctx.pattern_pipeline_cache_count < MAX_PIPELINE_CACHE) {
        g_vk_ctx.pattern_pipeline_cache[g_vk_ctx.pattern_pipeline_cache_count].pipeline = pipeline;
        g_vk_ctx.pattern_pipeline_cache[g_vk_ctx.pattern_pipeline_cache_count].format = format;
        g_vk_ctx.pattern_pipeline_cache[g_vk_ctx.pattern_pipeline_cache_count].blend_group = blend_group;
        g_vk_ctx.pattern_pipeline_cache_count++;
    }
    return pipeline;
}

void vg_lite_vulkan_init_grad_pipeline(VkFormat format)
{
    if (g_vk_ctx.grad_stencil_pipeline) return;

    g_vk_ctx.grad_vert_shader = create_shader_module(g_gradient_vert_spv_data, (size_t)g_gradient_vert_spv_size);
    g_vk_ctx.grad_frag_shader = create_shader_module(g_gradient_frag_spv_data, (size_t)g_gradient_frag_spv_size);

    /* Push constant: mat3 path_matrix(48B) + mat3 grad_matrix(48B) +
     * 4 ints(16B) + spread_mode(4B) + padding(12B) = 128B */
    VkPushConstantRange pc_range = {0};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = 128;

    /* Binding 0: gradient texture (combined image sampler) */
    VkDescriptorSetLayoutBinding ds_binding = {0};
    ds_binding.binding = 0;
    ds_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ds_binding.descriptorCount = 1;
    ds_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    ds_binding.pImmutableSamplers = NULL;

    VkDescriptorSetLayoutCreateInfo ds_ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ds_ci.bindingCount = 1;
    ds_ci.pBindings = &ds_binding;
    VK_CHECK(vkCreateDescriptorSetLayout(g_vk_ctx.device, &ds_ci, NULL, &g_vk_ctx.grad_descriptor_layout));

    VkPipelineLayoutCreateInfo pl_ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &g_vk_ctx.grad_descriptor_layout;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &pc_range;
    VK_CHECK(vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL, &g_vk_ctx.grad_pipeline_layout));

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_vk_ctx.grad_vert_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_vk_ctx.grad_frag_shader, "main", NULL},
    };

    VkVertexInputBindingDescription binding = {0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

    VkPipelineColorBlendAttachmentState stencil_cba = {0};
    stencil_cba.colorWriteMask = 0;
    VkPipelineColorBlendStateCreateInfo stencil_cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    stencil_cb.attachmentCount = 1;
    stencil_cb.pAttachments = &stencil_cba;

    VkPipelineDepthStencilStateCreateInfo stencil_ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    stencil_ds.stencilTestEnable = VK_TRUE;
    stencil_ds.front.failOp = VK_STENCIL_OP_KEEP;
    stencil_ds.front.passOp = VK_STENCIL_OP_INVERT;
    stencil_ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
    stencil_ds.front.compareOp = VK_COMPARE_OP_ALWAYS;
    stencil_ds.front.compareMask = 0x01;
    stencil_ds.front.writeMask = 0x01;
    stencil_ds.front.reference = 0;
    stencil_ds.back = stencil_ds.front;

    VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_ci = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates = dyn_states;

    VkRenderPass rp = vg_lite_vulkan_create_render_pass(format);

    VkGraphicsPipelineCreateInfo gp_ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_ci.stageCount = 2;
    gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi;
    gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vs;
    gp_ci.pRasterizationState = &rs;
    gp_ci.pMultisampleState = &ms;
    gp_ci.pColorBlendState = &stencil_cb;
    gp_ci.pDepthStencilState = &stencil_ds;
    gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = g_vk_ctx.grad_pipeline_layout;
    gp_ci.renderPass = rp;
    gp_ci.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_vk_ctx.grad_stencil_pipeline));

    VkPipelineDepthStencilStateCreateInfo cover_ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    cover_ds.stencilTestEnable = VK_TRUE;
    cover_ds.front.failOp = VK_STENCIL_OP_KEEP;
    cover_ds.front.passOp = VK_STENCIL_OP_ZERO;
    cover_ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
    cover_ds.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    cover_ds.front.compareMask = 0x01;
    cover_ds.front.writeMask = 0x01;
    cover_ds.front.reference = 0;
    cover_ds.back = cover_ds.front;

    VkPipelineInputAssemblyStateCreateInfo cover_ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    cover_ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineColorBlendAttachmentState cover_cba = {0};
    cover_cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cover_cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cover_cb.attachmentCount = 1;
    cover_cb.pAttachments = &cover_cba;

    gp_ci.pInputAssemblyState = &cover_ia;
    gp_ci.pColorBlendState = &cover_cb;
    gp_ci.pDepthStencilState = &cover_ds;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_vk_ctx.grad_cover_pipeline));

    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);

    VkBufferCreateInfo vbo_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vbo_ci.size = 256;
    vbo_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &vbo_ci, NULL, &g_vk_ctx.grad_cover_vbo));
    VkMemoryRequirements vbo_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_vk_ctx.grad_cover_vbo, &vbo_req);
    VkMemoryAllocateInfo vbo_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    vbo_alloc.allocationSize = vbo_req.size;
    vbo_alloc.memoryTypeIndex = (uint32_t)find_memory_type(vbo_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &vbo_alloc, NULL, &g_vk_ctx.grad_cover_vbo_mem));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_vbo, g_vk_ctx.grad_cover_vbo_mem, 0));

    void *vbo_ptr;
    VK_CHECK(vkMapMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_vbo_mem, 0, 256, 0, &vbo_ptr));
    float quad[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
    memcpy(vbo_ptr, quad, sizeof(quad));
    vkUnmapMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_vbo_mem);

    VkBufferCreateInfo ibo_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ibo_ci.size = 6 * sizeof(uint32_t);
    ibo_ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &ibo_ci, NULL, &g_vk_ctx.grad_cover_ibo));
    VkMemoryRequirements ibo_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_vk_ctx.grad_cover_ibo, &ibo_req);
    VkMemoryAllocateInfo ibo_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ibo_alloc.allocationSize = ibo_req.size;
    ibo_alloc.memoryTypeIndex = (uint32_t)find_memory_type(ibo_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &ibo_alloc, NULL, &g_vk_ctx.grad_cover_ibo_mem));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_ibo, g_vk_ctx.grad_cover_ibo_mem, 0));

    void *ibo_ptr;
    VK_CHECK(vkMapMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_ibo_mem, 0, 6 * sizeof(uint32_t), 0, &ibo_ptr));
    uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    memcpy(ibo_ptr, indices, sizeof(indices));
    vkUnmapMemory(g_vk_ctx.device, g_vk_ctx.grad_cover_ibo_mem);
}

VkPipeline vg_lite_vulkan_get_grad_cover_pipeline(VkFormat format, int blend_group)
{
    for (int i = 0; i < g_vk_ctx.grad_pipeline_cache_count; i++) {
        if (g_vk_ctx.grad_pipeline_cache[i].format == format &&
            g_vk_ctx.grad_pipeline_cache[i].blend_group == blend_group)
            return g_vk_ctx.grad_pipeline_cache[i].pipeline;
    }
    if (!g_vk_ctx.grad_vert_shader) return VK_NULL_HANDLE;

    VkPipelineColorBlendAttachmentState cba;
    get_blend_attachment_state(blend_group, &cba);

    VkPipelineDepthStencilStateCreateInfo cover_ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    cover_ds.stencilTestEnable = VK_TRUE;
    cover_ds.front.failOp = VK_STENCIL_OP_KEEP;
    cover_ds.front.passOp = VK_STENCIL_OP_ZERO;
    cover_ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
    cover_ds.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    cover_ds.front.compareMask = 0x01;
    cover_ds.front.writeMask = 0x01;
    cover_ds.front.reference = 0;
    cover_ds.back = cover_ds.front;

    VkPipelineInputAssemblyStateCreateInfo cover_ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    cover_ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkVertexInputBindingDescription binding = {0, 8, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;

    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

    VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_ci = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates = dyn_states;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_vk_ctx.grad_vert_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_vk_ctx.grad_frag_shader, "main", NULL},
    };

    VkRenderPass rp = vg_lite_vulkan_create_render_pass(format);

    VkGraphicsPipelineCreateInfo gp_ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_ci.stageCount = 2;
    gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi;
    gp_ci.pInputAssemblyState = &cover_ia;
    gp_ci.pViewportState = &vs;
    gp_ci.pRasterizationState = &rs;
    gp_ci.pMultisampleState = &ms;
    gp_ci.pColorBlendState = &cb;
    gp_ci.pDepthStencilState = &cover_ds;
    gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = g_vk_ctx.grad_pipeline_layout;
    gp_ci.renderPass = rp;
    gp_ci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &pipeline));
    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);

    if (pipeline && g_vk_ctx.grad_pipeline_cache_count < MAX_PIPELINE_CACHE) {
        g_vk_ctx.grad_pipeline_cache[g_vk_ctx.grad_pipeline_cache_count].pipeline = pipeline;
        g_vk_ctx.grad_pipeline_cache[g_vk_ctx.grad_pipeline_cache_count].format = format;
        g_vk_ctx.grad_pipeline_cache[g_vk_ctx.grad_pipeline_cache_count].blend_group = blend_group;
        g_vk_ctx.grad_pipeline_cache_count++;
    }
    return pipeline;
}

/* ---- Scissor API ---- */

void vg_lite_vulkan_apply_scissor(uint32_t fb_width, uint32_t fb_height)
{
    if (g_vk_ctx.scissor_enabled && g_vk_ctx.scissor_count > 0) {
        vkCmdSetScissor(g_vk_ctx.cmd_buf, 0, g_vk_ctx.scissor_count, g_vk_ctx.scissor_rects);
    } else {
        VkRect2D full = {{0, 0}, {fb_width, fb_height}};
        vkCmdSetScissor(g_vk_ctx.cmd_buf, 0, 1, &full);
    }
}

vg_lite_error_t vg_lite_set_scissor(vg_lite_int32_t x, vg_lite_int32_t y,
                                    vg_lite_int32_t right, vg_lite_int32_t bottom)
{
    if (right <= x || bottom <= y) return VG_LITE_INVALID_ARGUMENT;
    g_vk_ctx.scissor_rects[0].offset.x = x;
    g_vk_ctx.scissor_rects[0].offset.y = y;
    g_vk_ctx.scissor_rects[0].extent.width = (uint32_t)(right - x);
    g_vk_ctx.scissor_rects[0].extent.height = (uint32_t)(bottom - y);
    g_vk_ctx.scissor_count = 1;
    g_vk_ctx.scissor_enabled = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_scissor_rects(vg_lite_buffer_t *target, vg_lite_uint32_t nums,
                                      vg_lite_rectangle_t rect[])
{
    (void)target;
    if (nums == 0 || nums > MAX_SCISSOR_RECTS || !rect) return VG_LITE_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < nums; i++) {
        g_vk_ctx.scissor_rects[i].offset.x = rect[i].x;
        g_vk_ctx.scissor_rects[i].offset.y = rect[i].y;
        g_vk_ctx.scissor_rects[i].extent.width = rect[i].width;
        g_vk_ctx.scissor_rects[i].extent.height = rect[i].height;
    }
    g_vk_ctx.scissor_count = (int)nums;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_enable_scissor(void)
{
    g_vk_ctx.scissor_enabled = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_disable_scissor(void)
{
    g_vk_ctx.scissor_enabled = 0;
    return VG_LITE_SUCCESS;
}
