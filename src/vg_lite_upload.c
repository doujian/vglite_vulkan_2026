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

static VkResult get_upload_pipeline(VkPipeline *pipeline,
                                    VkPipelineLayout *layout,
                                    VkDescriptorSetLayout *desc_layout)
{
    if (*pipeline != VK_NULL_HANDLE) {
        *layout = g_vk_ctx.upload_pipeline_layout;
        *desc_layout = g_vk_ctx.upload_descriptor_layout;
        return VK_SUCCESS;
    }

    VkDescriptorSetLayoutBinding bindings[2] = {{0}, {0}};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dl_ci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl_ci.bindingCount = 2;
    dl_ci.pBindings = bindings;
    VkResult r = vkCreateDescriptorSetLayout(g_vk_ctx.device, &dl_ci, NULL,
                                             &g_vk_ctx.upload_descriptor_layout);
    if (r != VK_SUCCESS) return r;

    VkPushConstantRange push = {0};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.offset = 0;
    push.size = 16; /* row_words, dst_stride_bytes, dst_offset_words, height */

    VkPipelineLayoutCreateInfo pl_ci = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_ci.setLayoutCount = 1;
    pl_ci.pSetLayouts = &g_vk_ctx.upload_descriptor_layout;
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &push;
    r = vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL,
                               &g_vk_ctx.upload_pipeline_layout);
    if (r != VK_SUCCESS) return r;

    VkShaderModule comp = load_shader_module(g_vk_ctx.device, "upload_comp");
    if (comp == VK_NULL_HANDLE) return VK_ERROR_INITIALIZATION_FAILED;

    VkComputePipelineCreateInfo cp_ci = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cp_ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cp_ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cp_ci.stage.module = comp;
    cp_ci.stage.pName = "main";
    cp_ci.layout = g_vk_ctx.upload_pipeline_layout;
    r = vkCreateComputePipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &cp_ci,
                                 NULL, &g_vk_ctx.upload_pipeline);
    vkDestroyShaderModule(g_vk_ctx.device, comp, NULL);
    if (r != VK_SUCCESS) return r;

    *pipeline = g_vk_ctx.upload_pipeline;
    *layout = g_vk_ctx.upload_pipeline_layout;
    *desc_layout = g_vk_ctx.upload_descriptor_layout;
    return VK_SUCCESS;
}

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
/* CPU fallback (linear only)                                          */
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
/* LINEAR path: staging SSBO -> image-memory alias buffer              */
/* ------------------------------------------------------------------ */

