/* vg_lite_upload.c ??vg_lite_upload_buffer implementation.
 *
 * Uploads user pixel data (data[3]/stride[3], plane 0 only) into an
 * allocated vg_lite_buffer. Two paths:
 *
 * LINEAR buffers (VG_LITE_LINEAR):
 *   GPU compute path (shaders/upload.comp): user rows are packed into a
 *   HOST_VISIBLE staging SSBO, and the destination image's device memory
 *   is aliased as a STORAGE_BUFFER (vkBindBufferMemory on the image's
 *   VkDeviceMemory). A 64-thread/row compute shader scatters u32s using
 *   the subresource offset/rowPitch passed as push constants. Falls back
 *   to a per-row CPU memcpy when the layout is not 4-byte aligned or the
 *   pipeline/staging allocation fails.
 *
 * TILED buffers (VG_LITE_TILED, OPTIMAL tiling):
 *   GPU compute path (shaders/upload_tiled.comp): single unified shader
 *   writing RAW pixel bit patterns through a FORMATLESS uimage2D storage
 *   view (R32/R16/R8_UINT selected by bytes-per-pixel; requires
 *   shaderStorageImageWriteWithoutFormat). Packed 16bpp images are created
 *   as R16_UINT by vg_lite_allocate (see vg_lite.c), so the storage view
 *   format always matches the image's own format. The hardware resolves
 *   tile addressing.
 *
 * Unsupported: multi-plane YUV formats (data[1]/data[2]) on both paths;
 * sub-byte and other bpp on the tiled path.
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
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
    push.size = 16; /* width, height, bytes_per_pixel, pad */

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
    VkDeviceSize size;
} staging_t;

static int staging_create(VkDeviceSize size, staging_t *st)
{
    memset(st, 0, sizeof(*st));
    st->size = size;
    VkBufferCreateInfo b_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    b_ci.size = size;
    b_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
/* CPU write into buffer->memory (LINEAR images / shadow buffers)      */
/* ------------------------------------------------------------------ */

static vg_lite_error_t upload_buffer_cpu(vg_lite_buffer_t *buffer,
                                         const uint8_t *data,
                                         uint32_t data_stride,
                                         uint32_t row_bytes)
{
    if (!buffer->memory)
        return VG_LITE_OUT_OF_MEMORY;
    for (int32_t y = 0; y < buffer->height; y++)
        memcpy((uint8_t *)buffer->memory + (size_t)y * buffer->stride,
               data + (size_t)y * data_stride, row_bytes);
    return VG_LITE_SUCCESS;
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
    uint32_t row_bytes = (uint32_t)buffer->width * bytes_pp;
    uint32_t row_padded = (row_bytes + 3) & ~3u;
    VkDeviceSize staging_size = (VkDeviceSize)row_padded * buffer->height;

    staging_t staging;
    if (!staging_create(staging_size, &staging))
        return VG_LITE_OUT_OF_MEMORY;
    memset(staging.mapped, 0, (size_t)staging_size);
    for (int32_t y = 0; y < buffer->height; y++)
        memcpy((uint8_t *)staging.mapped + (size_t)y * row_padded,
               data + (size_t)y * data_stride, row_bytes);

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

    VkDescriptorBufferInfo bi = {0};
    bi.buffer = staging.buffer;
    bi.offset = 0;
    bi.range = VK_WHOLE_SIZE;
    VkDescriptorImageInfo ii = {0};
    ii.sampler = VK_NULL_HANDLE;
    ii.imageView = dst_view;
    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writes[2] = {{0}, {0}};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bi;
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

        struct { uint32_t width, height, bpp, pad; } push;
        push.width = (uint32_t)buffer->width;
        push.height = (uint32_t)buffer->height;
        push.bpp = bytes_pp;
        push.pad = 0;

        vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipe_layout, 0, 1, &ds, 0, NULL);
        vkCmdPushConstants(g_vk_ctx.cmd_buf, pipe_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &push);
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
    vkDestroyImageView(g_vk_ctx.device, dst_view, NULL);
    staging_destroy(&staging);
    return VG_LITE_SUCCESS;

tiled_fail:
    if (ds != VK_NULL_HANDLE)
        vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &ds);
    vkDestroyImageView(g_vk_ctx.device, dst_view, NULL);
    staging_destroy(&staging);
    return VG_LITE_OUT_OF_MEMORY;
}

/* ------------------------------------------------------------------ */
/* OPTIMAL-without-STORAGE path: staging TRANSFER_SRC ->              */
/* vkCmdCopyBufferToImage (driver handles tiling). Generic fallback   */
/* for devices that reject OPTIMAL+STORAGE+MUTABLE_FORMAT.            */
/* ------------------------------------------------------------------ */

