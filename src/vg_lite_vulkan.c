#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
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

vg_lite_error_t vg_lite_vulkan_init(void)
{
    VkResult res;
    memset(&g_vk_ctx, 0, sizeof(g_vk_ctx));

    const char *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };
    int enable_validation = 0;
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    if (layer_count > 0) {
        VkLayerProperties *layers = malloc(layer_count * sizeof(VkLayerProperties));
        vkEnumerateInstanceLayerProperties(&layer_count, layers);
        for (uint32_t i = 0; i < layer_count; i++) {
            if (strcmp(layers[i].layerName, validation_layers[0]) == 0) { enable_validation = 1; break; }
        }
        free(layers);
    }

    const char *extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
    VkInstanceCreateInfo inst_ci = {0};
    inst_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    inst_ci.enabledExtensionCount = 3;
    inst_ci.ppEnabledExtensionNames = extensions;

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
    vkEnumeratePhysicalDevices(g_vk_ctx.instance, &gpu_count, NULL);
    if (gpu_count == 0) return VG_LITE_NO_CONTEXT;
    VkPhysicalDevice *gpus = malloc(gpu_count * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(g_vk_ctx.instance, &gpu_count, gpus);
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
    if (enable_validation) { dev_ci.enabledLayerCount = 1; dev_ci.ppEnabledLayerNames = validation_layers; }

    res = vkCreateDevice(g_vk_ctx.physical_device, &dev_ci, NULL, &g_vk_ctx.device);
    if (res != VK_SUCCESS) return VG_LITE_NO_CONTEXT;
    vkGetDeviceQueue(g_vk_ctx.device, g_vk_ctx.queue_family_index, 0, &g_vk_ctx.queue);

    VkCommandPoolCreateInfo pool_ci = {0};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = g_vk_ctx.queue_family_index;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(g_vk_ctx.device, &pool_ci, NULL, &g_vk_ctx.command_pool);

    VkCommandBufferAllocateInfo cmd_ai = {0};
    cmd_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ai.commandPool = g_vk_ctx.command_pool;
    cmd_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(g_vk_ctx.device, &cmd_ai, &g_vk_ctx.cmd_buf);

    VkFenceCreateInfo fence_ci = {0};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(g_vk_ctx.device, &fence_ci, NULL, &g_vk_ctx.fence);

    VkDescriptorPoolSize ds_pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 } };
    VkDescriptorPoolCreateInfo ds_pool_ci = {0};
    ds_pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ds_pool_ci.maxSets = 64;
    ds_pool_ci.poolSizeCount = 1;
    ds_pool_ci.pPoolSizes = ds_pool_sizes;
    ds_pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    vkCreateDescriptorPool(g_vk_ctx.device, &ds_pool_ci, NULL, &g_vk_ctx.descriptor_pool);

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_destroy(void)
{
    if (!g_vk_ctx.device) return VG_LITE_SUCCESS;
    vkDeviceWaitIdle(g_vk_ctx.device);
    vg_lite_vulkan_destroy_pipelines();
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
    if (g_vk_ctx.cmd_buf_submitted) { vkResetCommandBuffer(g_vk_ctx.cmd_buf, 0); g_vk_ctx.cmd_buf_submitted = 0; }
    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(g_vk_ctx.cmd_buf, &bi);
    g_vk_ctx.cmd_buf_recording = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_submit_command(int wait)
{
    if (!g_vk_ctx.cmd_buf_recording) return VG_LITE_SUCCESS;
    vkEndCommandBuffer(g_vk_ctx.cmd_buf);
    g_vk_ctx.cmd_buf_recording = 0;
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g_vk_ctx.cmd_buf;
    vkResetFences(g_vk_ctx.device, 1, &g_vk_ctx.fence);
    vkQueueSubmit(g_vk_ctx.queue, 1, &si, g_vk_ctx.fence);
    g_vk_ctx.cmd_buf_submitted = 1;
    if (wait) vkWaitForFences(g_vk_ctx.device, 1, &g_vk_ctx.fence, VK_TRUE, UINT64_MAX);
    for (int i = 0; i < g_vk_ctx.pending_fb_count; i++) {
        vkDestroyFramebuffer(g_vk_ctx.device, g_vk_ctx.pending_fb[i], NULL);
    }
    g_vk_ctx.pending_fb_count = 0;
    return VG_LITE_SUCCESS;
}

VkRenderPass vg_lite_vulkan_create_render_pass(VkFormat format)
{
    VkAttachmentDescription attachments[2] = {0};
    /* Color attachment */
    attachments[0].format = format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    /* Depth/stencil attachment */
    attachments[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference stencil_ref = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pDepthStencilAttachment = &stencil_ref;
    
    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 2;
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 0;
    ci.pDependencies = NULL;
    
    VkRenderPass rp;
    if (vkCreateRenderPass(g_vk_ctx.device, &ci, NULL, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    return rp;
}

vg_lite_error_t vg_lite_vulkan_set_render_target(vg_lite_buffer_t *target)
{
    if (!target->handle) return VG_LITE_INVALID_ARGUMENT;
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;
    
    static int rp_debug = 0;
    if (rp_debug < 3) {
        printf("set_render_target: current_fb_image=%p, new_image=%p\n", 
               (void*)g_vk_ctx.current_fb_image, (void*)internal->image);
        rp_debug++;
    }
    
    if (g_vk_ctx.current_fb_image == internal->image) return VG_LITE_SUCCESS;
    
    if (g_vk_ctx.current_fb) { vg_lite_vulkan_end_render_pass(); }
    
    if (internal->render_pass == VK_NULL_HANDLE) {
        VkFormat vkfmt = vg_lite_format_to_vk(target->format);
        internal->render_pass = vg_lite_vulkan_create_render_pass(vkfmt);
    }
    
    if (internal->depth_stencil_image == VK_NULL_HANDLE) {
        printf("Creating depth/stencil image: %dx%d\n", target->width, target->height);
        VkImageCreateInfo img_ci = {0};
        img_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_ci.imageType = VK_IMAGE_TYPE_2D;
        img_ci.format = VK_FORMAT_D24_UNORM_S8_UINT;
        img_ci.extent.width = target->width;
        img_ci.extent.height = target->height;
        img_ci.extent.depth = 1;
        img_ci.mipLevels = 1;
        img_ci.arrayLayers = 1;
        img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        img_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_vk_ctx.device, &img_ci, NULL, &internal->depth_stencil_image) != VK_SUCCESS)
            return VG_LITE_OUT_OF_MEMORY;
        
        VkMemoryRequirements mem_req;
        vkGetImageMemoryRequirements(g_vk_ctx.device, internal->depth_stencil_image, &mem_req);
        VkMemoryAllocateInfo alloc_ci = {0};
        alloc_ci.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_ci.allocationSize = mem_req.size;
        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_props);
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((mem_req.memoryTypeBits & (1 << i)) && 
                (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                alloc_ci.memoryTypeIndex = i; break;
            }
        }
        if (vkAllocateMemory(g_vk_ctx.device, &alloc_ci, NULL, &internal->depth_stencil_memory) != VK_SUCCESS)
            return VG_LITE_OUT_OF_MEMORY;
        printf("Depth/stencil memory allocated: size=%zu\n", alloc_ci.allocationSize);
        vkBindImageMemory(g_vk_ctx.device, internal->depth_stencil_image, internal->depth_stencil_memory, 0);
        
        VkImageViewCreateInfo view_ci = {0};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = internal->depth_stencil_image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_D24_UNORM_S8_UINT;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->depth_stencil_view) != VK_SUCCESS)
            return VG_LITE_OUT_OF_MEMORY;
        
        VkImageMemoryBarrier stencil_barrier = {0};
        stencil_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        stencil_barrier.srcAccessMask = 0;
        stencil_barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        stencil_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        stencil_barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        stencil_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stencil_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        stencil_barrier.image = internal->depth_stencil_image;
        stencil_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        stencil_barrier.subresourceRange.baseMipLevel = 0;
        stencil_barrier.subresourceRange.levelCount = 1;
        stencil_barrier.subresourceRange.baseArrayLayer = 0;
        stencil_barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, NULL, 0, NULL, 1, &stencil_barrier);
    }
    
    VkImageView fb_views[2] = {internal->view, internal->depth_stencil_view};
    VkFramebufferCreateInfo fb_ci = {0};
    fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass = internal->render_pass;
    fb_ci.attachmentCount = 2;
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
    
    VkClearValue clear_values[2] = {0};
    clear_values[1].depthStencil.depth = 0.0f;
    clear_values[1].depthStencil.stencil = 0;
    
    VkRenderPassBeginInfo rpbi = {0};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = internal->render_pass;
    rpbi.framebuffer = fb;
    rpbi.renderArea.offset.x = 0;
    rpbi.renderArea.offset.y = 0;
    rpbi.renderArea.extent.width = target->width;
    rpbi.renderArea.extent.height = target->height;
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clear_values;
    
    static int rp_begin_debug = 0;
    if (rp_begin_debug < 3) {
        printf("vkCmdBeginRenderPass: fb=%p, renderPass=%p\n", (void*)fb, (void*)internal->render_pass);
        rp_begin_debug++;
    }
    
    vkCmdBeginRenderPass(g_vk_ctx.cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_end_render_pass(void)
{
    static int rp_end_debug = 0;
    if (rp_end_debug < 3) {
        printf("vkCmdEndRenderPass: current_fb=%p\n", (void*)g_vk_ctx.current_fb);
        rp_end_debug++;
    }
    
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

static VkPipeline create_blit_pipeline(VkFormat format, int blend_group)
{
    if (!g_vk_ctx.vert_shader) {
        extern const uint32_t g_vert_spv_data[];
        extern const uint32_t g_frag_spv_data[];
        extern const uint32_t g_vert_spv_size;
        extern const uint32_t g_frag_spv_size;
        g_vk_ctx.vert_shader = create_shader_module(g_vert_spv_data, (size_t)g_vert_spv_size);
        g_vk_ctx.frag_shader = create_shader_module(g_frag_spv_data, (size_t)g_frag_spv_size);
    }

    if (!g_vk_ctx.blit_pipeline_layout) {
        VkPushConstantRange pc_range = {0};
        pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
        pc_range.offset = 0;
        pc_range.size = 68;

        VkDescriptorSetLayoutBinding bindings[2] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        };
        VkDescriptorSetLayoutCreateInfo ds_ci = {0};
        ds_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ds_ci.bindingCount = 2;
        ds_ci.pBindings = bindings;
        vkCreateDescriptorSetLayout(g_vk_ctx.device, &ds_ci, NULL, &g_vk_ctx.blit_descriptor_layout);

        VkPipelineLayoutCreateInfo pl_ci = {0};
        pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl_ci.setLayoutCount = 1;
        pl_ci.pSetLayouts = &g_vk_ctx.blit_descriptor_layout;
        pl_ci.pushConstantRangeCount = 1;
        pl_ci.pPushConstantRanges = &pc_range;
        vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL, &g_vk_ctx.blit_pipeline_layout);
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_vk_ctx.vert_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_vk_ctx.frag_shader, "main", NULL},
    };
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; ms.minSampleShading = 1.0f;

    VkPipelineColorBlendAttachmentState cba;
    get_blend_attachment_state(blend_group, &cba);
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;

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
    dyn_ci.dynamicStateCount = 2; dyn_ci.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp_ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_ci.stageCount = 2; gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi; gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vs;
    gp_ci.pRasterizationState = &rs; gp_ci.pMultisampleState = &ms;
    gp_ci.pColorBlendState = &cb; gp_ci.pDepthStencilState = &ds; gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = g_vk_ctx.blit_pipeline_layout;
    gp_ci.renderPass = rp; gp_ci.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &pipeline);
    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
    return pipeline;
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
}
