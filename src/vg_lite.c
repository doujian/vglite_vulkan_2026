#include "vg_lite.h"
#include "vg_lite_config.h"
#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vg_lite_math.h"
#include "shader_loader.h"

#if VGLITE_BLIT_PERF
#define BOTTOM_OF_PIPE_BIT 0x00002000

/* CPU fallback timer for environments without GPU timestamp support (e.g. cmodel) */
static int blit_perf_use_cpu_timer = 0;
static int blit_perf_gpu_checked   = 0;

#if defined(_WIN32)
  #include <windows.h>
  static LARGE_INTEGER blit_perf_cpu_start;
  static LARGE_INTEGER blit_perf_cpu_freq;
  static void blit_perf_cpu_record_start(void) {
      QueryPerformanceFrequency(&blit_perf_cpu_freq);
      QueryPerformanceCounter(&blit_perf_cpu_start);
  }
  static uint64_t blit_perf_cpu_read_ns(void) {
      LARGE_INTEGER now;
      QueryPerformanceCounter(&now);
      return (uint64_t)((double)(now.QuadPart - blit_perf_cpu_start.QuadPart)
                        * 1e9 / blit_perf_cpu_freq.QuadPart);
  }
#else
  #include <time.h>
  static struct timespec blit_perf_cpu_start;
  static void blit_perf_cpu_record_start(void) {
      clock_gettime(CLOCK_MONOTONIC, &blit_perf_cpu_start);
  }
  static uint64_t blit_perf_cpu_read_ns(void) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      return (uint64_t)(now.tv_sec - blit_perf_cpu_start.tv_sec) * 1000000000ULL
           + (uint64_t)(now.tv_nsec - blit_perf_cpu_start.tv_nsec);
  }
#endif
#endif

extern vg_lite_error_t vg_lite_draw_impl(vg_lite_buffer_t *t, vg_lite_path_t *p, 
                                         vg_lite_fill_t fl, vg_lite_matrix_t *m, 
                                         vg_lite_blend_t b, vg_lite_color_t c);
extern void vg_lite_draw_cleanup(void);
extern void vg_lite_draw_cleanup_pending_buffers(void);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_initialized = 0;

static VkSampler s_sampler_point = VK_NULL_HANDLE;
static VkSampler s_sampler_linear = VK_NULL_HANDLE;

