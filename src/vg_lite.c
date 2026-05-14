#include "vg_lite.h"
#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "spv_vert.h"
#include "spv_frag.h"

extern vg_lite_error_t vg_lite_draw_impl(vg_lite_buffer_t *t, vg_lite_path_t *p, 
                                         vg_lite_fill_t fl, vg_lite_matrix_t *m, 
                                         vg_lite_blend_t b, vg_lite_color_t c);
extern void vg_lite_draw_cleanup(void);
extern void vg_lite_draw_cleanup_pending_buffers(void);
#include "vg_lite_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "spv_vert.h"
#include "spv_frag.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_initialized = 0;

static int32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(g_vk_ctx.physical_device, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props)
            return (int32_t)i;
    }
    return -1;
}

static VkSampler s_sampler = VK_NULL_HANDLE;

static VkSampler get_or_create_sampler(void)
{
    if (s_sampler != VK_NULL_HANDLE) return s_sampler;
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = VK_FILTER_LINEAR;
    ci.minFilter = VK_FILTER_LINEAR;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.anisotropyEnable = VK_FALSE;
    ci.maxAnisotropy = 1.0f;
    ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    ci.unnormalizedCoordinates = VK_FALSE;
    ci.compareEnable = VK_FALSE;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkCreateSampler(g_vk_ctx.device, &ci, NULL, &s_sampler);
    return s_sampler;
}

static void destroy_sampler(void)
{
    if (s_sampler != VK_NULL_HANDLE && g_vk_ctx.device) {
        vkDestroySampler(g_vk_ctx.device, s_sampler, NULL);
        s_sampler = VK_NULL_HANDLE;
    }
}

