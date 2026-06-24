#include "vg_lite_vulkan.h"
#include "vg_lite_format.h"
#include "vg_lite_math.h"
#include "vlc_parser.h"
#include "tessellator.h"
#include "draw_vert_spv.h"
#include "draw_frag_spv.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#define MAX_PENDING_BUFFERS 512

typedef struct {
    VkBuffer vbo;
    VkDeviceMemory vbo_mem;
    VkBuffer ibo;
    VkDeviceMemory ibo_mem;
} pending_buffer_t;

typedef struct {
    VkShaderModule vert_shader;
    VkShaderModule frag_shader;
    VkPipelineLayout pipeline_layout;
    VkPipeline fill_pipeline;
    VkPipeline stencil_pipeline;
    VkPipeline cover_pipeline;
    VkBuffer cover_vbo;
    VkDeviceMemory cover_vbo_mem;
    VkBuffer cover_ibo;
    VkDeviceMemory cover_ibo_mem;
    pending_buffer_t pending_buffers[MAX_PENDING_BUFFERS];
    int pending_count;
} draw_pipeline_t;

static draw_pipeline_t g_draw_pipeline = {0};

static VkShaderModule create_shader_module(const uint8_t* code, size_t size)
{
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = size;
    ci.pCode = (const uint32_t*)code;
    VkShaderModule module;
    if (vkCreateShaderModule(g_vk_ctx.device, &ci, NULL, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

static void init_draw_pipeline(VkFormat format)
{
    if (g_draw_pipeline.fill_pipeline) {
        // Already initialized, skip
        return;
    }
    
    g_draw_pipeline.vert_shader = create_shader_module(draw_vert_spv_data, draw_vert_spv_size);
    g_draw_pipeline.frag_shader = create_shader_module(draw_frag_spv_data, draw_frag_spv_size);
    
    VkPushConstantRange pc_range = {0};
    pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pc_range.offset = 0;
    pc_range.size = 56;
    
    VkPipelineLayoutCreateInfo pl_ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_ci.pushConstantRangeCount = 1;
    pl_ci.pPushConstantRanges = &pc_range;
    VK_CHECK(vkCreatePipelineLayout(g_vk_ctx.device, &pl_ci, NULL, &g_draw_pipeline.pipeline_layout));
    
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, g_draw_pipeline.vert_shader, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, g_draw_pipeline.frag_shader, "main", NULL},
    };
    
    VkVertexInputBindingDescription binding = {0, sizeof(float) * 2, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;
    
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
    
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_FALSE;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;
    
    VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.scissorCount = 1;
    
    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn_ci = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn_ci.dynamicStateCount = 2;
    dyn_ci.pDynamicStates = dyn_states;
    
    VkRenderPass rp = vg_lite_vulkan_create_render_pass(format);
    
    VkGraphicsPipelineCreateInfo gp_ci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp_ci.stageCount = 2;
    gp_ci.pStages = stages;
    gp_ci.pVertexInputState = &vi;
    gp_ci.pInputAssemblyState = &ia;
    gp_ci.pViewportState = &vs;
    gp_ci.pRasterizationState = &rs;
    gp_ci.pMultisampleState = &ms;
    gp_ci.pColorBlendState = &cb;
    gp_ci.pDepthStencilState = &ds;
    gp_ci.pDynamicState = &dyn_ci;
    gp_ci.layout = g_draw_pipeline.pipeline_layout;
    gp_ci.renderPass = rp;
    gp_ci.subpass = 0;
    
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_draw_pipeline.fill_pipeline));
    
    VkPipelineDepthStencilStateCreateInfo stencil_ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    stencil_ds.depthTestEnable = VK_FALSE;
    stencil_ds.depthWriteEnable = VK_FALSE;
    stencil_ds.stencilTestEnable = VK_TRUE;
    /* EVEN_ODD fill rule: INVERT LSB on every triangle pass */
    stencil_ds.front.failOp = VK_STENCIL_OP_KEEP;
    stencil_ds.front.passOp = VK_STENCIL_OP_INVERT;
    stencil_ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
    stencil_ds.front.compareOp = VK_COMPARE_OP_ALWAYS;
    stencil_ds.front.compareMask = 0x01;
    stencil_ds.front.writeMask = 0x01;
    stencil_ds.front.reference = 0;
    stencil_ds.back.failOp = VK_STENCIL_OP_KEEP;
    stencil_ds.back.passOp = VK_STENCIL_OP_INVERT;
    stencil_ds.back.depthFailOp = VK_STENCIL_OP_KEEP;
    stencil_ds.back.compareOp = VK_COMPARE_OP_ALWAYS;
    stencil_ds.back.compareMask = 0x01;
    stencil_ds.back.writeMask = 0x01;
    stencil_ds.back.reference = 0;
    
    VkPipelineColorBlendAttachmentState stencil_cba = {0};
    stencil_cba.colorWriteMask = 0;
    VkPipelineColorBlendStateCreateInfo stencil_cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    stencil_cb.attachmentCount = 1;
    stencil_cb.pAttachments = &stencil_cba;
    
    gp_ci.pColorBlendState = &stencil_cb;
    gp_ci.pDepthStencilState = &stencil_ds;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_draw_pipeline.stencil_pipeline));
    
    VkPipelineDepthStencilStateCreateInfo cover_ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    cover_ds.depthTestEnable = VK_FALSE;
    cover_ds.depthWriteEnable = VK_FALSE;
    cover_ds.stencilTestEnable = VK_TRUE;
    cover_ds.front.failOp = VK_STENCIL_OP_KEEP;
    cover_ds.front.passOp = VK_STENCIL_OP_ZERO;  /* Reset stencil after cover */
    cover_ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
    cover_ds.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;  /* Pass if stencil LSB != 0 */
    cover_ds.front.compareMask = 0x01;
    cover_ds.front.writeMask = 0x01;
    cover_ds.front.reference = 0;
    cover_ds.back.failOp = VK_STENCIL_OP_KEEP;
    cover_ds.back.passOp = VK_STENCIL_OP_ZERO;
    cover_ds.back.depthFailOp = VK_STENCIL_OP_KEEP;
    cover_ds.back.compareOp = VK_COMPARE_OP_NOT_EQUAL;
    cover_ds.back.compareMask = 0x01;
    cover_ds.back.writeMask = 0x01;
    cover_ds.back.reference = 0;
    
    VkPipelineInputAssemblyStateCreateInfo cover_ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    cover_ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    
    gp_ci.pInputAssemblyState = &cover_ia;
    gp_ci.pColorBlendState = &cb;
    gp_ci.pDepthStencilState = &cover_ds;
    VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_draw_pipeline.cover_pipeline));
    
    gp_ci.pInputAssemblyState = &ia;
    
    vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
    
    VkBufferCreateInfo cover_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    cover_ci.size = 256;
    cover_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &cover_ci, NULL, &g_draw_pipeline.cover_vbo));
    
    VkMemoryRequirements cover_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_draw_pipeline.cover_vbo, &cover_req);
    VkMemoryAllocateInfo cover_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    cover_alloc.allocationSize = cover_req.size;
    cover_alloc.memoryTypeIndex = (uint32_t)find_memory_type(cover_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &cover_alloc, NULL, &g_draw_pipeline.cover_vbo_mem));
    vkBindBufferMemory(g_vk_ctx.device, g_draw_pipeline.cover_vbo, g_draw_pipeline.cover_vbo_mem, 0);
    
    VkBufferCreateInfo cover_ibo_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    cover_ibo_ci.size = 6 * sizeof(uint32_t);
    cover_ibo_ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &cover_ibo_ci, NULL, &g_draw_pipeline.cover_ibo));
    
    VkMemoryRequirements cover_ibo_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, g_draw_pipeline.cover_ibo, &cover_ibo_req);
    VkMemoryAllocateInfo cover_ibo_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    cover_ibo_alloc.allocationSize = cover_ibo_req.size;
    cover_ibo_alloc.memoryTypeIndex = (uint32_t)find_memory_type(cover_ibo_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &cover_ibo_alloc, NULL, &g_draw_pipeline.cover_ibo_mem));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, g_draw_pipeline.cover_ibo, g_draw_pipeline.cover_ibo_mem, 0));
    
    uint32_t cover_indices[6] = {0, 1, 2, 0, 2, 3};
    void* ibo_ptr;
    VK_CHECK(vkMapMemory(g_vk_ctx.device, g_draw_pipeline.cover_ibo_mem, 0, 6 * sizeof(uint32_t), 0, &ibo_ptr));
    memcpy(ibo_ptr, cover_indices, 6 * sizeof(uint32_t));
    vkUnmapMemory(g_vk_ctx.device, g_draw_pipeline.cover_ibo_mem);
    
    /* Test: map and write to cover VBO immediately */
    void* test_ptr;
    VkResult map_result = vkMapMemory(g_vk_ctx.device, g_draw_pipeline.cover_vbo_mem, 0, 256, 0, &test_ptr);
    if (map_result == VK_SUCCESS && test_ptr) {
        float test_data[8] = {-1, -1, 1, -1, 1, 1, -1, 1};
        memcpy(test_ptr, test_data, sizeof(test_data));
        vkUnmapMemory(g_vk_ctx.device, g_draw_pipeline.cover_vbo_mem);
    }
}