VkSampler get_or_create_sampler(vg_lite_filter_t filter)
{
    VkSampler *sampler_ptr = (filter == VG_LITE_FILTER_POINT) ? &s_sampler_point : &s_sampler_linear;
    if (*sampler_ptr != VK_NULL_HANDLE) return *sampler_ptr;
    
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = (filter == VG_LITE_FILTER_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.minFilter = (filter == VG_LITE_FILTER_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.anisotropyEnable = VK_FALSE;
    ci.maxAnisotropy = 1.0f;
    ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    ci.unnormalizedCoordinates = VK_FALSE;
    ci.compareEnable = VK_FALSE;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VK_CHECK(vkCreateSampler(g_vk_ctx.device, &ci, NULL, sampler_ptr));
    return *sampler_ptr;
}

static void destroy_sampler(void)
{
    if (s_sampler_point != VK_NULL_HANDLE && g_vk_ctx.device) {
        vkDestroySampler(g_vk_ctx.device, s_sampler_point, NULL);
        s_sampler_point = VK_NULL_HANDLE;
    }
    if (s_sampler_linear != VK_NULL_HANDLE && g_vk_ctx.device) {
        vkDestroySampler(g_vk_ctx.device, s_sampler_linear, NULL);
        s_sampler_linear = VK_NULL_HANDLE;
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
    case gcFEATURE_BIT_VG_RADIAL_GRADIENT: return VGL_TRUE;
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
    VK_CHECK(vkBindImageMemory(g_vk_ctx.device, internal->image, internal->memory, 0));

    VkImageViewCreateInfo view_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_ci.image = internal->image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vkfmt;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->view));
    internal->swizzle_view = VK_NULL_HANDLE;

    if (buffer->format == VG_LITE_L8) {
        view_ci.components.r = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_ONE;
        VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view));
    } else if (buffer->format == VG_LITE_A8) {
        view_ci.components.r = VK_COMPONENT_SWIZZLE_ZERO;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_ZERO;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_ZERO;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_R;
        VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view));
    } else if (buffer->format == VG_LITE_RGB565 || buffer->format == VG_LITE_BGR565) {
        view_ci.components.r = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_G;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_B;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_ONE;
        VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view));
    } else if (buffer->format == VG_LITE_RGBA4444) {
        /* VK R4G4B4A4: R=15:12, G=11:8, B=7:4, A=3:0
         * VGLite RGBA4444: A=15:12, B=11:8, G=7:4, R=3:0
         * Swizzle: shader.r=vkA, shader.g=vkB, shader.b=vkG, shader.a=vkR */
        view_ci.components.r = VK_COMPONENT_SWIZZLE_A;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_B;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_G;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_R;
        VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view));
    } else if (buffer->format == VG_LITE_BGRA4444) {
        /* VK B4G4R4A4: B=15:12, G=11:8, R=7:4, A=3:0
         * VGLite BGRA4444: A=15:12, R=11:8, G=7:4, B=3:0
         * Swizzle: shader.r=vkR, shader.g=vkG, shader.b=vkA, shader.a=vkB */
        view_ci.components.r = VK_COMPONENT_SWIZZLE_R;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_G;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_A;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_B;
        VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view));
    } else if (buffer->format == VG_LITE_ARGB8888) {
        /* VK R8G8B8A8: byte0=R,byte1=G,byte2=B,byte3=A
         * VGLite ARGB8888 mem [A,R,G,B]: byte0=A,byte1=R,byte2=G,byte3=B
         * Swizzle: shader.r=G(byte1=VGR), shader.g=B(byte2=VGG),
         *          shader.b=A(byte3=VGB), shader.a=R(byte0=VGA) */
        view_ci.components.r = VK_COMPONENT_SWIZZLE_G;
        view_ci.components.g = VK_COMPONENT_SWIZZLE_B;
        view_ci.components.b = VK_COMPONENT_SWIZZLE_A;
        view_ci.components.a = VK_COMPONENT_SWIZZLE_R;
        VK_CHECK(vkCreateImageView(g_vk_ctx.device, &view_ci, NULL, &internal->swizzle_view));
    }
    internal->render_pass = VK_NULL_HANDLE;
    internal->sampler = VK_NULL_HANDLE;
    internal->mapped_base = NULL;
    internal->width = buffer->width;
    internal->height = buffer->height;
    internal->msaa_dirty = 0;

    /* Get actual image layout in memory (offset and row pitch) */
    VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout;
    vkGetImageSubresourceLayout(g_vk_ctx.device, internal->image, &sub, &layout);
    buffer->stride = layout.rowPitch;

    void *mapped = NULL;
    VK_CHECK(vkMapMemory(g_vk_ctx.device, internal->memory, 0, VK_WHOLE_SIZE, 0, &mapped));

    /* Layout transition on a separate command buffer (does not interrupt main cmd_buf render pass) */
    {
        VkCommandBuffer icb = g_vk_ctx.init_cmd_buf;
        VK_CHECK(vkResetCommandBuffer(icb, 0));
        VkCommandBufferBeginInfo bi = {0};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(icb, &bi));
        VkImageMemoryBarrier init_bar = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        init_bar.srcAccessMask = 0;
        init_bar.dstAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        init_bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        init_bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        init_bar.image = internal->image;
        init_bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        init_bar.subresourceRange.levelCount = 1;
        init_bar.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(icb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1, &init_bar);
        VK_CHECK(vkEndCommandBuffer(icb));
        VkSubmitInfo si = {0};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &icb;
        VK_CHECK(vkResetFences(g_vk_ctx.device, 1, &g_vk_ctx.fence));
        VK_CHECK(vkQueueSubmit(g_vk_ctx.queue, 1, &si, g_vk_ctx.fence));
        VK_CHECK(vkWaitForFences(g_vk_ctx.device, 1, &g_vk_ctx.fence, VK_TRUE, UINT64_MAX));
    }

    buffer->handle = internal;
    buffer->memory = (uint8_t *)mapped + layout.offset;
    buffer->address = 0;
    buffer->image_mode = VG_LITE_NORMAL_IMAGE_MODE;  /* Initialize to default */

    internal->mapped_base = mapped;
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_free(vg_lite_buffer_t *buffer)
{
    if (!buffer || !buffer->handle) return VG_LITE_INVALID_ARGUMENT;
    vg_lite_vulkan_flush_render_pass();
    vg_lite_vulkan_begin_command();
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;
    if (internal->msaa_dirty)
        vg_lite_vulkan_resolve_msaa_to_target(internal);
    if (g_vk_ctx.current_fb_internal == internal)
        g_vk_ctx.current_fb_internal = NULL;
    vg_lite_vulkan_submit_command(1);
    if (internal->msaa_color_view) vkDestroyImageView(g_vk_ctx.device, internal->msaa_color_view, NULL);
    if (internal->msaa_color_image) vkDestroyImage(g_vk_ctx.device, internal->msaa_color_image, NULL);
    if (internal->msaa_color_memory) vkFreeMemory(g_vk_ctx.device, internal->msaa_color_memory, NULL);
    if (internal->msaa_depth_view) vkDestroyImageView(g_vk_ctx.device, internal->msaa_depth_view, NULL);
    if (internal->msaa_depth_image) vkDestroyImage(g_vk_ctx.device, internal->msaa_depth_image, NULL);
    if (internal->msaa_depth_memory) vkFreeMemory(g_vk_ctx.device, internal->msaa_depth_memory, NULL);
    if (internal->resolve_view) vkDestroyImageView(g_vk_ctx.device, internal->resolve_view, NULL);
    if (internal->resolve_image) vkDestroyImage(g_vk_ctx.device, internal->resolve_image, NULL);
    if (internal->resolve_memory) vkFreeMemory(g_vk_ctx.device, internal->resolve_memory, NULL);
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
    
    /* VGLite color is 0xAABBGGRR: A at bits 24-31, B at bits 16-23, G at bits 8-15, R at bits 0-7 */
    uint8_t a = (color >> 24) & 0xFF;
    uint8_t b = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8)  & 0xFF;
    uint8_t r = (color)       & 0xFF;
    
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
    } else if (target->format == VG_LITE_RGB565) {
        /* VK_FORMAT_B5G6R5_UNORM_PACK16: standard Vulkan mapping float32[0]=R, [1]=G, [2]=B.
         * No-MSAA path follows Vulkan spec correctly.
         * (MSAA path needs R/B swap due to Intel Iris Xe driver bug �?see blit/draw code.) */
        clear_att.clearValue.color.float32[0] = (float)r / 255.0f;
        clear_att.clearValue.color.float32[1] = (float)g / 255.0f;
        clear_att.clearValue.color.float32[2] = (float)b / 255.0f;
        clear_att.clearValue.color.float32[3] = (float)a / 255.0f;
    } else if (target->format == VG_LITE_RGBA4444) {
        /* VK_FORMAT_R4G4B4A4_UNORM_PACK16: standard Vulkan mapping float32[0]=R, [1]=G, [2]=B, [3]=A.
         * No-MSAA path follows Vulkan spec correctly.
         * (MSAA path needs full channel remap due to Intel Iris Xe driver bug.) */
        clear_att.clearValue.color.float32[0] = (float)r / 255.0f;
        clear_att.clearValue.color.float32[1] = (float)g / 255.0f;
        clear_att.clearValue.color.float32[2] = (float)b / 255.0f;
        clear_att.clearValue.color.float32[3] = (float)a / 255.0f;
    } else if (target->format == VG_LITE_BGRA4444) {
        /* VK_FORMAT_B4G4R4A4_UNORM_PACK16: standard Vulkan mapping float32[0]=B, [1]=G, [2]=R, [3]=A.
         * No-MSAA path follows Vulkan spec correctly.
         * (MSAA path needs full channel remap due to Intel Iris Xe driver bug.) */
        clear_att.clearValue.color.float32[0] = (float)b / 255.0f;
        clear_att.clearValue.color.float32[1] = (float)g / 255.0f;
        clear_att.clearValue.color.float32[2] = (float)r / 255.0f;
        clear_att.clearValue.color.float32[3] = (float)a / 255.0f;
    } else {
        /* VkClearValue channels are format-independent per Vulkan spec:
         * [0]=R value, [1]=G value, [2]=B value, [3]=A value
         * The driver handles format-specific memory layout internally. */
        clear_att.clearValue.color.float32[0] = (float)r / 255.0f;
        clear_att.clearValue.color.float32[1] = (float)g / 255.0f;
        clear_att.clearValue.color.float32[2] = (float)b / 255.0f;
        clear_att.clearValue.color.float32[3] = (float)a / 255.0f;
    }

    vg_lite_vulkan_begin_command();
    /* Reuse the no-MSAA RP if it's already active on the same target.
     * Only flush when switching targets or when a different RP type is active. */
    {
        buffer_internal_t *ci = (buffer_internal_t *)target->handle;
        if (!(g_vk_ctx.current_fb_image == ci->image && g_vk_ctx.current_fb_is_no_msaa))
            vg_lite_vulkan_flush_render_pass();
    }
    vg_lite_vulkan_set_render_target_no_msaa(target);

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
    internal->msaa_needs_seed = 1;  /* no-MSAA RP wrote to target; flag for draw path */
    internal->msaa_dirty = 0;
    return VG_LITE_SUCCESS;
}

