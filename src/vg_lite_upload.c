/* vg_lite_upload.c ??vg_lite_upload_buffer implementation.
 *
 * Uploads user pixel data (data[3]/stride[3], plane 0 only) into an
 * allocated vg_lite_buffer. Two paths:
 *
 * TILED buffers (VG_LITE_TILED, OPTIMAL + STORAGE usage):
 *   GPU compute path (shaders/upload_tiled.comp): texelFetch reads each
 *   pixel's raw bytes from a *_UINT texel-buffer view (R32/R16/R8_UINT by
 *   bytes-per-pixel; the fetch unit does the byte extraction) and
 *   imageStore writes them through a FORMATLESS uimage2D storage view
 *   (requires shaderStorageImageWriteWithoutFormat). Packed 16bpp images
 *   are created as R16_UINT by vg_lite_allocate (see vg_lite.c), so the
 *   storage view format always matches the image's own format. The
 *   hardware resolves tile addressing. Any runtime NOT_SUPPORT degrades
 *   to the generic path.
 *
 * Everything else (LINEAR images, OPTIMAL without STORAGE, shadow
 *   formats): upload_buffer_staging() dispatches by buffer shape —
 *   mapped LINEAR non-shadow images get a direct per-row memcpy into
 *   buffer->memory; all other cases repack the user rows into the
 *   buffer->stride layout and delegate to vg_lite_buffer_write() (A4 /
 *   OPENVG_sRGBA_8888 shadow sync, cpu_cache invalidation, and staging +
 *   vkCmdCopyBufferToImage for OPTIMAL images via upload_staging).
 *
 * Unsupported: multi-plane YUV formats (data[1]/data[2]) on all paths.
 */

#include "vg_lite.h"
#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include "shader_loader.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Pipeline lazy init                                                  */
/* ------------------------------------------------------------------ */

static VkResult get_upload_tiled_pipeline(VkPipeline *pipeline,
                                           VkPipelineLayout *layout,
                                           VkDescriptorSetLayout *desc_layout)
{
    if (*pipeline != VK_NULL_HANDLE) {
        *layout = g_vk_ctx.upload_tiled_pipeline_layout;
        *desc_layout = g_vk_ctx.upload_tiled_descriptor_layout;
        return VK_SUCCESS;
    }

    VkDescriptorSetLayoutBinding bindings[2] = {{0}, {0}};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dl_ci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl_ci.bindingCount = 2;
    dl_ci.pBindings = bindings;
    VkResult r = vkCreateDescriptorSetLayout(g_vk_ctx.device, &dl_ci, NULL,
                                             &g_vk_ctx.upload_tiled_descriptor_layout);
    if (r != VK_SUCCESS) return r;

    VkPushConstantRange push = {0};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.offset = 0;
    push.size = 8; /* width, height */

    VkPipelineLayoutCreateInfo pl_ci = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &g_vk_ctx.upload_tiled_descriptor_layout;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &push;
    r = vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL,
                               &g_vk_ctx.upload_tiled_pipeline_layout);
    if (r != VK_SUCCESS) return r;

    VkShaderModule comp = load_shader_module(g_vk_ctx.device, "upload_tiled_comp");
    if (comp == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;

    VkComputePipelineCreateInfo cp_ci = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp_ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp_ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp_ci.stage.module = comp;
    cp_ci.stage.pName = "main";
    cp_ci.layout = g_vk_ctx.upload_tiled_pipeline_layout;
    r = vkCreateComputePipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &cp_ci,
                                 NULL, &g_vk_ctx.upload_tiled_pipeline);
    vkDestroyShaderModule(g_vk_ctx.device, comp, NULL);
    if (r != VK_SUCCESS) return r;

    *pipeline = g_vk_ctx.upload_tiled_pipeline;
    *layout = g_vk_ctx.upload_tiled_pipeline_layout;
    *desc_layout = g_vk_ctx.upload_tiled_descriptor_layout;
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Staging SSBO helper (HOST_VISIBLE | HOST_COHERENT, mapped)          */
/* ------------------------------------------------------------------ */

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
} staging_t;