static void create_vertex_buffer(int vertex_count, int index_count, VkBuffer* vbo, VkDeviceMemory* vbo_mem, VkBuffer* ibo, VkDeviceMemory* ibo_mem)
{
    if (vertex_count == 0) return;
    
    VkBufferCreateInfo vbo_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    vbo_ci.size = vertex_count * sizeof(float) * 2;
    vbo_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &vbo_ci, NULL, vbo));
    
    VkMemoryRequirements vbo_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, *vbo, &vbo_req);
    VkMemoryAllocateInfo vbo_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    vbo_alloc.allocationSize = vbo_req.size;
    vbo_alloc.memoryTypeIndex = (uint32_t)find_memory_type(vbo_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &vbo_alloc, NULL, vbo_mem));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, *vbo, *vbo_mem, 0));
    
    if (index_count == 0) return;
    
    VkBufferCreateInfo ibo_ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ibo_ci.size = index_count * sizeof(uint32_t);
    ibo_ci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    VK_CHECK(vkCreateBuffer(g_vk_ctx.device, &ibo_ci, NULL, ibo));
    
    VkMemoryRequirements ibo_req;
    vkGetBufferMemoryRequirements(g_vk_ctx.device, *ibo, &ibo_req);
    VkMemoryAllocateInfo ibo_alloc = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ibo_alloc.allocationSize = ibo_req.size;
    ibo_alloc.memoryTypeIndex = (uint32_t)find_memory_type(ibo_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(g_vk_ctx.device, &ibo_alloc, NULL, ibo_mem));
    VK_CHECK(vkBindBufferMemory(g_vk_ctx.device, *ibo, *ibo_mem, 0));
}