static void compute_blit_shader_matrix(vg_lite_matrix_t *matrix,
    int src_w, int src_h, int dst_w, int dst_h,
    float shader_mat[3][3])
{
    vg_lite_matrix_t inv;
    memset(&inv, 0, sizeof(inv));
    mat3_inverse(matrix->m, inv.m);

    float s_inv[3][3] = {{1.0f/src_w, 0, 0}, {0, 1.0f/src_h, 0}, {0, 0, 1}};
    float d_scl[3][3] = {{(float)dst_w, 0, 0}, {0, (float)dst_h, 0}, {0, 0, 1}};
    float temp[3][3];

    mat3_multiply(inv.m, d_scl, temp);
    mat3_multiply(s_inv, temp, shader_mat);
}

/* Compute AABB in NDC of the blit destination region.
 * Transforms source 4 corners through blit matrix to target pixel space,
 * takes bounding box, clips to target bounds, converts to NDC.
 * Output: aabb[4] = {min_x_ndc, min_y_ndc, max_x_ndc, max_y_ndc} */
static void compute_blit_aabb(vg_lite_matrix_t *matrix,
    int src_w, int src_h, int dst_w, int dst_h,
    float aabb[4])
{
    float corners[4][2] = {
        {0, 0}, {(float)src_w, 0}, {(float)src_w, (float)src_h}, {0, (float)src_h}
    };
    float min_x = (float)dst_w, min_y = (float)dst_h, max_x = 0, max_y = 0;

    for (int i = 0; i < 4; i++) {
        float cx = corners[i][0], cy = corners[i][1];
        float tx = matrix->m[0][0] * cx + matrix->m[0][1] * cy + matrix->m[0][2];
        float ty = matrix->m[1][0] * cx + matrix->m[1][1] * cy + matrix->m[1][2];
        if (tx < min_x) min_x = tx;
        if (ty < min_y) min_y = ty;
        if (tx > max_x) max_x = tx;
        if (ty > max_y) max_y = ty;
    }

    /* Clip to target bounds */
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x > dst_w) max_x = (float)dst_w;
    if (max_y > dst_h) max_y = (float)dst_h;

    /* Expand by 1px margin to cover edge pixels that BI_LINEAR filtering
     * needs. Without this, pixels just outside the AABB won't be rasterized,
     * causing visible artifacts at the boundary of scaled blits. The fragment
     * shader clamps/discards out-of-range UVs, so extra coverage is safe. */
    min_x -= 1.0f;
    min_y -= 1.0f;
    max_x += 1.0f;
    max_y += 1.0f;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x > dst_w) max_x = (float)dst_w;
    if (max_y > dst_h) max_y = (float)dst_h;

    /* Convert to NDC: [0,dst] -> [-1,1] */
    aabb[0] = 2.0f * min_x / dst_w - 1.0f;
    aabb[1] = 2.0f * min_y / dst_h - 1.0f;
    aabb[2] = 2.0f * max_x / dst_w - 1.0f;
    aabb[3] = 2.0f * max_y / dst_h - 1.0f;
}

