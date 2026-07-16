#ifndef VG_LITE_VULKAN_H
#define VG_LITE_VULKAN_H

#include "volk.h"
#include "vg_lite.h"
#include <stdio.h>

#define VK_CHECK(call) do { \
    VkResult _r = (call); \
    if (_r != VK_SUCCESS) { \
        fprintf(stderr, "Vulkan error %d at %s:%d: %s\n", _r, __FILE__, __LINE__, #call); \
    } \
} while (0)

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
    /* MSAA attachments */
    VkImage msaa_color_image;
    VkImageView msaa_color_view;
    VkDeviceMemory msaa_color_memory;
    VkImage msaa_depth_image;
    VkImageView msaa_depth_view;
    VkDeviceMemory msaa_depth_memory;
    VkImage resolve_image;
    VkImageView resolve_view;
    VkDeviceMemory resolve_memory;
    int msaa_needs_seed;  /* Set when no-MSAA RP wrote to target; draw must seed MSAA before use */
    uint32_t width;
    uint32_t height;
    int msaa_dirty;
} buffer_internal_t;

typedef struct {
    VkPipeline pipeline;
    VkFormat format;
    int blend_group;
    int mode;  /* 0 = blit.frag shader-blend MSAA; 1 = native no-MSAA; 2 = native + MSAA hardware-blend */
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
    VkImage current_msaa_color_image;
    VkImage current_resolve_image;
    uint32_t current_fb_width;
    uint32_t current_fb_height;
    int current_fb_is_no_msaa;
    buffer_internal_t *current_fb_internal;

    #define MAX_PENDING_FB 32
    #define MAX_PENDING_DESC 64
    VkFramebuffer pending_fb[MAX_PENDING_FB];
    int pending_fb_count;
    VkRenderPass pending_rp[MAX_PENDING_FB];
    int pending_rp_count;
    VkDescriptorSet pending_desc_sets[MAX_PENDING_DESC];
    int pending_desc_count;

    VkCommandBuffer cmd_buf;
    int cmd_buf_recording;
    int cmd_buf_submitted;
    VkCommandBuffer init_cmd_buf; /* separate cmd buf for init/layout barriers */

    pipeline_cache_entry_t pipeline_cache[MAX_PIPELINE_CACHE];
    int pipeline_cache_count;

    VkPipelineLayout blit_pipeline_layout;
    VkDescriptorSetLayout blit_descriptor_layout;
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    /* Native blend pipeline (1-binding: sampler, push constants for params) */
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
    VkPipeline pattern_stencil_pipeline;
    VkBuffer pattern_cover_vbo;
    VkDeviceMemory pattern_cover_vbo_mem;
    VkBuffer pattern_cover_ibo;
    VkDeviceMemory pattern_cover_ibo_mem;
    pipeline_cache_entry_t pattern_pipeline_cache[MAX_PIPELINE_CACHE];
    int pattern_pipeline_cache_count;

    VkDebugUtilsMessengerEXT debug_messenger;
    
    VkBuffer clut_buffer;
    VkDeviceMemory clut_memory;
    void *clut_mapped;

    VkPipelineLayout grad_pipeline_layout;
    VkDescriptorSetLayout grad_descriptor_layout;
    VkShaderModule grad_vert_shader;
    VkShaderModule grad_frag_shader;
    VkPipeline grad_stencil_pipeline;
    VkPipeline grad_cover_pipeline;
    VkBuffer grad_cover_vbo;
    VkDeviceMemory grad_cover_vbo_mem;
    VkBuffer grad_cover_ibo;
    VkDeviceMemory grad_cover_ibo_mem;
    pipeline_cache_entry_t grad_pipeline_cache[MAX_PIPELINE_CACHE];
    int grad_pipeline_cache_count;

    /* Scissor state */
    #define MAX_SCISSOR_RECTS 16
    VkRect2D scissor_rects[MAX_SCISSOR_RECTS];
    int scissor_count;
    int scissor_enabled;

    /* AABB blit optimization */
    VkShaderModule blit_aabb_vert_shader;
    uint8_t use_aabb_blit;              /* 0 = original fullscreen, 1 = AABB pipeline */
    pipeline_cache_entry_t blit_aabb_pipeline_cache[MAX_PIPELINE_CACHE];
    int blit_aabb_pipeline_cache_count;

    /* GPU timestamp query pool */
    VkQueryPool timestamp_query_pool;
    float timestamp_period;             /* nanoseconds per timestamp tick */
    uint32_t timestamp_slot_counter;
} vk_context_t;

/* Helper: set scissor at draw time — uses user scissor if enabled, else full framebuffer */
void vg_lite_vulkan_apply_scissor(uint32_t fb_width, uint32_t fb_height);

extern vk_context_t g_vk_ctx;

int32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props);

vg_lite_error_t vg_lite_vulkan_init(void);
vg_lite_error_t vg_lite_vulkan_destroy(void);

vg_lite_error_t vg_lite_vulkan_set_render_target(vg_lite_buffer_t *target);
vg_lite_error_t vg_lite_vulkan_end_render_pass(void);
/* End the currently-active render pass (if any), resolving the MSAA color
 * attachment to the OPTIMAL intermediate and copying it to the LINEAR target.
 * Called by clear/blit/draw/pattern/grad/free/finish before starting a new
 * render pass or submitting, to flush deferred rendering and make the target's
 * host-visible memory up-to-date. */
void vg_lite_vulkan_flush_render_pass(void);

vg_lite_error_t vg_lite_vulkan_begin_command(void);
vg_lite_error_t vg_lite_vulkan_submit_command(int wait);

int vg_lite_blend_to_group(vg_lite_blend_t blend);
VkPipeline vg_lite_vulkan_get_pipeline(VkFormat format, int blend_group);
VkPipeline vg_lite_vulkan_get_pipeline_no_msaa(VkFormat format, int blend_group);
VkPipeline vg_lite_vulkan_get_pipeline_native_msaa(VkFormat format, int blend_group);
VkPipeline vg_lite_vulkan_get_pipeline_aabb_no_msaa(VkFormat format, int blend_group);
VkPipeline vg_lite_vulkan_get_pipeline_aabb_native_msaa(VkFormat format, int blend_group);

/* GPU timestamp utilities */
void vg_lite_vulkan_write_timestamp(VkPipelineStageFlagBits stage);
uint64_t vg_lite_vulkan_read_timestamp(uint32_t slot);
double vg_lite_vulkan_get_elapsed_ns(uint32_t start_slot, uint32_t end_slot);

vg_lite_error_t vg_lite_vulkan_seed_msaa(vg_lite_buffer_t *target, VkSampler sampler);
vg_lite_error_t vg_lite_vulkan_set_render_target_no_msaa(vg_lite_buffer_t *target);
vg_lite_error_t vg_lite_vulkan_resolve_msaa_to_target(buffer_internal_t *internal);
VkPipeline vg_lite_vulkan_get_pattern_pipeline(VkFormat format, int blend_group);
void vg_lite_vulkan_init_pattern_pipeline(VkFormat format);
VkPipeline vg_lite_vulkan_get_pattern_cover_pipeline(VkFormat format, int blend_group);
void vg_lite_vulkan_destroy_pipelines(void);

void vg_lite_vulkan_init_grad_pipeline(VkFormat format);
VkPipeline vg_lite_vulkan_get_grad_cover_pipeline(VkFormat format, int blend_group);

VkRenderPass vg_lite_vulkan_create_render_pass(VkFormat format);

VkSampler get_or_create_sampler(vg_lite_filter_t filter);

#endif
