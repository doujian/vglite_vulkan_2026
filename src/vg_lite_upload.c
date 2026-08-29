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

/* ------------------------------------------------------------------ */
/* Batch upload: one staging allocation, one command buffer, one       */
/* submit + fence wait for the whole set.                              */
/* ------------------------------------------------------------------ */

typedef struct {
    vg_lite_buffer_t *buf;
    const uint8_t *data;
    uint32_t data_stride;
    uint32_t row_bytes;
    uint32_t bytes_pp;      /* 1/2/4 when the compute path applies   */
    VkDeviceSize offset;    /* segment start inside the batch staging */
    VkDeviceSize size;      /* tight row_bytes * height              */
    uint32_t res_idx;       /* slot in src_views/dst_views/sets      */
    int use_compute;        /* optimal + storage: dispatch path      */
    int use_copy;           /* optimal without compute: copy path    */
} batch_item_t;

static VkFormat batch_view_fmt(uint32_t bytes_pp)
{
    switch (bytes_pp) {
    case 4: return VK_FORMAT_R32_UINT;
    case 2: return VK_FORMAT_R16_UINT;
    default: return VK_FORMAT_R8_UINT;
    }
}

/* Sequential fallback for one item (single-plane data, user stride). */
static vg_lite_error_t seq_upload_one(vg_lite_buffer_t *b,
                                      vg_lite_uint8_t *data, vg_lite_uint32_t stride)
{
    vg_lite_uint8_t *d[3] = {data, NULL, NULL};
    vg_lite_uint32_t s[3] = {stride, 0, 0};
    return vg_lite_upload_buffer(b, d, s);
}