vg_lite_error_t vg_lite_init(vg_lite_uint32_t tess_width, vg_lite_uint32_t tess_height)
{
    (void)tess_width; (void)tess_height;
    if (g_initialized) return VG_LITE_SUCCESS;
    vg_lite_error_t err = vg_lite_vulkan_init();
    if (err != VG_LITE_SUCCESS) return err;
    g_initialized = 1;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_close(void)
{
    if (!g_initialized) return VG_LITE_SUCCESS;
    vg_lite_draw_cleanup();
    destroy_sampler();
    vg_lite_vulkan_destroy();
    g_initialized = 0;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_get_info(vg_lite_info_t *info)
{
    if (!info) return VG_LITE_INVALID_ARGUMENT;
    info->api_version = VGLITE_API_VERSION_3_0;
    info->header_version = VGLITE_HEADER_VERSION;
    info->release_version = VGLITE_RELEASE_VERSION;
    info->reserved = 0;
    return VG_LITE_SUCCESS;
}

vg_lite_uint32_t vg_lite_get_product_info(vg_lite_char *name, vg_lite_uint32_t *chip_id, vg_lite_uint32_t *chip_rev)
{
    if (name) strncpy(name, "Vulkan_llvmpipe", 64);
    if (chip_id) *chip_id = 0x1234;
    if (chip_rev) *chip_rev = 0x0001;
    return VG_LITE_SUCCESS;
}

vg_lite_uint32_t vg_lite_query_feature(vg_lite_feature_t feature)
{
    switch (feature) {
    case gcFEATURE_BIT_VG_PE_CLEAR:     return VGL_TRUE;
    case gcFEATURE_BIT_VG_IM_INPUT:     return VGL_TRUE;
    case gcFEATURE_BIT_VG_NEW_BLEND_MODE: return VGL_TRUE;
    case gcFEATURE_BIT_VG_BORDER_CULLING: return VGL_TRUE;
    case gcFEATURE_BIT_VG_SCISSOR:      return VGL_TRUE;
    default:                            return VGL_FALSE;
    }
}

vg_lite_error_t vg_lite_allocate(vg_lite_buffer_t *buffer)
{
    if (!buffer) return VG_LITE_INVALID_ARGUMENT;
    if (!g_initialized) return VG_LITE_NO_CONTEXT;
    buffer->stride = vg_lite_format_stride(buffer->format, buffer->width);
    buffer->tiled = VG_LITE_LINEAR;
    VkFormat vkfmt = vg_lite_format_to_vk(buffer->format);

    VkImageFormatProperties img_fmt_props;
    if (vkGetPhysicalDeviceImageFormatProperties(g_vk_ctx.physical_device, vkfmt,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            0, &img_fmt_props) != VK_SUCCESS) {
        return VG_LITE_NOT_SUPPORT;
    }

    VkImageCreateInfo img_ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = vkfmt;
    img_ci.extent.width = buffer->width;
    img_ci.extent.height = buffer->height;
    img_ci.extent.depth = 1;
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_LINEAR;
    img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    buffer_internal_t *internal = calloc(1, sizeof(buffer_internal_t));
    if (vkCreateImage(g_vk_ctx.device, &img_ci, NULL, &internal->image) != VK_SUCCESS) {
        free(internal); return VG_LITE_OUT_OF_MEMORY;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(g_vk_ctx.device, internal->image, &mem_req);
    VkMemoryAllocateInfo alloc_ci = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_ci.allocationSize = mem_req.size;
    int32_t mem_type = find_memory_type(mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type < 0) {
        vkDestroyImage(g_vk_ctx.device, internal->image, NULL);
        free(internal); return VG_LITE_OUT_OF_MEMORY;
    }
    alloc_ci.memoryTypeIndex = (uint32_t)mem_type;
    if (vkAllocateMemory(g_vk_ctx.device, &alloc_ci, NULL, &internal->memory) != VK_SUCCESS) {
        vkDestroyImage(g_vk_ctx.device, internal->image, NULL);
        free(internal); return VG_LITE_OUT_OF_MEMORY;
    }
    vkBindImageMemory(g_vk_ctx.device, internal->image, internal->memory, 0);

    VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_ci.image = internal->image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vkfmt;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->view);
    internal->swizzle_view = VK_NULL_HANDLE;

    if (buffer->format == VG_LITE_L8) {
        view_ci.components.r = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_ONE;
        vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view);
    } else if (buffer->format == VG_LITE_A8) {
        view_ci.components.r = VK_COMPONENT_SWIZZLE_ZERO;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_ZERO;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_ZERO;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_R;
        vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view);
    }
    internal->render_pass = VK_NULL_HANDLE;
    internal->sampler = VK_NULL_HANDLE;
    internal->mapped_base = NULL;

    /* Get actual image layout in memory (offset and row pitch) */
    VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(g_vk_ctx.device, internal->image, &sub, &layout);
    buffer->stride = layout.rowPitch;

    void *mapped = NULL;
    vkMapMemory(g_vk_ctx.device, internal->memory, 0, VK_WHOLE_SIZE, 0, &mapped);

    /* Transition image from UNDEFINED to GENERAL layout */
    vg_lite_vulkan_begin_command();
    VkImageMemoryBarrier init_bar = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    init_bar.srcAccessMask = 0;
    init_bar.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    init_bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    init_bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    init_bar.image = internal->image;
    init_bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    init_bar.subresourceRange.levelCount = 1;
    init_bar.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1, &init_bar);
    vg_lite_vulkan_submit_command(1);

    buffer->handle = internal;
    buffer->memory = (uint8_t *)mapped + layout.offset;
    buffer->address = 0;

    internal->mapped_base = mapped;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_free(vg_lite_buffer_t *buffer)
{
    if (!buffer || !buffer->handle) return VG_LITE_INVALID_ARGUMENT;
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;
    if (internal->depth_stencil_view) vkDestroyImageView(g_vk_ctx.device, internal->depth_stencil_view, NULL);
    if (internal->depth_stencil_image) vkDestroyImage(g_vk_ctx.device, internal->depth_stencil_image, NULL);
    if (internal->depth_stencil_memory) vkFreeMemory(g_vk_ctx.device, internal->depth_stencil_memory, NULL);
    if (internal->view) vkDestroyImageView(g_vk_ctx.device, internal->view, NULL);
    if (internal->swizzle_view) vkDestroyImageView(g_vk_ctx.device, internal->swizzle_view, NULL);
    if (internal->render_pass) vkDestroyRenderPass(g_vk_ctx.device, internal->render_pass, NULL);
    if (internal->image) vkDestroyImage(g_vk_ctx.device, internal->image, NULL);
    if (internal->mapped_base) vkUnmapMemory(g_vk_ctx.device, internal->memory);
    if (internal->memory) vkFreeMemory(g_vk_ctx.device, internal->memory, NULL);
    free(internal);
    buffer->handle = NULL;
    buffer->memory = NULL;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_clear(vg_lite_buffer_t *target, vg_lite_rectangle_t *rect, vg_lite_color_t color)
{
    if (!target) return VG_LITE_INVALID_ARGUMENT;
    if (!g_initialized) return VG_LITE_NO_CONTEXT;
    
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;
    VkFormat vkfmt = vg_lite_format_to_vk(target->format);
    
    VkClearAttachment clear_att = {0};
    clear_att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clear_att.colorAttachment = 0;
    
    uint8_t r = (color)       & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;
    
    if (target->format == VG_LITE_L8) {
        float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        clear_att.clearValue.color.float32[0] = lum / 255.0f;
        clear_att.clearValue.color.float32[1] = 0.0f;
        clear_att.clearValue.color.float32[2] = 0.0f;
        clear_att.clearValue.color.float32[3] = 1.0f;
    } else if (target->format == VG_LITE_A8) {
        clear_att.clearValue.color.float32[0] = (float)a / 255.0f;
        clear_att.clearValue.color.float32[1] = 0.0f;
        clear_att.clearValue.color.float32[2] = 0.0f;
        clear_att.clearValue.color.float32[3] = 1.0f;
    } else if (vkfmt == VK_FORMAT_B8G8R8A8_UNORM) {
        clear_att.clearValue.color.float32[0] = (float)r / 255.0f;
        clear_att.clearValue.color.float32[1] = (float)g / 255.0f;
        clear_att.clearValue.color.float32[2] = (float)b / 255.0f;
        clear_att.clearValue.color.float32[3] = (float)a / 255.0f;
    } else {
        clear_att.clearValue.color.float32[0] = (float)r / 255.0f;
        clear_att.clearValue.color.float32[1] = (float)g / 255.0f;
        clear_att.clearValue.color.float32[2] = (float)b / 255.0f;
        clear_att.clearValue.color.float32[3] = (float)a / 255.0f;
    }
    printf("vg_lite_clear: color=0x%08x, vkfmt=%d, clear=[%.2f,%.2f,%.2f,%.2f]\n",
           color, vkfmt,
           clear_att.clearValue.color.float32[0],
           clear_att.clearValue.color.float32[1],
           clear_att.clearValue.color.float32[2],
           clear_att.clearValue.color.float32[3]);
    
    vg_lite_vulkan_begin_command();
    vg_lite_vulkan_set_render_target(target);

    VkClearRect clear_rect;
    if (rect) {
        int32_t x = rect->x < 0 ? 0 : rect->x;
        int32_t y = rect->y < 0 ? 0 : rect->y;
        int32_t r_bound = (rect->x + rect->width) > target->width ? target->width : (rect->x + rect->width);
        int32_t b_bound = (rect->y + rect->height) > target->height ? target->height : (rect->y + rect->height);
        if (x >= r_bound || y >= b_bound) return VG_LITE_SUCCESS;
        clear_rect.rect.offset.x = x;
        clear_rect.rect.offset.y = y;
        clear_rect.rect.extent.width = r_bound - x;
        clear_rect.rect.extent.height = b_bound - y;
    } else {
        clear_rect.rect.offset.x = 0;
        clear_rect.rect.offset.y = 0;
        clear_rect.rect.extent.width = target->width;
        clear_rect.rect.extent.height = target->height;
    }
    clear_rect.baseArrayLayer = 0;
    clear_rect.layerCount = 1;
    vkCmdClearAttachments(g_vk_ctx.cmd_buf, 1, &clear_att, 1, &clear_rect);
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_blit(vg_lite_buffer_t *target,
                             vg_lite_buffer_t *source,
                             vg_lite_matrix_t *matrix,
                             vg_lite_blend_t blend,
                             vg_lite_color_t color,
                             vg_lite_filter_t filter)
{
    if (!target || !source) return VG_LITE_INVALID_ARGUMENT;
    if (!g_initialized) return VG_LITE_NO_CONTEXT;

    buffer_internal_t *target_int = (buffer_internal_t *)target->handle;
    buffer_internal_t *src_int = (buffer_internal_t *)source->handle;
    VkFormat vkfmt = vg_lite_format_to_vk(target->format);

    int blend_group = vg_lite_blend_to_group(blend);
    /* Native blend only works correctly when the framebuffer stores all 4 channels
       (BGRA8888). For L8/A8/RGB565 targets, the GPU blends on 4 channels but only
       stores R (L8/A8) or packs to 16-bit (RGB565), losing data in the process.
       Fall back to shader-blend for non-BGRA8888 targets. */
    if (blend_group != BG_SHADER && target->format != VG_LITE_BGRA8888)
        blend_group = BG_SHADER;
    int native_blend = (blend_group != BG_SHADER);

    VkPipeline pipeline = vg_lite_vulkan_get_pipeline(vkfmt, blend_group);
    if (!pipeline) return VG_LITE_OUT_OF_MEMORY;
    vg_lite_vulkan_begin_command();
    vg_lite_vulkan_end_render_pass();

    /* Compute shader matrix: maps normalized frag_pos [0,1] -> source UV [0,1]
       VGLite matrix M: dst_pixel = M * src_pixel
       Need: src_uv = inv(M) * (frag_pos * dst_size) / src_size
       Combined: shader_mat = Scale(1/src_w, 1/src_h) * inv(M) * Scale(dst_w, dst_h) */
    vg_lite_matrix_t M = *matrix;
    float det00 = M.m[1][1]*M.m[2][2] - M.m[2][1]*M.m[1][2];
    float det01 = M.m[2][0]*M.m[1][2] - M.m[1][0]*M.m[2][2];
    float det02 = M.m[1][0]*M.m[2][1] - M.m[2][0]*M.m[1][1];
    float det = M.m[0][0]*det00 + M.m[0][1]*det01 + M.m[0][2]*det02;
    if (fabsf(det) < 1e-7f) det = 1e-7f;
    float inv_det = 1.0f / det;

    vg_lite_matrix_t inv;
    memset(&inv, 0, sizeof(inv));
    inv.m[0][0] = inv_det * det00;
    inv.m[0][1] = inv_det * (M.m[2][1]*M.m[0][2] - M.m[0][1]*M.m[2][2]);
    inv.m[0][2] = inv_det * (M.m[0][1]*M.m[1][2] - M.m[1][1]*M.m[0][2]);
    inv.m[1][0] = inv_det * det01;
    inv.m[1][1] = inv_det * (M.m[0][0]*M.m[2][2] - M.m[2][0]*M.m[0][2]);
    inv.m[1][2] = inv_det * (M.m[1][0]*M.m[0][2] - M.m[0][0]*M.m[1][2]);
    inv.m[2][0] = inv_det * det02;
    inv.m[2][1] = inv_det * (M.m[2][0]*M.m[0][1] - M.m[0][0]*M.m[2][1]);
    inv.m[2][2] = inv_det * (M.m[0][0]*M.m[1][1] - M.m[1][0]*M.m[0][1]);

    float sw = (float)source->width, sh = (float)source->height;
    float dw = (float)target->width, dh = (float)target->height;

    /* Compute shader_mat = Scale(1/sw, 1/sh) * inv(M) * Scale(dw, dh)
       via two matrix multiplications */
    float s_inv[3][3] = {{1.0f/sw, 0, 0}, {0, 1.0f/sh, 0}, {0, 0, 1}};
    float d_scl[3][3] = {{dw, 0, 0}, {0, dh, 0}, {0, 0, 1}};
    float temp[3][3], shader_mat[3][3];

    /* temp = inv(M) * Scale(dw, dh) */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            temp[i][j] = 0;
            for (int k = 0; k < 3; k++)
                temp[i][j] += inv.m[i][k] * d_scl[k][j];
        }

    /* shader_mat = Scale(1/sw, 1/sh) * temp */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            shader_mat[i][j] = 0;
            for (int k = 0; k < 3; k++)
                shader_mat[i][j] += s_inv[i][k] * temp[k][j];
        }

    /* Temp image to hold copy of target (shader-blend only, native-blend reads dst directly) */
    VkImage tmp_image = VK_NULL_HANDLE;
    VkDeviceMemory tmp_memory = VK_NULL_HANDLE;
    VkImageView tmp_view = VK_NULL_HANDLE;

    if (!native_blend) {
    VkImageCreateInfo tmp_ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    tmp_ci.imageType = VK_IMAGE_TYPE_2D;
    tmp_ci.format = vkfmt;
    tmp_ci.extent.width = target->width;
    tmp_ci.extent.height = target->height;
    tmp_ci.extent.depth = 1;
    tmp_ci.mipLevels = 1;
    tmp_ci.arrayLayers = 1;
    tmp_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    tmp_ci.tiling = VK_IMAGE_TILING_LINEAR;
    tmp_ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    tmp_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    tmp_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g_vk_ctx.device, &tmp_ci, NULL, &tmp_image) != VK_SUCCESS)
        return VG_LITE_OUT_OF_MEMORY;

    VkMemoryRequirements tmp_req;
    vkGetImageMemoryRequirements(g_vk_ctx.device, tmp_image, &tmp_req);
    VkMemoryAllocateInfo tmp_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    tmp_alloc.allocationSize = tmp_req.size;
    int32_t tmp_mem_type = find_memory_type(tmp_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (tmp_mem_type < 0) {
        vkDestroyImage(g_vk_ctx.device, tmp_image, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }
    tmp_alloc.memoryTypeIndex = (uint32_t)tmp_mem_type;
    if (vkAllocateMemory(g_vk_ctx.device, &tmp_alloc, NULL, &tmp_memory) != VK_SUCCESS) {
        vkDestroyImage(g_vk_ctx.device, tmp_image, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }
    vkBindImageMemory(g_vk_ctx.device, tmp_image, tmp_memory, 0);

    VkImageViewCreateInfo tmpv_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    tmpv_ci.image = tmp_image;
    tmpv_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    tmpv_ci.format = vkfmt;
    tmpv_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    tmpv_ci.subresourceRange.levelCount = 1;
    tmpv_ci.subresourceRange.layerCount = 1;

    if (target->format == VG_LITE_L8) {
        tmpv_ci.components.r = VK_COMPONENT_SWIZZLE_R;
        tmpv_ci.components.g = VK_COMPONENT_SWIZZLE_R;
        tmpv_ci.components.b = VK_COMPONENT_SWIZZLE_R;
        tmpv_ci.components.a = VK_COMPONENT_SWIZZLE_ONE;
    } else if (target->format == VG_LITE_A8) {
        tmpv_ci.components.r = VK_COMPONENT_SWIZZLE_ZERO;
        tmpv_ci.components.g = VK_COMPONENT_SWIZZLE_ZERO;
        tmpv_ci.components.b = VK_COMPONENT_SWIZZLE_ZERO;
        tmpv_ci.components.a = VK_COMPONENT_SWIZZLE_R;
    }

    if (vkCreateImageView(g_vk_ctx.device, &tmpv_ci, NULL, &tmp_view) != VK_SUCCESS) {
        vkDestroyImage(g_vk_ctx.device, tmp_image, NULL);
        vkFreeMemory(g_vk_ctx.device, tmp_memory, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }

    /* Transition tmp to GENERAL (LINEAR tiling must use GENERAL layout) */
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = tmp_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    /* Copy target -> tmp */
    VkImageCopy cp = {0};
    cp.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.srcSubresource.layerCount = 1;
    cp.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cp.dstSubresource.layerCount = 1;
    cp.extent.width = target->width;
    cp.extent.height = target->height;
    cp.extent.depth = 1;
    vkCmdCopyImage(g_vk_ctx.cmd_buf,
        target_int->image, VK_IMAGE_LAYOUT_GENERAL,
        tmp_image, VK_IMAGE_LAYOUT_GENERAL, 1, &cp);

    /* Transition tmp to shader read (still GENERAL for LINEAR tiling) */
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = tmp_image;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    }

    /* Begin render pass */
    vg_lite_vulkan_set_render_target(target);

    /* Descriptor set */
    VkDescriptorSetAllocateInfo ds_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ds_alloc.descriptorPool = g_vk_ctx.descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = &g_vk_ctx.blit_descriptor_layout;
    VkDescriptorSet desc_set;
    if (vkAllocateDescriptorSets(g_vk_ctx.device, &ds_alloc, &desc_set) != VK_SUCCESS)
        return VG_LITE_OUT_OF_MEMORY;

    VkSampler sampler = get_or_create_sampler();
    VkImageView src_view = src_int->swizzle_view ? src_int->swizzle_view : src_int->view;
    VkDescriptorImageInfo si = {sampler, src_view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo di;
    if (native_blend) {
        di = si;
    } else {
        di = (VkDescriptorImageInfo){sampler, tmp_view, VK_IMAGE_LAYOUT_GENERAL};
    }
    VkWriteDescriptorSet ws[2] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &si, NULL, NULL},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di, NULL, NULL},
    };
    vkUpdateDescriptorSets(g_vk_ctx.device, 2, ws, 0, NULL);

    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
        g_vk_ctx.blit_pipeline_layout, 0, 1, &desc_set, 0, NULL);

    /* Push constants — mat3 has vec4 column alignment in GLSL (16 bytes per column) */
    struct { float m[12]; int blend; unsigned color; int im_mode; int filt; int flags; } pc;
    memset(&pc, 0, sizeof(pc));
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) pc.m[j*4+i] = shader_mat[i][j];
    pc.blend = (int)blend; pc.color = color;
    pc.im_mode = (int)VG_LITE_NORMAL_IMAGE_MODE; pc.filt = (int)filter;
    pc.flags = 0;
    if (target->format == VG_LITE_L8)  pc.flags |= 1;
    if (target->format == VG_LITE_A8)  pc.flags |= 2;
    if (native_blend)                   pc.flags |= 4;
    vkCmdPushConstants(g_vk_ctx.cmd_buf, g_vk_ctx.blit_pipeline_layout,
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    VkViewport vp = {0, 0, (float)target->width, (float)target->height, 0, 1};
    VkRect2D sc = {{0,0}, {target->width, target->height}};
    vkCmdSetViewport(g_vk_ctx.cmd_buf, 0, 1, &vp);
    vkCmdSetScissor(g_vk_ctx.cmd_buf, 0, 1, &sc);
    vkCmdDraw(g_vk_ctx.cmd_buf, 3, 1, 0, 0);
    vg_lite_vulkan_end_render_pass();

    /* Cleanup temp resources after submit */
    vg_lite_vulkan_submit_command(1);
    vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &desc_set);
    if (!native_blend) {
        vkDestroyImageView(g_vk_ctx.device, tmp_view, NULL);
        vkDestroyImage(g_vk_ctx.device, tmp_image, NULL);
        vkFreeMemory(g_vk_ctx.device, tmp_memory, NULL);
    }
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_finish(void)
{
    vg_lite_vulkan_end_render_pass();
    vg_lite_vulkan_submit_command(1);
    vg_lite_draw_cleanup_pending_buffers();
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_flush(void)
{
    vg_lite_vulkan_end_render_pass();
    vg_lite_vulkan_submit_command(0);
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_identity(vg_lite_matrix_t *matrix)
{
    if (!matrix) return VG_LITE_INVALID_ARGUMENT;
    memset(matrix, 0, sizeof(*matrix));
    matrix->m[0][0] = 1.0f;
    matrix->m[1][1] = 1.0f;
    matrix->m[2][2] = 1.0f;
    matrix->scaleX = 1.0f;
    matrix->scaleY = 1.0f;
    matrix->angle = 0.0f;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_translate(vg_lite_float_t x, vg_lite_float_t y, vg_lite_matrix_t *matrix)
{
    if (!matrix) return VG_LITE_INVALID_ARGUMENT;
    vg_lite_matrix_t t;
    vg_lite_identity(&t);
    t.m[0][2] = x;
    t.m[1][2] = y;
    vg_lite_matrix_t result = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                result.m[i][j] += matrix->m[i][k] * t.m[k][j];
    *matrix = result;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_scale(vg_lite_float_t sx, vg_lite_float_t sy, vg_lite_matrix_t *matrix)
{
    if (!matrix) return VG_LITE_INVALID_ARGUMENT;
    vg_lite_matrix_t t;
    vg_lite_identity(&t);
    t.m[0][0] = sx;
    t.m[1][1] = sy;
    vg_lite_matrix_t result = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                result.m[i][j] += matrix->m[i][k] * t.m[k][j];
    *matrix = result;
    matrix->scaleX = sx;
    matrix->scaleY = sy;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_rotate(vg_lite_float_t degrees, vg_lite_matrix_t *matrix)
{
    if (!matrix) return VG_LITE_INVALID_ARGUMENT;
    float rad = degrees * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    vg_lite_matrix_t t;
    vg_lite_identity(&t);
    t.m[0][0] = c;  t.m[0][1] = -s;
    t.m[1][0] = s;  t.m[1][1] = c;
    vg_lite_matrix_t result = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                result.m[i][j] += matrix->m[i][k] * t.m[k][j];
    *matrix = result;
    matrix->angle = degrees;
    return VG_LITE_SUCCESS;
}

/* Stub APIs - return NOT_SUPPORT */
vg_lite_error_t vg_lite_blit_rect(vg_lite_buffer_t *t, vg_lite_buffer_t *s, vg_lite_rectangle_t *r,
    vg_lite_matrix_t *m, vg_lite_blend_t b, vg_lite_color_t c, vg_lite_filter_t f)
{ (void)t;(void)s;(void)r;(void)m;(void)b;(void)c;(void)f; return VG_LITE_NOT_SUPPORT; }

vg_lite_error_t vg_lite_blit2(vg_lite_buffer_t *t, vg_lite_buffer_t *s0, vg_lite_buffer_t *s1,
    vg_lite_matrix_t *m0, vg_lite_matrix_t *m1, vg_lite_blend_t b, vg_lite_filter_t f)
{ (void)t;(void)s0;(void)s1;(void)m0;(void)m1;(void)b;(void)f; return VG_LITE_NOT_SUPPORT; }

vg_lite_error_t vg_lite_draw(vg_lite_buffer_t *t, vg_lite_path_t *p, vg_lite_fill_t fl,
    vg_lite_matrix_t *m, vg_lite_blend_t b, vg_lite_color_t c)
{ 
    return vg_lite_draw_impl(t, p, fl, m, b, c);
}

vg_lite_error_t vg_lite_set_color_key(vg_lite_color_key4_t ck) { (void)ck; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_enable_dither(void) { return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_disable_dither(void) { return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_source_global_alpha(vg_lite_global_alpha_t m, vg_lite_uint8_t v) { (void)m;(void)v; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_dest_global_alpha(vg_lite_global_alpha_t m, vg_lite_uint8_t v) { (void)m;(void)v; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_set_CLUT(vg_lite_uint32_t c, vg_lite_uint32_t *cl) { (void)c;(void)cl; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_gaussian_filter(vg_lite_float_t w0, vg_lite_float_t w1, vg_lite_float_t w2) { (void)w0;(void)w1;(void)w2; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_upload_buffer(vg_lite_buffer_t *b, vg_lite_uint8_t *d[3], vg_lite_uint32_t s[3]) { (void)b;(void)d;(void)s; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_map(vg_lite_buffer_t *b, vg_lite_map_flag_t f, int32_t fd) { (void)b;(void)f;(void)fd; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_unmap(vg_lite_buffer_t *b) { (void)b; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_flush_mapped_buffer(vg_lite_buffer_t *b) { (void)b; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_dump_png(const char *fn, vg_lite_buffer_t *b) { (void)fn;(void)b; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_wrap_user_memory(vg_lite_buffer_t *b, vg_lite_uint8_t *m, vg_lite_uint32_t s, vg_lite_uint32_t w, vg_lite_uint32_t h, vg_lite_buffer_format_t f) { (void)b;(void)m;(void)s;(void)w;(void)h;(void)f; return VG_LITE_NOT_SUPPORT; }