static vg_lite_error_t create_temp_copy_image(VkFormat vkfmt,
    vg_lite_buffer_t *target, buffer_internal_t *target_int,
    VkImage *out_image, VkDeviceMemory *out_memory, VkImageView *out_view)
{
    VkImage tmp_image = VK_NULL_HANDLE;
    VkDeviceMemory tmp_memory = VK_NULL_HANDLE;
    VkImageView tmp_view = VK_NULL_HANDLE;

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
    VK_CHECK(vkBindImageMemory(g_vk_ctx.device, tmp_image, tmp_memory, 0));

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

    if (target_int->msaa_dirty)
        vg_lite_vulkan_resolve_msaa_to_target(target_int);

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

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = tmp_image;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

    *out_image = tmp_image;
    *out_memory = tmp_memory;
    *out_view = tmp_view;
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
    if (blend_group != BG_SHADER && target->format != VG_LITE_BGRA8888 && target->format != VG_LITE_BGR565
        && target->format != VG_LITE_RGBA8888 && target->format != VG_LITE_RGB565
        && target->format != VG_LITE_A8 && target->format != VG_LITE_L8)
        blend_group = BG_SHADER;
    int native_blend = (blend_group != BG_SHADER);

    VkPipeline pipeline;
    if (native_blend)
#if VGLITE_BLIT_MSAA
        pipeline = vg_lite_vulkan_get_pipeline_native_msaa(vkfmt, blend_group);
#else
        pipeline = vg_lite_vulkan_get_pipeline_no_msaa(vkfmt, blend_group);
#endif
    else
        pipeline = vg_lite_vulkan_get_pipeline(vkfmt, blend_group);
    if (!pipeline) return VG_LITE_OUT_OF_MEMORY;
    VkPipeline aabb_pipeline = VK_NULL_HANDLE;
    if (native_blend && g_vk_ctx.use_aabb_blit) {
#if VGLITE_BLIT_MSAA
        aabb_pipeline = vg_lite_vulkan_get_pipeline_aabb_native_msaa(vkfmt, blend_group);
#else
        aabb_pipeline = vg_lite_vulkan_get_pipeline_aabb_no_msaa(vkfmt, blend_group);
#endif
        if (!aabb_pipeline) return VG_LITE_OUT_OF_MEMORY;
    }
    vg_lite_vulkan_begin_command();
    if (!native_blend) {
        vg_lite_vulkan_flush_render_pass();
        vg_lite_vulkan_end_render_pass();
    }

    VkImage tmp_image = VK_NULL_HANDLE;
    VkDeviceMemory tmp_memory = VK_NULL_HANDLE;
    VkImageView tmp_view = VK_NULL_HANDLE;

    if (!native_blend) {
        vg_lite_error_t err = create_temp_copy_image(vkfmt, target, target_int, &tmp_image, &tmp_memory, &tmp_view);
        if (err != VG_LITE_SUCCESS) return err;
    }

    VkSampler sampler = get_or_create_sampler(filter);

    /* Only flush + resolve when the source or target has pending MSAA
     * writes that haven't been copied to the LINEAR image yet.
     * For consecutive native-blend blits to the same target with simple
     * CPU-filled sources (msaa_dirty=0), this is a no-op �?RP stays open
     * and set_render_target's reuse path fires (L500). */
    int need_flush = (src_int->msaa_dirty || target_int->msaa_dirty);
    if (need_flush) {
        vg_lite_vulkan_flush_render_pass();
        if (src_int->msaa_dirty)
            vg_lite_vulkan_resolve_msaa_to_target(src_int);
        if (target_int->msaa_dirty)
            vg_lite_vulkan_resolve_msaa_to_target(target_int);
    }

    /* Source image barrier: ensures CPU writes to the source are visible
     * to the fragment shader. Runs outside any render pass (after flush
     * if dirty, or when no RP is active). On RP reuse (consecutive blits,
     * current_fb != NULL, no dirty source), the prior vkQueueSubmit's
     * implicit host barrier already guarantees visibility �?skip it. */
    if (need_flush || !g_vk_ctx.current_fb) {
        VkImageMemoryBarrier src_bar = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        src_bar.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        src_bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        src_bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        src_bar.image = src_int->image;
        src_bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        src_bar.subresourceRange.levelCount = 1;
        src_bar.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, NULL, 0, NULL, 1, &src_bar);
    }