static int staging_create(VkDeviceSize size, VkBufferUsageFlags usage, staging_t *st)
{
    memset(st, 0, sizeof(*st));
    VkBufferCreateInfo b_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    b_ci.size = size;
    b_ci.usage = usage;
    b_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_vk_ctx.device, &b_ci, NULL, &st->buffer) != VK_SUCCESS)
        return 0;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, st->buffer, &req);
    int32_t mem_type = find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type < 0) {
        vkDestroyBuffer(g_vk_ctx.device, st->buffer, NULL);
        st->buffer = VK_NULL_HANDLE;
        return 0;
    }
    VkMemoryAllocateInfo a_ci = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a_ci.allocationSize = req.size;
    a_ci.memoryTypeIndex = (uint32_t)mem_type;
    if (vkAllocateMemory(g_vk_ctx.device, &a_ci, NULL, &st->memory) != VK_SUCCESS ||
        vkMapMemory(g_vk_ctx.device, st->memory, 0, VK_WHOLE_SIZE, 0, &st->mapped) != VK_SUCCESS) {
        if (st->memory) vkFreeMemory(g_vk_ctx.device, st->memory, NULL);
        vkDestroyBuffer(g_vk_ctx.device, st->buffer, NULL);
        st->memory = VK_NULL_HANDLE;
        st->buffer = VK_NULL_HANDLE;
        st->mapped = NULL;
        return 0;
    }
    vkBindBufferMemory(g_vk_ctx.device, st->buffer, st->memory, 0);
    return 1;
}

static void staging_destroy(staging_t *st)
{
    if (st->mapped) vkUnmapMemory(g_vk_ctx.device, st->memory);
    if (st->memory) vkFreeMemory(g_vk_ctx.device, st->memory, NULL);
    if (st->buffer) vkDestroyBuffer(g_vk_ctx.device, st->buffer, NULL);
    memset(st, 0, sizeof(*st));
}

/* Allocate one descriptor set from the global pool. */
static VkDescriptorSet alloc_desc_set(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = g_vk_ctx.descriptor_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet ds = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(g_vk_ctx.device, &ai, &ds) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return ds;
}

/* ------------------------------------------------------------------ */
/* Generic non-compute path: repack user rows and delegate to          */
/* vg_lite_buffer_write (shadow sync / cpu_cache invalidate /          */
/* upload_staging CopyBufferToImage), or direct memcpy for mapped      */
/* LINEAR images.                                                      */
/* ------------------------------------------------------------------ */

static vg_lite_error_t upload_buffer_staging(vg_lite_buffer_t *buffer,
                                             const uint8_t *data,
                                             uint32_t data_stride,
                                             uint32_t row_bytes)
{
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;
    uint32_t h = (uint32_t)buffer->height;

    /* Mapped LINEAR image (non-shadow): rows land directly in
     * buffer->memory; no staging buffer, no command submission. */
    if (!internal->is_optimal &&
        buffer->format != VG_LITE_A4 &&
        buffer->format != OPENVG_sRGBA_8888) {
        if (!buffer->memory)
            return VG_LITE_OUT_OF_MEMORY;
        for (uint32_t y = 0; y < h; y++)
            memcpy((uint8_t *)buffer->memory + (size_t)y * buffer->stride,
                   data + (size_t)y * data_stride, row_bytes);
        vg_lite_buffer_flush(buffer);
        return VG_LITE_SUCCESS;
    }

    /* Everything else (OPTIMAL images, shadow-transform formats): repack
     * the user rows into the buffer->stride layout that
     * vg_lite_buffer_write expects, then let it do the heavy lifting
     * (A4/sRGBA shadow sync, cpu_cache invalidation, staging +
     * CopyBufferToImage for OPTIMAL, direct write for LINEAR shadows). */
    size_t total = (size_t)buffer->stride * h;
    uint8_t *packed = (uint8_t *)malloc(total);
    if (!packed)
        return VG_LITE_OUT_OF_MEMORY;
    memset(packed, 0, total);
    for (uint32_t y = 0; y < h; y++)
        memcpy(packed + (size_t)y * buffer->stride,
               data + (size_t)y * data_stride, row_bytes);
    vg_lite_error_t err = vg_lite_buffer_write(buffer, packed);
    free(packed);
    return err;
}

/* ------------------------------------------------------------------ */
/* TILED path: staging SSBO -> formatless uimage2D                    */
/* ------------------------------------------------------------------ */