static vg_lite_error_t upload_buffer_linear(vg_lite_buffer_t *buffer,
                                            const uint8_t *data,
                                            uint32_t data_stride,
                                            uint32_t row_bytes,
                                            uint32_t bpp_bits)
{
    buffer_internal_t *internal = (buffer_internal_t *)buffer->handle;

    uint32_t row_words = (row_bytes + 3) / 4;
    VkDeviceSize staging_size = (VkDeviceSize)row_words * 4 * buffer->height;

    /* Destination layout must be u32-aligned for the compute path. */
    VkSubresourceLayout layout;
    VkImageSubresource sub = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    vkGetImageSubresourceLayout(g_vk_ctx.device, internal->image, &sub, &layout);
    if ((layout.offset & 3) || (layout.rowPitch & 3))
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);

    VkPipeline pipeline = g_vk_ctx.upload_pipeline;
    VkPipelineLayout pipe_layout = g_vk_ctx.upload_pipeline_layout;
    VkDescriptorSetLayout desc_layout = g_vk_ctx.upload_descriptor_layout;
    if (get_upload_pipeline(&pipeline, &pipe_layout, &desc_layout) != VK_SUCCESS)
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);

    staging_t staging;
    if (!staging_create(staging_size, &staging))
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);

    /* Pack rows into staging; zero-pad tail bytes of each row to 4B. */
    memset(staging.mapped, 0, (size_t)staging_size);
    for (int32_t y = 0; y < buffer->height; y++)
        memcpy((uint8_t *)staging.mapped + (size_t)y * row_words * 4,
               data + (size_t)y * data_stride, row_bytes);
    (void)bpp_bits;

    /* Alias the image's device memory as a storage buffer. The buffer must
     * span the full image memory (offset + rowPitch*height can exceed the
     * packed staging size when rowPitch > row_bytes). */
    VkMemoryRequirements img_req;
    vkGetImageMemoryRequirements(g_vk_ctx.device, internal->image, &img_req);
    if (img_req.size < staging_size ||
        (layout.offset + layout.rowPitch * (VkDeviceSize)(buffer->height - 1) + row_bytes) > img_req.size ||
        internal->memory == VK_NULL_HANDLE) {
        staging_destroy(&staging);
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);
    }

    VkBuffer dst_buf = VK_NULL_HANDLE;
    VkBufferCreateInfo b_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    b_ci.size = img_req.size; /* cover the whole image memory */
    b_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    b_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(g_vk_ctx.device, &b_ci, NULL, &dst_buf) != VK_SUCCESS) {
        staging_destroy(&staging);
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);
    }
    if (vkBindBufferMemory(g_vk_ctx.device, dst_buf, internal->memory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(g_vk_ctx.device, dst_buf, NULL);
        staging_destroy(&staging);
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);
    }

    VkDescriptorSet ds = alloc_desc_set(desc_layout);
    if (ds == VK_NULL_HANDLE) {
        vkDestroyBuffer(g_vk_ctx.device, dst_buf, NULL);
        staging_destroy(&staging);
        return upload_buffer_cpu(buffer, data, data_stride, row_bytes);
    }

    VkDescriptorBufferInfo bi[2] = {{0}, {0}};
    bi[0].buffer = staging.buffer;
    bi[0].offset = 0;
    bi[0].range = VK_WHOLE_SIZE;
    bi[1].buffer = dst_buf;
    bi[1].offset = 0;
    bi[1].range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet writes[2] = {{0}, {0}};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = ds;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bi[0];
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = ds;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &bi[1];
    vkUpdateDescriptorSets(g_vk_ctx.device, 2, writes, 0, NULL);

    /* Drain any in-flight work that may use the image memory, then record. */
    vg_lite_vulkan_flush_render_pass();
    if (vg_lite_vulkan_submit_command(1) != VG_LITE_SUCCESS ||
        vg_lite_vulkan_begin_command() != VG_LITE_SUCCESS)
        goto linear_fail;

    {
        VkBufferMemoryBarrier pre = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        pre.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        pre.buffer = staging.buffer;
        pre.offset = 0;
        pre.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, NULL, 1, &pre, 0, NULL);

        struct { uint32_t row_words, dst_stride, dst_offset, height; } push;
        push.row_words = row_words;
        push.dst_stride = (uint32_t)layout.rowPitch;
        push.dst_offset = (uint32_t)(layout.offset / 4);
        push.height = (uint32_t)buffer->height;

        vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipe_layout, 0, 1, &ds, 0, NULL);
        vkCmdPushConstants(g_vk_ctx.cmd_buf, pipe_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &push);
        vkCmdDispatch(g_vk_ctx.cmd_buf, (row_words + 63) / 64,
                      (uint32_t)buffer->height, 1);

        VkBufferMemoryBarrier post = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        post.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        post.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        post.buffer = dst_buf;
        post.offset = 0;
        post.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, NULL, 1, &post, 0, NULL);
    }

    vg_lite_vulkan_submit_command(1);
    vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &ds);
    vkDestroyBuffer(g_vk_ctx.device, dst_buf, NULL);
    staging_destroy(&staging);
    return VG_LITE_SUCCESS;

linear_fail:
    if (ds != VK_NULL_HANDLE)
        vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &ds);
    vkDestroyBuffer(g_vk_ctx.device, dst_buf, NULL);
    staging_destroy(&staging);
    return VG_LITE_OUT_OF_MEMORY;
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

    if (buffer->tiled == VG_LITE_TILED) {
        if (bpp_bits == 32) return upload_buffer_tiled(buffer, pdata, data_stride, 4);
        if (bpp_bits == 16) return upload_buffer_tiled(buffer, pdata, data_stride, 2);
        if (bpp_bits == 8)  return upload_buffer_tiled(buffer, pdata, data_stride, 1);
        return VG_LITE_NOT_SUPPORT;
    }
    return upload_buffer_linear(buffer, pdata, data_stride, row_bytes, bpp_bits);
}