vg_lite_error_t vg_lite_upload_buffers(vg_lite_buffer_t **bufs,
                                       vg_lite_uint8_t **datas,
                                       vg_lite_uint32_t *strides,
                                       vg_lite_uint32_t count)
{
    if (!bufs || !datas || !strides || count == 0)
        return VG_LITE_INVALID_ARGUMENT;

    batch_item_t *items = (batch_item_t *)calloc(count, sizeof(batch_item_t));
    if (!items)
        return VG_LITE_OUT_OF_MEMORY;

    VkPipeline pipeline = g_vk_ctx.upload_tiled_pipeline;
    VkPipelineLayout pipe_layout = g_vk_ctx.upload_tiled_pipeline_layout;
    VkDescriptorSetLayout desc_layout = g_vk_ctx.upload_tiled_descriptor_layout;
    uint32_t n_gpu = 0, n_compute = 0;
    VkDeviceSize total = 0;
    vg_lite_error_t err = VG_LITE_SUCCESS;

    /* Pass 1: classify every buffer and size the single staging block. */
    for (uint32_t i = 0; i < count; i++) {
        vg_lite_buffer_t *b = bufs[i];
        buffer_internal_t *in = b ? (buffer_internal_t *)b->handle : NULL;
        if (!b || !in || !b->width || !b->height || !datas[i] ||
            vg_lite_is_yuv_format(b->format)) {
            err = VG_LITE_INVALID_ARGUMENT;
            goto plan_done;
        }
        batch_item_t *it = &items[i];
        it->buf = b;
        it->data = datas[i];
        uint32_t bpp_bits = vg_lite_format_bpp(b->format);
        it->row_bytes = ((uint32_t)b->width * bpp_bits + 7) / 8;
        it->data_stride = strides[i] ? strides[i] : it->row_bytes;
        if (it->data_stride < it->row_bytes) {
            err = VG_LITE_INVALID_ARGUMENT;
            goto plan_done;
        }
        it->size = (VkDeviceSize)it->row_bytes * b->height;

        if (!in->is_optimal) {
            /* LINEAR (incl. shadow formats): handled CPU-side below /
             * delegated after the GPU phase; no staging segment. */
            continue;
        }
        if (in->cpu_cache) { free(in->cpu_cache); in->cpu_cache = NULL; }
        it->offset = (total + 15u) & ~(VkDeviceSize)15u; /* view+copy offset alignment */
        total = it->offset + it->size;
        n_gpu++;

        it->bytes_pp = bpp_bits == 32 ? 4 : bpp_bits == 16 ? 2 : bpp_bits == 8 ? 1 : 0;
        if (in->has_storage && it->bytes_pp && g_vk_ctx.storage_image_wo_format &&
            n_compute < 32 /* descriptor pool headroom */) {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(g_vk_ctx.physical_device,
                                                batch_view_fmt(it->bytes_pp), &fp);
            VkPhysicalDeviceProperties pdp;
            vkGetPhysicalDeviceProperties(g_vk_ctx.physical_device, &pdp);
            if ((fp.optimalTilingFeatures & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT) &&
                it->size / it->bytes_pp <= pdp.limits.maxTexelBufferElements) {
                it->use_compute = 1;
                n_compute++;
                continue;
            }
        }
        it->use_copy = 1; /* OPTIMAL fallback: copy engine */
    }
    if (get_upload_tiled_pipeline(&pipeline, &pipe_layout, &desc_layout) != VK_SUCCESS) {
        /* no compute pipeline: every optimal item goes through the copy engine */
        for (uint32_t i = 0; i < count; i++) {
            buffer_internal_t *in = (buffer_internal_t *)items[i].buf->handle;
            if (in->is_optimal) {
                items[i].use_compute = 0;
                items[i].use_copy = 1;
            }
        }
    }

plan_done:
    if (err != VG_LITE_SUCCESS) { free(items); return err; }

    /* CPU-only phase first: LINEAR non-shadow rows land in mapped memory. */
    for (uint32_t i = 0; i < count; i++) {
        batch_item_t *it = &items[i];
        buffer_internal_t *in = (buffer_internal_t *)it->buf->handle;
        if (!in->is_optimal) {
            if (it->buf->format == VG_LITE_A4 || it->buf->format == OPENVG_sRGBA_8888)
                continue; /* delegated after the GPU phase */
            if (!it->buf->memory) { err = VG_LITE_OUT_OF_MEMORY; break; }
            for (int32_t y = 0; y < it->buf->height; y++)
                memcpy((uint8_t *)it->buf->memory + (size_t)y * it->buf->stride,
                       it->data + (size_t)y * it->data_stride, it->row_bytes);
            vg_lite_buffer_flush(it->buf);
        }
    }
    if (err != VG_LITE_SUCCESS) {
        free(items);
        /* degrade: upload everything one by one (handles all shapes) */
        for (uint32_t i = 0; i < count; i++) {
            seq_upload_one(bufs[i], datas[i], strides[i]);
        }
        return err;
    }

    /* GPU phase: single staging, single command buffer, single submit. */
    if (n_gpu == 0) {
        free(items);
        goto delegate;
    }

    {
        staging_t staging;
        if (!staging_create(total ? total : 4,
                            VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
                            &staging)) {
            free(items);
            /* degrade to sequential uploads for every buffer */
            for (uint32_t i = 0; i < count; i++) {
                seq_upload_one(bufs[i], datas[i], strides[i]);
            }
            return VG_LITE_SUCCESS;
        }

        for (uint32_t i = 0; i < count; i++) {
            batch_item_t *it = &items[i];
            if (!it->size) continue;
            for (int32_t y = 0; y < it->buf->height; y++)
                memcpy((uint8_t *)staging.mapped + it->offset + (size_t)y * it->row_bytes,
                       it->data + (size_t)y * it->data_stride, it->row_bytes);
        }

        VkBufferView *src_views = (VkBufferView *)calloc(n_compute ? n_compute : 1, sizeof(VkBufferView));
        VkImageView *dst_views = (VkImageView *)calloc(n_compute ? n_compute : 1, sizeof(VkImageView));
        VkDescriptorSet *sets = (VkDescriptorSet *)calloc(n_compute ? n_compute : 1, sizeof(VkDescriptorSet));
        uint32_t vi = 0;
        vg_lite_error_t rec_err = VG_LITE_SUCCESS;

        if (!src_views || !dst_views || !sets)
            rec_err = VG_LITE_OUT_OF_MEMORY;

        if (rec_err == VG_LITE_SUCCESS) {
            vg_lite_vulkan_flush_render_pass();
            if (vg_lite_vulkan_submit_command(1) != VG_LITE_SUCCESS ||
                vg_lite_vulkan_begin_command() != VG_LITE_SUCCESS)
                rec_err = VG_LITE_OUT_OF_MEMORY;
        }

        /* One global host->device barrier covers every staging segment. */
        if (rec_err == VG_LITE_SUCCESS) {
            VkBufferMemoryBarrier buf_pre = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            buf_pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            buf_pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            buf_pre.buffer = staging.buffer;
            buf_pre.offset = 0;
            buf_pre.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
                VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 1, &buf_pre, 0, NULL);
        }

        for (uint32_t i = 0; i < count && rec_err == VG_LITE_SUCCESS; i++) {
            batch_item_t *it = &items[i];
            buffer_internal_t *in = (buffer_internal_t *)it->buf->handle;
            if (!it->size) continue;

            if (it->use_compute) {
                VkBufferViewCreateInfo bv_ci = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
                bv_ci.buffer = staging.buffer;
                bv_ci.format = batch_view_fmt(it->bytes_pp);
                bv_ci.offset = it->offset;
                bv_ci.range = it->size;
                if (vkCreateBufferView(g_vk_ctx.device, &bv_ci, NULL, &src_views[vi]) != VK_SUCCESS) {
                    it->use_compute = 0;
                    it->use_copy = 1; /* degrade this item to the copy path */
                } else {
                    VkImageViewCreateInfo v_ci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    v_ci.image = in->image;
                    v_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    v_ci.format = bv_ci.format;
                    v_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    v_ci.subresourceRange.levelCount = 1;
                    v_ci.subresourceRange.layerCount = 1;
                    if (vkCreateImageView(g_vk_ctx.device, &v_ci, NULL, &dst_views[vi]) != VK_SUCCESS) {
                        vkDestroyBufferView(g_vk_ctx.device, src_views[vi], NULL);
                        src_views[vi] = VK_NULL_HANDLE;
                        it->use_compute = 0;
                        it->use_copy = 1;
                    }
                }
                if (it->use_compute) {
                    sets[vi] = alloc_desc_set(desc_layout);
                    if (sets[vi] == VK_NULL_HANDLE) {
                        vkDestroyBufferView(g_vk_ctx.device, src_views[vi], NULL);
                        vkDestroyImageView(g_vk_ctx.device, dst_views[vi], NULL);
                        src_views[vi] = VK_NULL_HANDLE;
                        dst_views[vi] = VK_NULL_HANDLE;
                        it->use_compute = 0;
                        it->use_copy = 1;
                    }
                }
                if (it->use_compute) {
                    it->res_idx = vi;
                    VkDescriptorImageInfo ii = {0};
                    ii.imageView = dst_views[vi];
                    ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    VkWriteDescriptorSet writes[2] = {{0}, {0}};
                    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[0].dstSet = sets[vi];
                    writes[0].dstBinding = 0;
                    writes[0].descriptorCount = 1;
                    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                    writes[0].pTexelBufferView = &src_views[vi];
                    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[1].dstSet = sets[vi];
                    writes[1].dstBinding = 1;
                    writes[1].descriptorCount = 1;
                    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    writes[1].pImageInfo = &ii;
                    vkUpdateDescriptorSets(g_vk_ctx.device, 2, writes, 0, NULL);
                    vi++;
                }
            }
        }

        /* Record dispatches and copies in one pass. */
        for (uint32_t i = 0; i < count && rec_err == VG_LITE_SUCCESS; i++) {
            batch_item_t *it = &items[i];
            buffer_internal_t *in = (buffer_internal_t *)it->buf->handle;
            if (!it->size) continue;

            if (it->use_compute) {
                uint32_t k = it->res_idx;
                VkImageMemoryBarrier img_pre = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                img_pre.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                img_pre.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                img_pre.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                img_pre.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                img_pre.image = in->image;
                img_pre.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                img_pre.subresourceRange.levelCount = 1;
                img_pre.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0, 0, NULL, 0, NULL, 1, &img_pre);

                struct { uint32_t width, height; } push;
                push.width = (uint32_t)it->buf->width;
                push.height = (uint32_t)it->buf->height;
                vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        pipe_layout, 0, 1, &sets[k], 0, NULL);
                vkCmdPushConstants(g_vk_ctx.cmd_buf, pipe_layout,
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, &push);
                vkCmdDispatch(g_vk_ctx.cmd_buf,
                              ((uint32_t)it->buf->width + 7) / 8,
                              ((uint32_t)it->buf->height + 7) / 8, 1);

                VkImageMemoryBarrier img_post = img_pre;
                img_post.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                img_post.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    0, 0, NULL, 0, NULL, 1, &img_post);
            } else if (it->use_copy) {
                VkImageMemoryBarrier dst_bar = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                dst_bar.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                dst_bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                dst_bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                dst_bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                dst_bar.image = in->image;
                dst_bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                dst_bar.subresourceRange.levelCount = 1;
                dst_bar.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, NULL, 0, NULL, 1, &dst_bar);

                VkBufferImageCopy region = {0};
                region.bufferOffset = it->offset;
                region.bufferRowLength = 0; /* tight rows */
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.layerCount = 1;
                region.imageExtent.width = (uint32_t)it->buf->width;
                region.imageExtent.height = (uint32_t)it->buf->height;
                region.imageExtent.depth = 1;
                vkCmdCopyBufferToImage(g_vk_ctx.cmd_buf, staging.buffer, in->image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                VkImageMemoryBarrier gen_bar = dst_bar;
                gen_bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                gen_bar.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                gen_bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                gen_bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                vkCmdPipelineBarrier(g_vk_ctx.cmd_buf,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    0, 0, NULL, 0, NULL, 1, &gen_bar);
            }
        }

        if (rec_err == VG_LITE_SUCCESS) {
            vg_lite_vulkan_submit_command(1);
        } else {
            vg_lite_vulkan_begin_command(); /* keep the context usable */
            /* the batch never executed: degrade the optimal buffers */
            for (uint32_t i = 0; i < count; i++) {
                buffer_internal_t *in = (buffer_internal_t *)bufs[i]->handle;
                if (in->is_optimal)
                    seq_upload_one(bufs[i], datas[i], strides[i]);
            }
        }

        for (uint32_t k = 0; k < n_compute; k++) {
            if (sets[k])
                vkFreeDescriptorSets(g_vk_ctx.device, g_vk_ctx.descriptor_pool, 1, &sets[k]);
            if (src_views[k]) vkDestroyBufferView(g_vk_ctx.device, src_views[k], NULL);
            if (dst_views[k]) vkDestroyImageView(g_vk_ctx.device, dst_views[k], NULL);
        }
        free(src_views);
        free(dst_views);
        free(sets);
        staging_destroy(&staging);
        free(items);
    }

delegate:
    /* Shadow-transform LINEAR formats keep their dedicated path. */
    for (uint32_t i = 0; i < count; i++) {
        vg_lite_buffer_t *b = bufs[i];
        buffer_internal_t *in = (buffer_internal_t *)b->handle;
        if (!in->is_optimal &&
            (b->format == VG_LITE_A4 || b->format == OPENVG_sRGBA_8888)) {
            uint32_t bpp_bits = vg_lite_format_bpp(b->format);
            uint32_t row_bytes = ((uint32_t)b->width * bpp_bits + 7) / 8;
            uint32_t data_stride = strides[i] ? strides[i] : row_bytes;
            err = upload_buffer_staging(b, datas[i], data_stride, row_bytes);
            if (err != VG_LITE_SUCCESS) return err;
        }
    }
    return VG_LITE_SUCCESS;
}