static void upload_geom(VkBuffer vbo, VkDeviceMemory vbo_mem, VkBuffer ibo, VkDeviceMemory ibo_mem, TessGeometry* geom)
{
    void* ptr;
    VK_CHECK(vkMapMemory(g_vk_ctx.device, vbo_mem, 0, geom->vertex_count * sizeof(float) * 2, 0, &ptr));
    for (int i = 0; i < geom->vertex_count; i++) {
        ((float*)ptr)[i*2] = geom->vertices[i].x;
        ((float*)ptr)[i*2+1] = geom->vertices[i].y;
    }
    vkUnmapMemory(g_vk_ctx.device, vbo_mem);
    
    VK_CHECK(vkMapMemory(g_vk_ctx.device, ibo_mem, 0, geom->index_count * sizeof(uint32_t), 0, &ptr));
    memcpy(ptr, geom->indices, geom->index_count * sizeof(uint32_t));
    vkUnmapMemory(g_vk_ctx.device, ibo_mem);
}

static void destroy_buffer(VkBuffer buf, VkDeviceMemory mem)
{
    if (buf) vkDestroyBuffer(g_vk_ctx.device, buf, NULL);
    if (mem) vkFreeMemory(g_vk_ctx.device, mem, NULL);
}

static void add_pending_buffer(VkBuffer vbo, VkDeviceMemory vbo_mem, VkBuffer ibo, VkDeviceMemory ibo_mem)
{
    if (g_draw_pipeline.pending_count < MAX_PENDING_BUFFERS) {
        g_draw_pipeline.pending_buffers[g_draw_pipeline.pending_count].vbo = vbo;
        g_draw_pipeline.pending_buffers[g_draw_pipeline.pending_count].vbo_mem = vbo_mem;
        g_draw_pipeline.pending_buffers[g_draw_pipeline.pending_count].ibo = ibo;
        g_draw_pipeline.pending_buffers[g_draw_pipeline.pending_count].ibo_mem = ibo_mem;
        g_draw_pipeline.pending_count++;
    } else {
        destroy_buffer(vbo, vbo_mem);
        destroy_buffer(ibo, ibo_mem);
    }
}

void vg_lite_draw_cleanup_pending_buffers(void)
{
    for (int i = 0; i < g_draw_pipeline.pending_count; i++) {
        destroy_buffer(g_draw_pipeline.pending_buffers[i].vbo, 
                       g_draw_pipeline.pending_buffers[i].vbo_mem);
        destroy_buffer(g_draw_pipeline.pending_buffers[i].ibo,
                       g_draw_pipeline.pending_buffers[i].ibo_mem);
    }
    g_draw_pipeline.pending_count = 0;
}

