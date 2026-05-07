#ifndef VG_LITE_VULKAN_H
#define VG_LITE_VULKAN_H

#include <vulkan/vulkan.h>
#include "vg_lite.h"

/* Internal Vulkan context - one global instance */
typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;

    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;
    VkFence fence;

    /* Current render target state */
    VkFramebuffer current_fb;
    VkRenderPass render_pass;
    VkImageView current_fb_view;
    VkImage current_fb_image;
    uint32_t current_fb_width;
    uint32_t current_fb_height;

    /* Pending framebuffers to destroy after submit */
    #define MAX_PENDING_FB 32
    VkFramebuffer pending_fb[MAX_PENDING_FB];
    int pending_fb_count;

    /* Command buffer state */
    VkCommandBuffer cmd_buf;
    int cmd_buf_recording;      /* 1 if command buffer is in recording state */
    int cmd_buf_submitted;      /* 1 if command buffer was submitted and needs reset */

    /* Pipeline cache */
    VkPipeline blit_pipeline;
    VkPipelineLayout blit_pipeline_layout;
    VkDescriptorSetLayout blit_descriptor_layout;
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    VkFormat blit_pipeline_format;

    /* Blend state tracking */
    int current_blend_mode;

    /* Debug */
    VkDebugUtilsMessengerEXT debug_messenger;
} vk_context_t;

/* Global context */
extern vk_context_t g_vk_ctx;

vg_lite_error_t vg_lite_vulkan_init(void);
vg_lite_error_t vg_lite_vulkan_destroy(void);

/* Render target management */
vg_lite_error_t vg_lite_vulkan_set_render_target(vg_lite_buffer_t *target);
vg_lite_error_t vg_lite_vulkan_end_render_pass(void);

/* Command buffer helpers */
vg_lite_error_t vg_lite_vulkan_begin_command(void);
vg_lite_error_t vg_lite_vulkan_submit_command(int wait);

/* Pipeline creation (traditional render pass, NOT dynamic rendering) */
vg_lite_error_t vg_lite_vulkan_create_pipelines(VkFormat format);
void vg_lite_vulkan_destroy_pipelines(void);

/* Render pass creation for a specific format */
VkRenderPass vg_lite_vulkan_create_render_pass(VkFormat format);

#endif