static vg_lite_error_t upload_buffer_copy(vg_lite_buffer_t *buffer,
                                          const uint8_t *data,
                                          uint32_t data_stride,
                                          uint32_t row_bytes)
{
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;
    uint32_t h = (uint32_t)buffer->height;

    /* Staging holds tightly packed rows; bufferRowLength = 0 in the copy
     * region tells Vulkan exactly that. */
    VkDeviceSize staging_size = (VkDeviceSize)row_bytes * h;
    staging_t staging;
    memset(&staging, 0, sizeof(staging));
    VkBufferCreateInfo b_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    b_ci.size = staging_size;
    b_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    b_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_vk_ctx.device, &b_ci, NULL, &staging.buffer) != VK_SUCCESS)
        return VG_LITE_OUT_OF_MEMORY;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, staging.buffer, &req);
    VkMemoryAllocateInfo a_ci = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    a_ci.allocationSize = req.size;
    int32_t mem_type = find_memory_type(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type < 0) {
        vkDestroyBuffer(g_vk_ctx.device, staging.buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }
    a_ci.memoryTypeIndex = (uint32_t)mem_type;
    if (vkAllocateMemory(g_vk_ctx.device, &a_ci, NULL, &staging.memory) != VK_SUCCESS ||
        vkMapMemory(g_vk_ctx.device, staging.memory, 0, VK_WHOLE_SIZE, 0, &staging.mapped) != VK_SUCCESS) {
        if (staging.memory) vkFreeMemory(g_vk_ctx.device, staging.memory, NULL);
        vkDestroyBuffer(g_vk_ctx.device, staging.buffer, NULL);
        return VG_LITE_OUT_OF_MEMORY;
    }
    vkBindBufferMemory(g_vk_ctx.device, staging.buffer, staging.memory, 0);
    for (uint32_t y = 0; y < h; y++)
        memcpy((uint8_t *)staging.mapped + (size_t)y * row_bytes,
               data + (size_t)y * data_stride, row_bytes);

    vg_lite_vulkan_flush_render_pass();
    if (vg_lite_vulkan_submit_command(1) != VG_LITE_SUCCESS) {
        staging_destroy(&staging);
        return VG_LITE_OUT_OF_MEMORY;
    }
    vg_lite_vulkan_begin_command();

    VkImageMemoryBarrier dst_bar = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    dst_bar.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    dst_bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dst_bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    dst_bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    dst_bar.image = internal->image;
    dst_bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    dst_bar.subresourceRange.levelCount = 1;
    dst_bar.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &dst_bar);

    VkBufferImageCopy region = {0};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;   /* tightly packed rows */
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)buffer->width;
    region.imageExtent.height = h;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(g_vk_ctx.cmd_buf, staging.buffer, internal->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier gen_bar = dst_bar;
    gen_bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    gen_bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    gen_bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    gen_bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, NULL, 0, NULL, 1, &gen_bar);

    vg_lite_vulkan_submit_command(1);
    staging_destroy(&staging);
    return VG_LITE_SUCCESS;
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
     *  - is_optimal + has_storage -> compute-shader imageStore
     *  - is_optimal, no storage   -> staging + CopyBufferToImage
     *  - linear                   -> staging + CopyBufferToImage (or CPU memcpy)
     * If the compute path is unavailable at upload time (feature/format probe
     * or pipeline failure inside upload_buffer_tiled), degrade to the copy
     * path instead of failing. Shadow-transform formats (A4,
     * OPENVG_sRGBA_8888) have their own GPU layout and dedicated paths. */
    if (internal->is_optimal) {
        if (buffer->format == VG_LITE_A4 || buffer->format == OPENVG_sRGBA_8888)
            return VG_LITE_NOT_SUPPORT;
        if (internal->has_storage) {
            vg_lite_error_t err = VG_LITE_NOT_SUPPORT;
            if (bpp_bits == 32) err = upload_buffer_tiled(buffer, pdata, data_stride, 4);
            else if (bpp_bits == 16) err = upload_buffer_tiled(buffer, pdata, data_stride, 2);
            else if (bpp_bits == 8)  err = upload_buffer_tiled(buffer, pdata, data_stride, 1);
            if (err == VG_LITE_SUCCESS || err == VG_LITE_OUT_OF_MEMORY)
                return err;
            /* compute upload unsupported here -> degrade to copy engine */
        }
        return upload_buffer_copy(buffer, pdata, data_stride, row_bytes);
    }
    /* Shadow formats keep their VGLite CPU layout in buffer->memory
     * (shadow buffer, stride-pitched); write there directly. */
    if (buffer->format == VG_LITE_A4 || buffer->format == OPENVG_sRGBA_8888)
        return upload_buffer_cpu(buffer, pdata, data_stride, row_bytes);

    /* LINEAR (non-shadow): same staging + CopyBufferToImage path — the copy
     * engine handles rowPitch, no memory-alias compute needed. */
    (void)bpp_bits;
    return upload_buffer_copy(buffer, pdata, data_stride, row_bytes);
}