vg_lite_error_t vg_lite_draw_impl(vg_lite_buffer_t *target, vg_lite_path_t *path,
                             vg_lite_fill_t fill_rule, vg_lite_matrix_t *matrix,
                             vg_lite_blend_t blend, vg_lite_color_t color)
{
    (void)fill_rule; (void)blend;
    if (!target || !path) return VG_LITE_INVALID_ARGUMENT;
    if (!path->path || path->path_length == 0) return VG_LITE_INVALID_ARGUMENT;
    
    VlcPath vlc_path;
    vlc_path_init(&vlc_path);
    int cmd_count = vlc_parse_path(path, &vlc_path);
    if (cmd_count <= 0) {
        vlc_path_free(&vlc_path);
        return VG_LITE_INVALID_ARGUMENT;
    }
    
    TessGeometry geom;
    tess_geometry_init(&geom);
    int vtx_count = tessellate_path(&vlc_path, &geom);
    if (vtx_count <= 0 || geom.vertex_count == 0 || geom.index_count == 0) {
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return VG_LITE_SUCCESS;
    }
    
    VkFormat vkfmt = vg_lite_format_to_vk(target->format);
    init_draw_pipeline(vkfmt);
    
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;
    
    VkBuffer vbo = VK_NULL_HANDLE, ibo = VK_NULL_HANDLE;
    VkDeviceMemory vbo_mem = VK_NULL_HANDLE, ibo_mem = VK_NULL_HANDLE;
    create_vertex_buffer(geom.vertex_count, geom.index_count, &vbo, &vbo_mem, &ibo, &ibo_mem);
    upload_geom(vbo, vbo_mem, ibo, ibo_mem, &geom);
    
    vg_lite_error_t err = vg_lite_vulkan_begin_command();
    if (err != VG_LITE_SUCCESS) {
        destroy_buffer(vbo, vbo_mem);
        destroy_buffer(ibo, ibo_mem);
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return err;
    }
    
    if (g_vk_ctx.current_fb == VK_NULL_HANDLE || g_vk_ctx.current_fb_image != internal->image) {
        err = vg_lite_vulkan_set_render_target(target);
        if (err != VG_LITE_SUCCESS) {
            destroy_buffer(vbo, vbo_mem);
            destroy_buffer(ibo, ibo_mem);
            tess_geometry_free(&geom);
            vlc_path_free(&vlc_path);
            return err;
        }
    }
    
    VkViewport vp = {0, 0, (float)target->width, (float)target->height, 0, 1};
    vkCmdSetViewport(g_vk_ctx.cmd_buf, 0, 1, &vp);
    vg_lite_vulkan_apply_scissor(target->width, target->height);
    
struct {
        float m0[4];
        float m1[4];
        float m2[4];
        int blend;
        uint32_t color;
    } pc_data;
    
    float w = (float)target->width;
    float h = (float)target->height;
    
    float screen_to_ndc[3][3] = {
        {2.0f/w, 0, -1.0f},
        {0, 2.0f/h, -1.0f},
        {0, 0, 1.0f}
    };
    
    float combined[3][3] = {0};
    
    if (matrix) {
        mat3_multiply(screen_to_ndc, matrix->m, combined);
        
        for (int col = 0; col < 3; col++) {
            pc_data.m0[col] = combined[0][col];
            pc_data.m1[col] = combined[1][col];
            pc_data.m2[col] = combined[2][col];
        }
        pc_data.m0[3] = 0; pc_data.m1[3] = 0; pc_data.m2[3] = 0;
    } else {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                combined[i][j] = (i == j) ? 1.0f : 0.0f;  // Identity matrix for full-screen test
        for (int col = 0; col < 3; col++) {
            pc_data.m0[col] = combined[0][col];
            pc_data.m1[col] = combined[1][col];
            pc_data.m2[col] = combined[2][col];
        }
        pc_data.m0[3] = 0; pc_data.m1[3] = 0; pc_data.m2[3] = 0;
    }
    pc_data.blend = 0;
    pc_data.color = color;
    
    vkCmdPushConstants(g_vk_ctx.cmd_buf, g_draw_pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc_data), &pc_data);
    
    /* Pass 1: Stencil pass - draw triangles to set stencil */
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(g_vk_ctx.cmd_buf, 0, 1, &vbo, &offset);
    vkCmdBindIndexBuffer(g_vk_ctx.cmd_buf, ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_draw_pipeline.stencil_pipeline);
    vkCmdDrawIndexed(g_vk_ctx.cmd_buf, geom.index_count, 1, 0, 0, 0);
    
    /* Pass 2: Cover pass - draw cover quad, test stencil, write color */
    struct {
        float m0[4];
        float m1[4];
        float m2[4];
        int blend;
        uint32_t color;
    } cover_pc = {0};
    cover_pc.m0[0] = 1; cover_pc.m0[1] = 0; cover_pc.m0[2] = 0;
    cover_pc.m1[0] = 0; cover_pc.m1[1] = 1; cover_pc.m1[2] = 0;
    cover_pc.m2[0] = 0; cover_pc.m2[1] = 0; cover_pc.m2[2] = 1;
    cover_pc.blend = 0;
    cover_pc.color = color;
    vkCmdPushConstants(g_vk_ctx.cmd_buf, g_draw_pipeline.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(cover_pc), &cover_pc);
    
    VkDeviceSize cover_offset = 0;
    vkCmdBindVertexBuffers(g_vk_ctx.cmd_buf, 0, 1, &g_draw_pipeline.cover_vbo, &cover_offset);
    vkCmdBindIndexBuffer(g_vk_ctx.cmd_buf, g_draw_pipeline.cover_ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_draw_pipeline.cover_pipeline);
    vkCmdDrawIndexed(g_vk_ctx.cmd_buf, 6, 1, 0, 0, 0);
    
    tess_geometry_free(&geom);
    vlc_path_free(&vlc_path);
    add_pending_buffer(vbo, vbo_mem, ibo, ibo_mem);
    
    return err;
}

static VkSampler s_pattern_sampler = VK_NULL_HANDLE;

void vg_lite_draw_cleanup(void)
{
    if (g_draw_pipeline.fill_pipeline) {
        vkDestroyPipeline(g_vk_ctx.device, g_draw_pipeline.fill_pipeline, NULL);
        g_draw_pipeline.fill_pipeline = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.stencil_pipeline) {
        vkDestroyPipeline(g_vk_ctx.device, g_draw_pipeline.stencil_pipeline, NULL);
        g_draw_pipeline.stencil_pipeline = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.cover_pipeline) {
        vkDestroyPipeline(g_vk_ctx.device, g_draw_pipeline.cover_pipeline, NULL);
        g_draw_pipeline.cover_pipeline = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.cover_vbo) {
        vkDestroyBuffer(g_vk_ctx.device, g_draw_pipeline.cover_vbo, NULL);
        g_draw_pipeline.cover_vbo = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.cover_vbo_mem) {
        vkFreeMemory(g_vk_ctx.device, g_draw_pipeline.cover_vbo_mem, NULL);
        g_draw_pipeline.cover_vbo_mem = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.cover_ibo) {
        vkDestroyBuffer(g_vk_ctx.device, g_draw_pipeline.cover_ibo, NULL);
        g_draw_pipeline.cover_ibo = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.cover_ibo_mem) {
        vkFreeMemory(g_vk_ctx.device, g_draw_pipeline.cover_ibo_mem, NULL);
        g_draw_pipeline.cover_ibo_mem = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.pipeline_layout) {
        vkDestroyPipelineLayout(g_vk_ctx.device, g_draw_pipeline.pipeline_layout, NULL);
        g_draw_pipeline.pipeline_layout = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.vert_shader) {
        vkDestroyShaderModule(g_vk_ctx.device, g_draw_pipeline.vert_shader, NULL);
        g_draw_pipeline.vert_shader = VK_NULL_HANDLE;
    }
    if (g_draw_pipeline.frag_shader) {
        vkDestroyShaderModule(g_vk_ctx.device, g_draw_pipeline.frag_shader, NULL);
        g_draw_pipeline.frag_shader = VK_NULL_HANDLE;
    }
    if (s_pattern_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(g_vk_ctx.device, s_pattern_sampler, NULL);
        s_pattern_sampler = VK_NULL_HANDLE;
    }
}