static vg_lite_error_t upload_buffer_tiled(vg_lite_buffer_t *buffer,
                                           const uint8_t *data,
                                           uint32_t data_stride,
                                           uint32_t bytes_pp)
{
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;
    if (!g_vk_ctx.storage_image_wo_format)
        return VG_LITE_NOT_SUPPORT;

    /* Storage view format by bytes-per-pixel (image itself is created in
     * this format for 16bpp packed ??see vg_lite_allocate). */
    VkFormat view_fmt;
    switch (bytes_pp) {
    case 4: view_fmt = VK_FORMAT_R32_UINT; break;
    case 2: view_fmt = VK_FORMAT_R16_UINT; break;
    case 1: view_fmt = VK_FORMAT_R8_UINT;  break;
    default: return VG_LITE_NOT_SUPPORT;
    }

    /* Verify storage-image support for the chosen format on this image. */
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_vk_ctx.physical_device, view_fmt, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT))
        return VG_LITE_NOT_SUPPORT;

    VkPipeline pipeline = g_vk_ctx.upload_tiled_pipeline;
    VkPipelineLayout pipe_layout = g_vk_ctx.upload_tiled_pipeline_layout;
    VkDescriptorSetLayout desc_layout = g_vk_ctx.upload_tiled_descriptor_layout;
    if (get_upload_tiled_pipeline(&pipeline, &pipe_layout, &desc_layout) != VK_SUCCESS)
        return VG_LITE_NOT_SUPPORT;

    /* texelFetch reads one texel per pixel through a *_UINT buffer view:
     * the fetch unit extracts the pixel bytes, no shader ALU needed. */
    uint32_t row_bytes = (uint32_t)buffer->width * bytes_pp;
    VkDeviceSize staging_size = (VkDeviceSize)row_bytes * buffer->height;

    VkPhysicalDeviceProperties pdp;
    vkGetPhysicalDeviceProperties(g_vk_ctx.physical_device, &pdp);
    if (staging_size / bytes_pp > pdp.limits.maxTexelBufferElements)
        return VG_LITE_NOT_SUPPORT;

    staging_t staging;
    if (!staging_create(staging_size, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
                        &staging))
        return VG_LITE_OUT_OF_MEMORY;
    /* Rows are tightly packed: texel index = y*width + x, one texel per
     * pixel, no row padding needed. */
    for (int32_t y = 0; y < buffer->height; y++)
        memcpy((uint8_t *)staging.mapped + (size_t)y * row_bytes,
               data + (size_t)y * data_stride, row_bytes);

    /* Texel-buffer view on the staging data (format picks the texel size). */
    VkBufferViewCreateInfo bv_ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    bv_ci.buffer = staging.buffer;
    bv_ci.format = view_fmt;
    bv_ci.offset = 0;
    bv_ci.range = staging_size;
    VkBufferView src_view = VK_NULL_HANDLE;
    if (vkCreateBufferView(g_vk_ctx.device, &bv_ci, NULL, &src_view) != VK_SUCCESS) {
        staging_destroy(&staging);
        return VG_LITE_OUT_OF_MEMORY;
    }

    /* Transient storage view on the destination image (GENERAL layout). */
    VkImageViewCreateInfo v_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v_ci.image = internal->image;
    v_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v_ci.format = view_fmt;
    v_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    v_ci.subresourceRange.levelCount = 1;
    v_ci.subresourceRange.layerCount = 1;
    VkImageView dst_view = VK_NULL_HANDLE;
    if (vkCreateImageView(g_vk_ctx.device, &v_ci, NULL, &dst_view) != VK_SUCCESS) {
        staging_destroy(&staging);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkDescriptorSet ds = alloc_desc_set(desc_layout);
    if (ds == VK_NULL_HANDLE) {
        vkDestroyImageView(g_vk_ctx.device, dst_view, NULL);
        staging_destroy(&staging);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkDescriptorImageInfo ii = {0};
    ii.sampler = VK_NULL_HANDLE;
    ii.imageView = dst_view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2] = {{0}, {0}};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    writes[0].pTexelBufferView = &src_view;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &ii;
    vkUpdateDescriptorSets(g_vk_ctx.device, 2, writes, 0, NULL);
    vg_lite_vulkan_flush_render_pass();
    if (vg_lite_vulkan_submit_command(1) != VG_LITE_SUCCESS ||
        vg_lite_vulkan_begin_command() != VG_LITE_SUCCESS)
        goto tiled_fail;

    {
        VkBufferMemoryBarrier buf_pre = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        buf_pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        buf_pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        buf_pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buf_pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        buf_pre.buffer = staging.buffer;
        buf_pre.offset = 0;
        buf_pre.size = VK_WHOLE_SIZE;

        VkImageMemoryBarrier img_pre = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        img_pre.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        img_pre.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        img_pre.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_pre.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        img_pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_pre.image = internal->image;
        img_pre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        img_pre.subresourceRange.levelCount = 1;
        img_pre.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, NULL, 1, &buf_pre, 1, &img_pre);

        struct { uint32_t width, height; } push;
        push.width = (uint32_t)buffer->width;
        push.height = (uint32_t)buffer->height;

        vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipe_layout, 0, 1, &ds, 0, NULL);
        vkCmdPushConstants(g_vk_ctx.cmd_buf, pipe_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, &push);
        vkCmdDispatch(g_vk_ctx.cmd_buf,
                      ((uint32_t)buffer->width + 7) / 8,
                      ((uint32_t)buffer->height + 7) / 8, 1);

        VkImageMemoryBarrier img_post = img_pre;
        img_post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        img_post.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, NULL, 0, NULL, 1, &img_post);
    }

    vg_lite_vulkan_submit_command(1);
    vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &ds);
    vkDestroyBufferView(g_vk_ctx.device, src_view, NULL);
    vkDestroyImageView(g_vk_ctx.device, dst_view, NULL);
    staging_destroy(&staging);
    return VG_LITE_SUCCESS;