#if VGLITE_BLIT_MSAA
    VkFramebuffer prev_fb = g_vk_ctx.current_fb;
    vg_lite_vulkan_set_render_target(target);
    if (g_vk_ctx.current_fb != prev_fb) {
        vg_lite_vulkan_seed_msaa(target, sampler);
    }
#else
    vg_lite_vulkan_set_render_target_no_msaa(target);
#endif

    float shader_mat[3][3];
    compute_blit_shader_matrix(matrix, source->width, source->height, target->width, target->height, shader_mat);

    VkDescriptorSetAllocateInfo ds_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ds_alloc.descriptorPool = g_vk_ctx.descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = native_blend ? &g_vk_ctx.native_descriptor_layout : &g_vk_ctx.blit_descriptor_layout;
    VkDescriptorSet desc_set;
    if (vkAllocateDescriptorSets(g_vk_ctx.device, &ds_alloc, &desc_set) != VK_SUCCESS)
        return VG_LITE_OUT_OF_MEMORY;

    VkImageView src_view = src_int->swizzle_view ? src_int->swizzle_view : src_int->view;
    VkDescriptorImageInfo si = {sampler, src_view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo di;
    if (native_blend) {
        di = si;
    } else {
        di = (VkDescriptorImageInfo){sampler, tmp_view, VK_IMAGE_LAYOUT_GENERAL};
    }
    
    struct { float m[12]; int blend; unsigned color; int im_mode; int filt; int flags; int pad[3]; } pc = {0};
    for (int col = 0; col < 3; col++) {
        for (int row = 0; row < 3; row++) {
            pc.m[col * 4 + row] = shader_mat[row][col];
        }
    }
    pc.blend = (int)blend; pc.color = color;
    pc.im_mode = (int)source->image_mode; pc.filt = (int)filter;
    pc.flags = 0;
    if (target->format == VG_LITE_L8)  pc.flags |= 1;
    if (target->format == VG_LITE_A8)  pc.flags |= 2;
    if (source->format == VG_LITE_A8)  pc.flags |= 8;
    if (source->format == VG_LITE_INDEX_8) pc.flags |= 16;
    
    if (native_blend) {
        /* Push fragment data at offset 0 (80B), AABB at offset 80 (16B) */
        vkCmdPushConstants(g_vk_ctx.cmd_buf, g_vk_ctx.native_pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        float blit_aabb[4] = {-1.0f, -1.0f, 3.0f, 3.0f};
        if (g_vk_ctx.use_aabb_blit) {
            compute_blit_aabb(matrix, source->width, source->height,
                              target->width, target->height, blit_aabb);
        }
        vkCmdPushConstants(g_vk_ctx.cmd_buf, g_vk_ctx.native_pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 80, 16, blit_aabb);
    } else {
        memcpy(g_vk_ctx.blit_ssbo_mapped, &pc, sizeof(pc));
    }
    
    VkDescriptorBufferInfo ssbo_info = {g_vk_ctx.blit_ssbo_buffer, 0, sizeof(pc)};
    VkDescriptorBufferInfo clut_info = {g_vk_ctx.clut_buffer, 0, 256 * 4};

    if (native_blend) {
        VkWriteDescriptorSet ws[1] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &si, NULL, NULL},
        };
        vkUpdateDescriptorSets(g_vk_ctx.device, 1, ws, 0, NULL);
    } else {
        VkWriteDescriptorSet ws[4] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &si, NULL, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &di, NULL, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &ssbo_info, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, desc_set, 3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &clut_info, NULL},
        };
        vkUpdateDescriptorSets(g_vk_ctx.device, 4, ws, 0, NULL);
    }

    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
        (native_blend && g_vk_ctx.use_aabb_blit) ? aabb_pipeline : pipeline);
    vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
        native_blend ? g_vk_ctx.native_pipeline_layout : g_vk_ctx.blit_pipeline_layout,
        0, 1, &desc_set, 0, NULL);

    VkViewport vp = {0, 0, (float)target->width, (float)target->height, 0, 1};
    vkCmdSetViewport(g_vk_ctx.cmd_buf, 0, 1, &vp);
    vg_lite_vulkan_apply_scissor(target->width, target->height);