static VkSampler get_or_create_pattern_sampler(void)
{
    if (s_pattern_sampler != VK_NULL_HANDLE) return s_pattern_sampler;
    
    VkSamplerCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    ci.magFilter = VK_FILTER_NEAREST;
    ci.minFilter = VK_FILTER_NEAREST;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.anisotropyEnable = VK_FALSE;
    ci.maxAnisotropy = 1.0f;
    ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    ci.unnormalizedCoordinates = VK_FALSE;
    ci.compareEnable = VK_FALSE;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VK_CHECK(vkCreateSampler(g_vk_ctx.device, &ci, NULL, &s_pattern_sampler));
    return s_pattern_sampler;
}

vg_lite_error_t vg_lite_draw_pattern(vg_lite_buffer_t *target,
                                     vg_lite_path_t *path,
                                     vg_lite_fill_t fill_rule,
                                     vg_lite_matrix_t *path_matrix,
                                     vg_lite_buffer_t *pattern_image,
                                     vg_lite_matrix_t *pattern_matrix,
                                     vg_lite_blend_t blend,
                                     vg_lite_pattern_mode_t pattern_mode,
                                     vg_lite_color_t pattern_color,
                                     vg_lite_color_t color,
                                     vg_lite_filter_t filter)
{
    (void)fill_rule; (void)color; (void)filter;
    
    if (!target || !path || !pattern_image) return VG_LITE_INVALID_ARGUMENT;
    if (!path->path || path->path_length == 0) return VG_LITE_INVALID_ARGUMENT;
    
    buffer_internal_t *pattern_int = (buffer_internal_t *)pattern_image->handle;
    if (!pattern_int) return VG_LITE_INVALID_ARGUMENT;
    
    VlcPath vlc_path;
    vlc_path_init(&vlc_path);
    int cmd_count = vlc_parse_path(path, &vlc_path);
    if (cmd_count <= 0) {
        vlc_path_free(&vlc_path);
        return VG_LITE_INVALID_ARGUMENT;
    }
    
    TessGeometry geom;
    tess_geometry_init(&geom);
    int vtx_count = tessellate_path(&vlc_path, &geom);
    if (vtx_count <= 0 || geom.vertex_count == 0 || geom.index_count == 0) {
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return VG_LITE_SUCCESS;
    }
    
    VkFormat vkfmt = vg_lite_format_to_vk(target->format);
    VkPipeline pipeline = vg_lite_vulkan_get_pattern_pipeline(vkfmt, vg_lite_blend_to_group(blend));
    if (!pipeline) {
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return VG_LITE_OUT_OF_MEMORY;
    }
    
    VkBuffer vbo = VK_NULL_HANDLE, ibo = VK_NULL_HANDLE;
    VkDeviceMemory vbo_mem = VK_NULL_HANDLE, ibo_mem = VK_NULL_HANDLE;
    create_vertex_buffer(geom.vertex_count, geom.index_count, &vbo, &vbo_mem, &ibo, &ibo_mem);
    upload_geom(vbo, vbo_mem, ibo, ibo_mem, &geom);
    
    vg_lite_error_t err = vg_lite_vulkan_begin_command();
    if (err != VG_LITE_SUCCESS) {
        destroy_buffer(vbo, vbo_mem);
        destroy_buffer(ibo, ibo_mem);
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return err;
    }
    
    buffer_internal_t *target_int = (buffer_internal_t *)target->handle;
    if (g_vk_ctx.current_fb == VK_NULL_HANDLE || g_vk_ctx.current_fb_image != target_int->image) {
        err = vg_lite_vulkan_set_render_target(target);
        if (err != VG_LITE_SUCCESS) {
            destroy_buffer(vbo, vbo_mem);
            destroy_buffer(ibo, ibo_mem);
            tess_geometry_free(&geom);
            vlc_path_free(&vlc_path);
            return err;
        }
    }
    
    VkDescriptorSetAllocateInfo ds_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ds_alloc.descriptorPool = g_vk_ctx.descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = &g_vk_ctx.pattern_descriptor_layout;
    VkDescriptorSet desc_set;
    if (vkAllocateDescriptorSets(g_vk_ctx.device, &ds_alloc, &desc_set) != VK_SUCCESS) {
        destroy_buffer(vbo, vbo_mem);
        destroy_buffer(ibo, ibo_mem);
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return VG_LITE_OUT_OF_MEMORY;
    }
    
    VkSampler sampler = get_or_create_pattern_sampler();
    VkImageView pattern_view = pattern_int->swizzle_view ? pattern_int->swizzle_view : pattern_int->view;
    VkDescriptorImageInfo img_info = {sampler, pattern_view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet ws = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ws.dstSet = desc_set;
    ws.dstBinding = 0;
    ws.dstArrayElement = 0;
    ws.descriptorCount = 1;
    ws.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws.pImageInfo = &img_info;
    vkUpdateDescriptorSets(g_vk_ctx.device, 1, &ws, 0, NULL);
    
    float w = (float)target->width;
    float h = (float)target->height;
    float path_screen_to_ndc[3][3] = {{2.0f/w, 0, -1.0f}, {0, 2.0f/h, -1.0f}, {0, 0, 1.0f}};
    float path_combined[3][3] = {0};
    
    vg_lite_matrix_t identity = {0};
    vg_lite_identity(&identity);
    if (!path_matrix) path_matrix = &identity;
    
    mat3_multiply(path_screen_to_ndc, path_matrix->m, path_combined);
    
    if (!pattern_matrix) pattern_matrix = &identity;
    
    /* Compute inverse of pattern_matrix, then normalize by pattern dimensions.
     * This gives a matrix that transforms screen pixel coords to normalized UV [0,1].
     * (Following reference implementation in vg_lite_path.c lines 3355-3379) */
    float pattern_inv[3][3] = {0};
    float m[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) m[i][j] = pattern_matrix->m[i][j];
    
    if (!mat3_inverse(m, pattern_inv)) {
        pattern_inv[0][0] = 1.0f / pattern_image->width;
        pattern_inv[1][1] = 1.0f / pattern_image->height;
    } else {
        pattern_inv[0][0] /= pattern_image->width;
        pattern_inv[0][1] /= pattern_image->width;
        pattern_inv[0][2] /= pattern_image->width;
        pattern_inv[1][0] /= pattern_image->height;
        pattern_inv[1][1] /= pattern_image->height;
        pattern_inv[1][2] /= pattern_image->height;
    }
    
    struct {
        float path_m[12];
        float pattern_m[12];
        int pattern_mode;
        uint32_t pattern_color;
        int target_width;
        int target_height;
        int pattern_width;
        int pattern_height;
        int blend_mode;
    } pc_data = {0};
    
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
        pc_data.path_m[j*4+i] = path_combined[i][j];
        pc_data.pattern_m[j*4+i] = pattern_inv[i][j];  /* Use normalized inverse */
    }
    /* Convert vg_lite pattern mode to shader internal mode:
     * VG_LITE_PATTERN_COLOR   = 0x1D00 -> shader mode 0
     * VG_LITE_PATTERN_PAD     = 0x1D01 -> shader mode 1
     * VG_LITE_PATTERN_REPEAT  = 0x1D02 -> shader mode 2
     * VG_LITE_PATTERN_REFLECT = 0x1D03 -> shader mode 3
     */
    int shader_pattern_mode = 0;
    switch (pattern_mode) {
        case VG_LITE_PATTERN_COLOR:   shader_pattern_mode = 0; break;
        case VG_LITE_PATTERN_PAD:     shader_pattern_mode = 1; break;
        case VG_LITE_PATTERN_REPEAT:  shader_pattern_mode = 2; break;
        case VG_LITE_PATTERN_REFLECT: shader_pattern_mode = 3; break;
        default: shader_pattern_mode = 0; break;
    }
    
    pc_data.pattern_mode = shader_pattern_mode;
    pc_data.pattern_color = pattern_color;
    pc_data.target_width = target->width;
    pc_data.target_height = target->height;
    pc_data.pattern_width = pattern_image->width;
    pc_data.pattern_height = pattern_image->height;
    pc_data.blend_mode = (int)blend;
    
    vkCmdPushConstants(g_vk_ctx.cmd_buf, g_vk_ctx.pattern_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc_data), &pc_data);
    
    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_vk_ctx.pattern_pipeline_layout, 0, 1, &desc_set, 0, NULL);
    
    VkViewport vp = {0, 0, w, h, 0, 1};
    vkCmdSetViewport(g_vk_ctx.cmd_buf, 0, 1, &vp);
    vg_lite_vulkan_apply_scissor((uint32_t)w, (uint32_t)h);
    
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(g_vk_ctx.cmd_buf, 0, 1, &vbo, &offset);
    vkCmdBindIndexBuffer(g_vk_ctx.cmd_buf, ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(g_vk_ctx.cmd_buf, geom.index_count, 1, 0, 0, 0);
    
    tess_geometry_free(&geom);
    vlc_path_free(&vlc_path);
    add_pending_buffer(vbo, vbo_mem, ibo, ibo_mem);
    
    return VG_LITE_SUCCESS;
}

