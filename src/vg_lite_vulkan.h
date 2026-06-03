#ifndef VG_LITE_VULKAN_H
#define VG_LITE_VULKAN_H

#include <vulkan/vulkan.h>
#include "vg_lite.h"

#define BG_SHADER     0
#define BG_SRC_OVER   1
#define BG_DST_OVER   2
#define BG_ADDITIVE   3
#define BG_SUBTRACT   4
#define BG_NONE       5
#define BG_COUNT      6

#define MAX_PIPELINE_CACHE 64

typedef struct {
    VkImage image;
    VkImageView view;        /* identity swizzle - for framebuffer attachment */
    VkImageView swizzle_view; /* L8/A8 swizzle - for texture sampling */
    VkDeviceMemory memory;
    VkRenderPass render_pass;
    VkSampler sampler;
    void *mapped_base;       /* host mapped memory pointer for vkUnmapMemory */
    /* Depth/stencil for fill rules */
    VkImage depth_stencil_image;
    VkImageView depth_stencil_view;
    VkDeviceMemory depth_stencil_memory;
    /* MSAA attachments */
    VkImage msaa_color_image;
    VkImageView msaa_color_view;
    VkDeviceMemory msaa_color_memory;
    VkImage msaa_depth_image;
    VkImageView msaa_depth_view;
    VkDeviceMemory msaa_depth_memory;
} buffer_internal_t;

typedef struct {
    VkPipeline pipeline;
    VkFormat format;
    int blend_group;
    int no_msaa;
} pipeline_cache_entry_t;

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family_index;

    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;
    VkFence fence;

    VkFramebuffer current_fb;
    VkRenderPass render_pass;
    VkImageView current_fb_view;
    VkImage current_fb_image;
    uint32_t current_fb_width;
    uint32_t current_fb_height;

    #define MAX_PENDING_FB 32
    VkFramebuffer pending_fb[MAX_PENDING_FB];
    int pending_fb_count;
    VkRenderPass pending_rp[MAX_PENDING_FB];
    int pending_rp_count;

    VkCommandBuffer cmd_buf;
    int cmd_buf_recording;
    int cmd_buf_submitted;

    pipeline_cache_entry_t pipeline_cache[MAX_PIPELINE_CACHE];
    int pipeline_cache_count;

    VkPipelineLayout blit_pipeline_layout;
    VkDescriptorSetLayout blit_descriptor_layout;
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    /* Native blend pipeline (2-binding: sampler + SSBO) */
    VkPipelineLayout native_pipeline_layout;
    VkDescriptorSetLayout native_descriptor_layout;
    VkShaderModule native_frag_shader;
    /* SSBO for blit shader parameters */
    VkBuffer blit_ssbo_buffer;
    VkDeviceMemory blit_ssbo_memory;
    void *blit_ssbo_mapped;

    /* Pattern pipeline for vg_lite_draw_pattern */
    VkPipelineLayout pattern_pipeline_layout;
    VkDescriptorSetLayout pattern_descriptor_layout;
    VkShaderModule pattern_vert_shader;
    VkShaderModule pattern_frag_shader;
    pipeline_cache_entry_t pattern_pipeline_cache[MAX_PIPELINE_CACHE];
    int pattern_pipeline_cache_count;

    VkDebugUtilsMessengerEXT debug_messenger;
    
    VkBuffer clut_buffer;
    VkDeviceMemory clut_memory;
    void *clut_mapped;
} vk_context_t;

extern vk_context_t g_vk_ctx;

vg_lite_error_t vg_lite_vulkan_init(void);
vg_lite_error_t vg_lite_vulkan_destroy(void);

vg_lite_error_t vg_lite_vulkan_set_render_target(vg_lite_buffer_t *target);
vg_lite_error_t vg_lite_vulkan_end_render_pass(void);

vg_lite_error_t vg_lite_vulkan_begin_command(void);
vg_lite_error_t vg_lite_vulkan_submit_command(int wait);

int vg_lite_blend_to_group(vg_lite_blend_t blend);
VkPipeline vg_lite_vulkan_get_pipeline(VkFormat format, int blend_group);
VkPipeline vg_lite_vulkan_get_pipeline_no_msaa(VkFormat format, int blend_group);
vg_lite_error_t vg_lite_vulkan_set_render_target_no_msaa(vg_lite_buffer_t *target);
VkPipeline vg_lite_vulkan_get_pattern_pipeline(VkFormat format, int blend_group);
void vg_lite_vulkan_destroy_pipelines(void);

VkRenderPass vg_lite_vulkan_create_render_pass(VkFormat format);

#endif
