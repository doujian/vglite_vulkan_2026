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
    VkAttachmentDescription att = {0};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
    att.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    VkRenderPass rp;
    if (vkCreateRenderPass(g_vk_ctx.device, &ci, NULL, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    return rp;
}

typedef struct {
    VkImage image;
    VkImageView view;
    VkImageView swizzle_view;
    VkDeviceMemory memory;
    VkRenderPass render_pass;
    VkSampler sampler;
} buffer_internal_t;

vg_lite_error_t vg_lite_vulkan_set_render_target(vg_lite_buffer_t *target)
{
    if (g_vk_ctx.current_fb) { vg_lite_vulkan_end_render_pass(); }
    if (!target->handle) return VG_LITE_INVALID_ARGUMENT;
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;
    if (internal->render_pass == VK_NULL_HANDLE) {
        VkFormat vkfmt = vg_lite_format_to_vk(target->format);
        internal->render_pass = vg_lite_vulkan_create_render_pass(vkfmt);
    }
    VkFramebufferCreateInfo fb_ci = {0};
    fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb_ci.renderPass = internal->render_pass;
    fb_ci.attachmentCount = 1;
    fb_ci.pAttachments = &internal->view;
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
    VkRenderPassBeginInfo rpbi = {0};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = internal->render_pass;
    rpbi.framebuffer = fb;
    rpbi.renderArea.offset.x = 0;
    rpbi.renderArea.offset.y = 0;
    rpbi.renderArea.extent.width = target->width;
    rpbi.renderArea.extent.height = target->height;
    vkCmdBeginRenderPass(g_vk_ctx.cmd_buf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_vulkan_end_render_pass(void)
{
    if (g_vk_ctx.current_fb) {
        vkCmdEndRenderPass(g_vk_ctx.cmd_buf);
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

vg_lite_error_t vg_lite_vulkan_create_pipelines(VkFormat format)
{
    if (g_vk_ctx.blit_pipeline && g_vk_ctx.blit_pipeline_format == format)
        return VG_LITE_SUCCESS;

    if (g_vk_ctx.blit_pipeline) {
        vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.blit_pipeline, NULL);
        g_vk_ctx.blit_pipeline = VK_NULL_HANDLE;
    }

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
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1; cb.pAttachments = &cba;

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
    gp_ci.pColorBlendState = &cb; gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = g_vk_ctx.blit_pipeline_layout;
    gp_ci.renderPass = rp; gp_ci.subpass = 0;
    vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_vk_ctx.blit_pipeline);
    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
    g_vk_ctx.blit_pipeline_format = format;
    return VG_LITE_SUCCESS;
}

void vg_lite_vulkan_destroy_pipelines(void)
{
    if (g_vk_ctx.blit_pipeline) { vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.blit_pipeline, NULL); g_vk_ctx.blit_pipeline = VK_NULL_HANDLE; }
    if (g_vk_ctx.blit_pipeline_layout) { vkDestroyPipelineLayout(g_vk_ctx.device, g_vk_ctx.blit_pipeline_layout, NULL); g_vk_ctx.blit_pipeline_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.blit_descriptor_layout) { vkDestroyDescriptorSetLayout(g_vk_ctx.device, g_vk_ctx.blit_descriptor_layout, NULL); g_vk_ctx.blit_descriptor_layout = VK_NULL_HANDLE; }
    if (g_vk_ctx.vert_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.vert_shader, NULL); g_vk_ctx.vert_shader = VK_NULL_HANDLE; }
    if (g_vk_ctx.frag_shader) { vkDestroyShaderModule(g_vk_ctx.device, g_vk_ctx.frag_shader, NULL); g_vk_ctx.frag_shader = VK_NULL_HANDLE; }
}