static vg_lite_error_t draw_grad_internal(
    vg_lite_buffer_t *target,
    vg_lite_path_t *path,
    vg_lite_fill_t fill_rule,
    vg_lite_matrix_t *matrix,
    vg_lite_buffer_t *grad_image,
    vg_lite_matrix_t *grad_matrix,
    vg_lite_blend_t blend,
    int pattern_mode,
    vg_lite_color_t pattern_color)
{
    if (!target || !path || !grad_image) return VG_LITE_INVALID_ARGUMENT;
    if (!path->path || path->path_length == 0) return VG_LITE_INVALID_ARGUMENT;
    (void)pattern_color; /* unused — no COLOR mode in gradient shader */

    buffer_internal_t *grad_int = (buffer_internal_t *)grad_image->handle;
    if (!grad_int) return VG_LITE_INVALID_ARGUMENT;

    VlcPath vlc_path;
    vlc_path_init(&vlc_path);
    int cmd_count = vlc_parse_path(path, &vlc_path);
    if (cmd_count <= 0) {
        vlc_path_free(&vlc_path);
        return VG_LITE_INVALID_ARGUMENT;
    }

    TessGeometry geom;
    tess_geometry_init(&geom);
    int vtx_count = tessellate_path(&vlc_path, &geom);
    if (vtx_count <= 0 || geom.vertex_count == 0 || geom.index_count == 0) {
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return VG_LITE_SUCCESS;
    }

    VkFormat vkfmt = vg_lite_format_to_vk(target->format);
    vg_lite_vulkan_init_grad_pipeline(vkfmt);

    VkBuffer vbo = VK_NULL_HANDLE, ibo = VK_NULL_HANDLE;
    VkDeviceMemory vbo_mem = VK_NULL_HANDLE, ibo_mem = VK_NULL_HANDLE;
    create_vertex_buffer(geom.vertex_count, geom.index_count, &vbo, &vbo_mem, &ibo, &ibo_mem);
    upload_geom(vbo, vbo_mem, ibo, ibo_mem, &geom);

    vg_lite_error_t err = vg_lite_vulkan_begin_command();
    if (err != VG_LITE_SUCCESS) {
        destroy_buffer(vbo, vbo_mem);
        destroy_buffer(ibo, ibo_mem);
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return err;
    }

    buffer_internal_t *internal = (buffer_internal_t *)target->handle;
    if (g_vk_ctx.current_fb == VK_NULL_HANDLE || g_vk_ctx.current_fb_image != internal->image) {
        err = vg_lite_vulkan_set_render_target(target);
        if (err != VG_LITE_SUCCESS) {
            destroy_buffer(vbo, vbo_mem);
            destroy_buffer(ibo, ibo_mem);
            tess_geometry_free(&geom);
            vlc_path_free(&vlc_path);
            return err;
        }
    }

    VkViewport vp = {0, 0, (float)target->width, (float)target->height, 0, 1};
    vkCmdSetViewport(g_vk_ctx.cmd_buf, 0, 1, &vp);
    vg_lite_vulkan_apply_scissor(target->width, target->height);

    float w = (float)target->width;
    float h = (float)target->height;
    float screen_to_ndc[3][3] = {{2.0f/w, 0, -1.0f}, {0, 2.0f/h, -1.0f}, {0, 0, 1.0f}};
    float path_combined[3][3] = {0};

    vg_lite_matrix_t identity = {0};
    vg_lite_identity(&identity);
    if (!matrix) matrix = &identity;
    mat3_multiply(screen_to_ndc, matrix->m, path_combined);

    /* Push constant struct: matches gradient.vert/frag layout.
     * mat3 stored as 3 vec4 columns (std140), path_m[j*4+i] = path_combined[i][j] */
    struct {
        float path_m[12];
        float grad_m[12];
        int   target_width;
        int   target_height;
        int   grad_width;
        int   grad_height;
        int   spread_mode;  /* 0=FILL, 1=PAD, 2=REPEAT, 3=REFLECT */
    } pc_data = {0};

    pc_data.target_width = target->width;
    pc_data.target_height = target->height;
    pc_data.grad_width = grad_image->width;
    pc_data.grad_height = grad_image->height;
    pc_data.spread_mode = pattern_mode;

    /* Stencil pass push constants: path_matrix = screen_to_ndc * user_matrix */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            pc_data.path_m[j*4+i] = path_combined[i][j];

    vkCmdPushConstants(g_vk_ctx.cmd_buf, g_vk_ctx.grad_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc_data), &pc_data);

    VkDescriptorSetAllocateInfo ds_alloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ds_alloc.descriptorPool = g_vk_ctx.descriptor_pool;
    ds_alloc.descriptorSetCount = 1;
    ds_alloc.pSetLayouts = &g_vk_ctx.grad_descriptor_layout;
    VkDescriptorSet desc_set;
    if (vkAllocateDescriptorSets(g_vk_ctx.device, &ds_alloc, &desc_set) != VK_SUCCESS) {
        destroy_buffer(vbo, vbo_mem);
        destroy_buffer(ibo, ibo_mem);
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkSampler sampler = get_or_create_pattern_sampler();
    VkImageView grad_view = grad_int->swizzle_view ? grad_int->swizzle_view : grad_int->view;
    VkDescriptorImageInfo img_info = {sampler, grad_view, VK_IMAGE_LAYOUT_GENERAL};

    VkWriteDescriptorSet ws = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    ws.dstSet = desc_set;
    ws.dstBinding = 0;
    ws.dstArrayElement = 0;
    ws.descriptorCount = 1;
    ws.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws.pImageInfo = &img_info;
    vkUpdateDescriptorSets(g_vk_ctx.device, 1, &ws, 0, NULL);

    vkCmdBindDescriptorSets(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g_vk_ctx.grad_pipeline_layout, 0, 1, &desc_set, 0, NULL);

    VkDeviceSize vbo_offset = 0;
    vkCmdBindVertexBuffers(g_vk_ctx.cmd_buf, 0, 1, &vbo, &vbo_offset);
    vkCmdBindIndexBuffer(g_vk_ctx.cmd_buf, ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk_ctx.grad_stencil_pipeline);
    vkCmdDrawIndexed(g_vk_ctx.cmd_buf, geom.index_count, 1, 0, 0, 0);

    /* Cover pass: path_matrix = identity, grad_matrix maps screen pixel coords to texture UV.
     * Chain: screen_pixel -> path_coord (via inverse path_matrix) -> texture UV (via inverse grad_matrix / tex_size) */

    if (!grad_matrix) grad_matrix = &identity;

    float mat_inv[3][3] = {0};
    if (!mat3_inverse(matrix->m, mat_inv)) {
        mat_inv[0][0] = 1.0f; mat_inv[1][1] = 1.0f; mat_inv[2][2] = 1.0f;
    }

    float grad_inv[3][3] = {0};
    float gm[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) gm[i][j] = grad_matrix->m[i][j];
    if (!mat3_inverse(gm, grad_inv)) {
        grad_inv[0][0] = 1.0f; grad_inv[1][1] = 1.0f; grad_inv[2][2] = 1.0f;
    }

    float grad_norm[3][3] = {0};
    grad_norm[0][0] = grad_inv[0][0] / grad_image->width;
    grad_norm[0][1] = grad_inv[0][1] / grad_image->width;
    grad_norm[0][2] = grad_inv[0][2] / grad_image->width;
    grad_norm[1][0] = grad_inv[1][0] / grad_image->height;
    grad_norm[1][1] = grad_inv[1][1] / grad_image->height;
    grad_norm[1][2] = grad_inv[1][2] / grad_image->height;
    grad_norm[2][2] = 1.0f;

    float combined_pattern[3][3] = {0};
    mat3_multiply(grad_norm, mat_inv, combined_pattern);

    /* Cover pass push constants: path_matrix = identity, grad_matrix = combined_pattern */
    memset(&pc_data, 0, sizeof(pc_data));
    pc_data.target_width = target->width;
    pc_data.target_height = target->height;
    pc_data.grad_width = grad_image->width;
    pc_data.grad_height = grad_image->height;
    pc_data.spread_mode = pattern_mode;
    pc_data.path_m[0] = 1.0f;  /* identity col0 */
    pc_data.path_m[5] = 1.0f;  /* identity col1 */
    pc_data.path_m[10] = 1.0f; /* identity col2 */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            pc_data.grad_m[j*4+i] = combined_pattern[i][j];

    vkCmdPushConstants(g_vk_ctx.cmd_buf, g_vk_ctx.grad_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc_data), &pc_data);

    VkPipeline cover_pipeline = vg_lite_vulkan_get_grad_cover_pipeline(vkfmt, vg_lite_blend_to_group(blend));
    if (!cover_pipeline) {
        tess_geometry_free(&geom);
        vlc_path_free(&vlc_path);
        add_pending_buffer(vbo, vbo_mem, ibo, ibo_mem);
        return VG_LITE_OUT_OF_MEMORY;
    }

    VkDeviceSize cover_offset = 0;
    vkCmdBindVertexBuffers(g_vk_ctx.cmd_buf, 0, 1, &g_vk_ctx.grad_cover_vbo, &cover_offset);
    vkCmdBindIndexBuffer(g_vk_ctx.cmd_buf, g_vk_ctx.grad_cover_ibo, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, cover_pipeline);
    vkCmdDrawIndexed(g_vk_ctx.cmd_buf, 6, 1, 0, 0, 0);

    tess_geometry_free(&geom);
    vlc_path_free(&vlc_path);
    add_pending_buffer(vbo, vbo_mem, ibo, ibo_mem);

    return VG_LITE_SUCCESS;
}

