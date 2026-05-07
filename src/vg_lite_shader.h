#ifndef VG_LITE_SHADER_H
#define VG_LITE_SHADER_H

#include <vulkan/vulkan.h>
#include "vg_lite.h"
#include "vg_lite_vulkan.h"

typedef struct {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptor_layout;
} vg_lite_pipeline_t;

vg_lite_error_t vg_lite_shader_create_pipeline(vk_context_t *ctx,
                                                vg_lite_pipeline_t *pipeline,
                                                const uint32_t *vert_spv,
                                                size_t vert_size,
                                                const uint32_t *frag_spv,
                                                size_t frag_size,
                                                VkRenderPass render_pass);

vg_lite_error_t vg_lite_shader_destroy_pipeline(vk_context_t *ctx,
                                                 vg_lite_pipeline_t *pipeline);

#endif