tiled_fail:
    if (ds != VK_NULL_HANDLE)
        vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &ds);
    vkDestroyBufferView(g_vk_ctx.device, src_view, NULL);
    vkDestroyImageView(g_vk_ctx.device, dst_view, NULL);
    staging_destroy(&staging);
    return VG_LITE_OUT_OF_MEMORY;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

vg_lite_error_t vg_lite_upload_buffer(vg_lite_buffer_t *buffer,
                                      vg_lite_uint8_t *data[3],
                                      vg_lite_uint32_t stride[3])
{
    if (!buffer || !data || !data[0] || !stride)
        return VG_LITE_INVALID_ARGUMENT;
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;
    if (!internal || !buffer->width || !buffer->height)
        return VG_LITE_INVALID_ARGUMENT;

    /* Multi-plane YUV is not supported on any path. */
    if (data[1] || data[2])
        return VG_LITE_NOT_SUPPORT;
    if (vg_lite_is_yuv_format(buffer->format))
        return VG_LITE_NOT_SUPPORT;

    const uint8_t *pdata = data[0];
    uint32_t bpp_bits = vg_lite_format_bpp(buffer->format);
    uint32_t row_bytes = ((uint32_t)buffer->width * bpp_bits + 7) / 8;
    uint32_t data_stride = stride[0] ? stride[0] : row_bytes;
    if (data_stride < row_bytes)
        return VG_LITE_INVALID_ARGUMENT;

    /* Route by the buffer's ACTUAL shape (decided once at allocate time):
     *  - is_optimal + has_storage -> compute-shader imageStore, degrading
     *    to the generic path on any runtime NOT_SUPPORT
     *  - everything else -> upload_buffer_staging (direct memcpy for
     *    mapped LINEAR images; repack + vg_lite_buffer_write otherwise,
     *    which covers shadow formats, cpu_cache invalidation and
     *    staging + CopyBufferToImage for OPTIMAL images). */
    if (internal->is_optimal && internal->has_storage) {
        vg_lite_error_t err = VG_LITE_NOT_SUPPORT;
        if (bpp_bits == 32) err = upload_buffer_tiled(buffer, pdata, data_stride, 4);
        else if (bpp_bits == 16) err = upload_buffer_tiled(buffer, pdata, data_stride, 2);
        else if (bpp_bits == 8)  err = upload_buffer_tiled(buffer, pdata, data_stride, 1);
        if (err == VG_LITE_SUCCESS || err == VG_LITE_OUT_OF_MEMORY)
            return err;
        /* compute upload unsupported here -> degrade to the copy engine */
    }
    return upload_buffer_staging(buffer, pdata, data_stride, row_bytes);
}