vg_lite_error_t vg_lite_draw_grad(vg_lite_buffer_t *target,
                                   vg_lite_path_t *path,
                                   vg_lite_fill_t fill_rule,
                                   vg_lite_matrix_t *matrix,
                                   vg_lite_linear_gradient_t *grad,
                                   vg_lite_blend_t blend)
{
    if (!grad || !grad->image.handle) return VG_LITE_INVALID_ARGUMENT;
    return draw_grad_internal(target, path, fill_rule, matrix,
                              &grad->image, &grad->matrix, blend, 1, 0);
}

vg_lite_error_t vg_lite_draw_radial_grad(vg_lite_buffer_t *target,
                                          vg_lite_path_t *path,
                                          vg_lite_fill_t fill_rule,
                                          vg_lite_matrix_t *matrix,
                                          vg_lite_radial_gradient_t *grad,
                                          vg_lite_color_t paint_color,
                                          vg_lite_blend_t blend,
                                          vg_lite_filter_t filter)
{
    if (!grad || !grad->image.handle) return VG_LITE_INVALID_ARGUMENT;
    (void)paint_color; (void)filter;

    int shader_mode = 1;
    switch (grad->spread_mode) {
        case VG_LITE_GRADIENT_SPREAD_FILL:    shader_mode = 1; break;
        case VG_LITE_GRADIENT_SPREAD_PAD:     shader_mode = 1; break;
        case VG_LITE_GRADIENT_SPREAD_REPEAT:  shader_mode = 2; break;
        case VG_LITE_GRADIENT_SPREAD_REFLECT: shader_mode = 3; break;
        default: shader_mode = 1; break;
    }
    return draw_grad_internal(target, path, fill_rule, matrix,
                              &grad->image, &grad->matrix, blend, shader_mode, paint_color);
}