#if VGLITE_BLIT_PERF
    uint32_t perf_slot0 = g_vk_ctx.timestamp_slot_counter;
    if (perf_slot0 + 1 < VGLITE_TIMESTAMP_QUERY_COUNT)
        vkCmdWriteTimestamp(g_vk_ctx.cmd_buf, BOTTOM_OF_PIPE_BIT,
            g_vk_ctx.timestamp_query_pool, perf_slot0);
#endif
    vkCmdDraw(g_vk_ctx.cmd_buf, 3, 1, 0, 0);
#if VGLITE_BLIT_PERF
    if (perf_slot0 + 1 < VGLITE_TIMESTAMP_QUERY_COUNT) {
        vkCmdWriteTimestamp(g_vk_ctx.cmd_buf, BOTTOM_OF_PIPE_BIT,
            g_vk_ctx.timestamp_query_pool, perf_slot0 + 1);
        g_vk_ctx.timestamp_slot_counter += 2;
        g_vk_ctx.blit_perf_count++;
    }
#endif

    if (!native_blend) {
        /* Shader blend: immediate flush (temp copy lifecycle) */
        vg_lite_vulkan_end_render_pass();
        vg_lite_vulkan_submit_command(1);
        VK_CHECK(vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &desc_set));
        vkDestroyImageView(g_vk_ctx.device, tmp_view, NULL);
        vkDestroyImage(g_vk_ctx.device, tmp_image, NULL);
        vkFreeMemory(g_vk_ctx.device, tmp_memory, NULL);
    } else {
        /* Native blend: defer �?RP stays open, desc set freed after submit */
        if (g_vk_ctx.pending_desc_count < MAX_PENDING_DESC) {
            g_vk_ctx.pending_desc_sets[g_vk_ctx.pending_desc_count++] = desc_set;
        } else {
            /* Overflow safety: flush everything, then track current desc set */
            vg_lite_vulkan_flush_render_pass();
            vg_lite_vulkan_submit_command(1);
            for (int i = 0; i < g_vk_ctx.pending_desc_count; i++)
                VK_CHECK(vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &g_vk_ctx.pending_desc_sets[i]));
            g_vk_ctx.pending_desc_count = 0;
            g_vk_ctx.pending_desc_sets[g_vk_ctx.pending_desc_count++] = desc_set;
        }
    }
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_finish(void)
{
#if VGLITE_BLIT_PERF
    /* Always record CPU start before submit �?cheap, used if GPU timestamps fail */
    if (g_vk_ctx.blit_perf_count > 0)
        blit_perf_cpu_record_start();
#endif

    vg_lite_vulkan_end_render_pass();
    if (g_vk_ctx.current_fb_internal && g_vk_ctx.current_fb_internal->msaa_dirty)
        vg_lite_vulkan_resolve_msaa_to_target(g_vk_ctx.current_fb_internal);
    vg_lite_vulkan_submit_command(1);
    vg_lite_draw_cleanup_pending_buffers();

#if VGLITE_BLIT_PERF
    if (g_vk_ctx.blit_perf_count > 0) {
        uint64_t batch_ns = 0;

        if (!blit_perf_gpu_checked && g_vk_ctx.timestamp_query_pool) {
            /* First time: try reading one GPU timestamp to check support */
            vg_lite_vulkan_read_timestamp(0);
            blit_perf_gpu_checked = 1;
            if (g_vk_ctx.timestamp_query_failed) {
                blit_perf_use_cpu_timer = 1;
                fprintf(stderr, "[BLIT_PERF] GPU timestamps unavailable, falling back to CPU timer\n");
            }
        }

        if (blit_perf_use_cpu_timer || g_vk_ctx.timestamp_query_failed) {
            /* CPU timer: measure total wall-clock time for the batch */
            batch_ns = blit_perf_cpu_read_ns();
            g_vk_ctx.blit_perf_total_ns += batch_ns;
            printf("[BLIT_PERF] batch blits=%u  cpu_ns=%llu  avg_ns/blit=%.0f  total_ns=%llu\n",
                   g_vk_ctx.blit_perf_count,
                   (unsigned long long)batch_ns,
                   g_vk_ctx.blit_perf_count ? (double)batch_ns / g_vk_ctx.blit_perf_count : 0.0,
                   (unsigned long long)g_vk_ctx.blit_perf_total_ns);
        } else if (g_vk_ctx.timestamp_query_pool) {
            /* GPU timer: read back timestamps */
            for (uint32_t i = 0; i < g_vk_ctx.blit_perf_count; i++) {
                uint32_t s0 = i * 2, s1 = i * 2 + 1;
                uint64_t t0 = vg_lite_vulkan_read_timestamp(s0);
                uint64_t t1 = vg_lite_vulkan_read_timestamp(s1);
                if (t1 > t0)
                    batch_ns += (t1 - t0);
            }
            g_vk_ctx.blit_perf_total_ns += batch_ns;
            printf("[BLIT_PERF] batch blits=%u  gpu_ns=%llu  avg_ns/blit=%.0f  total_ns=%llu\n",
                   g_vk_ctx.blit_perf_count,
                   (unsigned long long)batch_ns,
                   g_vk_ctx.blit_perf_count ? (double)batch_ns / g_vk_ctx.blit_perf_count : 0.0,
                   (unsigned long long)g_vk_ctx.blit_perf_total_ns);
        }

        g_vk_ctx.blit_perf_count = 0;
        g_vk_ctx.timestamp_slot_counter = 0;
    }
#endif
    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_flush(void)
{
    vg_lite_vulkan_end_render_pass();
    if (g_vk_ctx.current_fb_internal && g_vk_ctx.current_fb_internal->msaa_dirty)
        vg_lite_vulkan_resolve_msaa_to_target(g_vk_ctx.current_fb_internal);
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
    mat3_multiply(matrix->m, t.m, result.m);
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
    mat3_multiply(matrix->m, t.m, result.m);
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
    mat3_multiply(matrix->m, t.m, result.m);
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
    printf("[DRAW] target=%dx%d fmt=%d  path={fmt=%d qual=%d bb=[%g,%g,%g,%g] len=%d  data=",
           t ? t->width : 0, t ? t->height : 0, t ? (int)t->format : -1,
           p ? (int)p->format : -1, p ? (int)p->quality : -1,
           p ? p->bounding_box[0] : 0, p ? p->bounding_box[1] : 0,
           p ? p->bounding_box[2] : 0, p ? p->bounding_box[3] : 0,
           p ? (int)p->path_length : 0);

    /* Dump path data as raw bytes (opcode + coordinates) */
    if (p && p->path && p->path_length > 0) {
        printf("[");
        for (vg_lite_uint32_t i = 0; i < p->path_length; i++) {
            unsigned char byte = ((unsigned char*)p->path)[i];
            if (i > 0) printf(" ");
            /* Print opcodes (0-4) as decimal, others as signed value */
            printf("%d", (signed char)byte);
        }
        printf("]");
    } else {
        printf("(null)");
    }

    printf("}  fill=%d  blend=%d  color=0x%08X  "
           "mat=[%g %g %g; %g %g %g; %g %g %g]\n",
           (int)fl, (int)b, c,
           m ? m->m[0][0] : 0, m ? m->m[0][1] : 0, m ? m->m[0][2] : 0,
           m ? m->m[1][0] : 0, m ? m->m[1][1] : 0, m ? m->m[1][2] : 0,
           m ? m->m[2][0] : 0, m ? m->m[2][1] : 0, m ? m->m[2][2] : 0);
    return vg_lite_draw_impl(t, p, fl, m, b, c);
}

vg_lite_error_t vg_lite_set_color_key(vg_lite_color_key4_t ck) { (void)ck; return VG_LITE_NOT_SUPPORT; }

vg_lite_error_t vg_lite_clear_path(vg_lite_path_t *path) { (void)path; return VG_LITE_SUCCESS; }

vg_lite_error_t vg_lite_init_path(vg_lite_path_t *path,
                                  vg_lite_format_t format,
                                  vg_lite_quality_t quality,
                                  vg_lite_uint32_t length,
                                  vg_lite_pointer data,
                                  vg_lite_float_t min_x,
                                  vg_lite_float_t min_y,
                                  vg_lite_float_t max_x,
                                  vg_lite_float_t max_y)
{
    if (path == NULL || data == NULL)
        return VG_LITE_INVALID_ARGUMENT;

    memset(path, 0, sizeof(vg_lite_path_t));
    path->bounding_box[0] = min_x;
    path->bounding_box[1] = min_y;
    path->bounding_box[2] = max_x;
    path->bounding_box[3] = max_y;
    path->quality     = quality;
    path->format      = format;
    path->path_length = length;
    path->path        = data;
    path->path_changed = 1;

    return VG_LITE_SUCCESS;
}
vg_lite_error_t vg_lite_enable_dither(void) { return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_disable_dither(void) { return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_source_global_alpha(vg_lite_global_alpha_t m, vg_lite_uint8_t v) { (void)m;(void)v; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_dest_global_alpha(vg_lite_global_alpha_t m, vg_lite_uint8_t v) { (void)m;(void)v; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_set_CLUT(vg_lite_uint32_t c, vg_lite_uint32_t *cl)
{
    extern vk_context_t g_vk_ctx;
    
    if (c != 256) return VG_LITE_INVALID_ARGUMENT;
    if (!g_vk_ctx.clut_mapped) return VG_LITE_NO_CONTEXT;
    if (!cl) return VG_LITE_INVALID_ARGUMENT;
    
    uint32_t *dst = (uint32_t *)g_vk_ctx.clut_mapped;
    memcpy(dst, cl, 256 * 4);
    
    return VG_LITE_SUCCESS;
}
vg_lite_error_t vg_lite_gaussian_filter(vg_lite_float_t w0, vg_lite_float_t w1, vg_lite_float_t w2) { (void)w0;(void)w1;(void)w2; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_upload_buffer(vg_lite_buffer_t *b, vg_lite_uint8_t *d[3], vg_lite_uint32_t s[3]) { (void)b;(void)d;(void)s; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_map(vg_lite_buffer_t *b, vg_lite_map_flag_t f, int32_t fd) { (void)b;(void)f;(void)fd; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_unmap(vg_lite_buffer_t *b) { (void)b; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_flush_mapped_buffer(vg_lite_buffer_t *b) { (void)b; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_dump_png(const char *fn, vg_lite_buffer_t *b) { (void)fn;(void)b; return VG_LITE_NOT_SUPPORT; }
vg_lite_error_t vg_lite_wrap_user_memory(vg_lite_buffer_t *b, vg_lite_uint8_t *m, vg_lite_uint32_t s, vg_lite_uint32_t w, vg_lite_uint32_t h, vg_lite_buffer_format_t f) { (void)b;(void)m;(void)s;(void)w;(void)h;(void)f; return VG_LITE_NOT_SUPPORT; }

/* --- Blit AABB optimization and GPU timestamp public API --- */

vg_lite_error_t vg_lite_set_blit_aabb_mode(vg_lite_uint32_t mode) {
    g_vk_ctx.use_aabb_blit = (mode != 0) ? 1 : 0;
    return VG_LITE_SUCCESS;
}

vg_lite_uint32_t vg_lite_get_blit_aabb_mode(void) {
    return g_vk_ctx.use_aabb_blit;
}

vg_lite_uint32_t vg_lite_write_timestamp(vg_lite_uint32_t stage) {
    /* Map vg_lite_uint32_t to VkPipelineStageFlagBits */
    vg_lite_vulkan_write_timestamp((VkPipelineStageFlagBits)stage);
    return g_vk_ctx.timestamp_slot_counter - 1;
}

vg_lite_uint64_t vg_lite_read_timestamp(vg_lite_uint32_t slot) {
    return vg_lite_vulkan_read_timestamp(slot);
}

double vg_lite_get_elapsed_ns(vg_lite_uint32_t start_slot, vg_lite_uint32_t end_slot) {
    return vg_lite_vulkan_get_elapsed_ns(start_slot, end_slot);
}
