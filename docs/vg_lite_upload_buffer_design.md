# vg_lite_upload_buffer 计算着色器（Compute Shader）实现设计文档

> 对应代码：
> - `src/vg_lite_upload.c` — CPU 侧实现（管线、staging、描述符、屏障、命令记录）
> - `shaders/upload.comp` — LINEAR 路径 compute shader
> - `shaders/upload_tiled.comp` — TILED（OPTIMAL）路径 compute shader
> - `src/vg_lite_vulkan.h/.c` — 管线缓存字段、设备特性开启、资源销毁
> - `src/vg_lite.c` — `vg_lite_allocate` 中 TILED buffer 的创建（R16_UINT 兼容格式 + MUTABLE_FORMAT + STORAGE usage）
> - 测试：`tests/uploadBuffer/uploadBuffer.c`（线性）、`tests/uploadTiled/uploadTiled.c`（tiled == linear 逐字节对比）

---

## 1. 功能概述

`vg_lite_upload_buffer`（宏别名 `vg_lite_buffer_upload`）把用户侧的像素数据上传到一块已通过 `vg_lite_allocate` 分配的 `vg_lite_buffer` 中：

```c
vg_lite_error_t vg_lite_upload_buffer(vg_lite_buffer_t *buffer,
                                      vg_lite_uint8_t *data[3],
                                      vg_lite_uint32_t stride[3]);
```

- `data[3] / stride[3]`：最多 3 个平面的数据指针与行跨度（本实现仅支持 plane 0）。
- 参考实现（gpu-vglite `vg_lite_image.c`）是**逐行 CPU memcpy**；tiled 直接返回 `VG_LITE_INVALID_ARGUMENT`。

本实现的差异化：**两条 GPU compute shader 上传路径**，用户数据一次进入 staging 后由 GPU 完成 strided 拷贝 / 像素散射：

| 目标 buffer | 路径 | 使用的 shader | 目标写入方式 |
|---|---|---|---|
| `VG_LITE_LINEAR`（线性 image，HOST_VISIBLE 持久映射） | `upload_buffer_linear` | `shaders/upload.comp` | 把 image 的 `VkDeviceMemory` **别名绑定为 STORAGE_BUFFER**，shader 按 subresource offset/rowPitch 散射 u32 |
| `VG_LITE_TILED`（OPTIMAL tiling，device-local 不映射） | `upload_buffer_tiled` | `shaders/upload_tiled.comp` | 通过 **无格式（formatless）`uimage2D`** 写入原始像素位模式，tile 寻址交由硬件 |

两条路径共享同一套基础设施：惰性缓存的 compute pipeline、HOST_VISIBLE+COHERENT staging SSBO、从全局 descriptor pool 分配的一次性 descriptor set、以及 submit/begin 同步骨架。

---

## 2. 公共入口与参数校验（`vg_lite_upload_buffer`）

```
vg_lite_upload_buffer(buffer, data[3], stride[3])
  ├─ !buffer / !data / !data[0] / !stride            → VG_LITE_INVALID_ARGUMENT
  ├─ !internal / width==0 / height==0                → VG_LITE_INVALID_ARGUMENT
  ├─ data[1] || data[2]（多平面 YUV）                  → VG_LITE_NOT_SUPPORT
  ├─ vg_lite_is_yuv_format(buffer->format)            → VG_LITE_NOT_SUPPORT
  ├─ row_bytes = (width * bpp_bits + 7) / 8
  ├─ data_stride = stride[0] ? stride[0] : row_bytes
  ├─ data_stride < row_bytes                          → VG_LITE_INVALID_ARGUMENT
  └─ 路由：
       tiled == VG_LITE_TILED → bpp 32/16/8 → upload_buffer_tiled(…, 4/2/1)
                                其它 bpp     → VG_LITE_NOT_SUPPORT
       否则                   → upload_buffer_linear
```

要点：

- `data_stride` 允许与 `row_bytes` 不同（用户行跨度可含 padding），这是 compute 路径存在意义之一 —— shader 内部按"行紧打包"的 staging 重新组织，跨度和对齐问题全部在 CPU 打包阶段和 push constant 中解决。
- 不支持项：多平面 YUV、sub-byte 格式（A4/INDEX_1/2/4 的 tiled 路径）、tiled 且 bpp 不属于 {1,2,4} 字节。

---

## 3. 公共基础设施

### 3.1 管线惰性缓存（`get_upload_pipeline` / `get_upload_tiled_pipeline`）

两条路径各自的 pipeline / pipeline layout / descriptor set layout 缓存在全局 `vk_context_t`（`vg_lite_vulkan.h`）：

```c
/* LINEAR */
VkPipelineLayout upload_pipeline_layout;
VkDescriptorSetLayout upload_descriptor_layout;
VkPipeline        upload_pipeline;
/* TILED */
VkPipelineLayout        upload_tiled_pipeline_layout;
VkDescriptorSetLayout   upload_tiled_descriptor_layout;
VkPipeline              upload_tiled_pipeline;
int                     storage_image_wo_format;   /* 特性探测结果 */
```

首次调用时创建，之后直接命中缓存（`*pipeline != VK_NULL_HANDLE` 早退分支**同时回填 layout/desc_layout 两个出参** —— 这是对一次真实崩溃的修复：调用方未初始化局部变量时早退分支不回填会导致 NULL layout 进 `vkAllocateDescriptorSets`，在 Intel 驱动内崩溃）。`destroy_pipelines`（vg_lite_vulkan.c）统一销毁这 6 个对象。

两者结构完全相同，只有 descriptor 绑定和 shader 不同：

| 项 | LINEAR | TILED |
|---|---|---|
| binding 0 | `STORAGE_BUFFER`（src staging） | `STORAGE_BUFFER`（src staging） |
| binding 1 | `STORAGE_BUFFER`（dst = image 内存别名 buffer） | `STORAGE_IMAGE`（dst = 无格式 uimage2D） |
| push constant | 16 B，COMPUTE stage | 16 B，COMPUTE stage |
| SPIR-V | `upload_comp`（由顶层 CMake `*.comp` glob 编译到 `build/spv/`） | `upload_tiled_comp` |

shader module 在创建 pipeline 后立即销毁（SPIR-V 已被 pipeline 持有）。

### 3.2 Staging SSBO（`staging_t`）

```c
typedef struct { VkBuffer buffer; VkDeviceMemory memory; void *mapped; VkDeviceSize size; } staging_t;
```

- usage：`VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
- 内存：`HOST_VISIBLE | HOST_COHERENT`（COHERENT 使 CPU 写入对随后提交的 compute 可见，无需显式 flush；提交前仍加 HOST→SHADER_READ buffer barrier 做正式的可用性/可见性交接）
- 创建即 `vkMapMemory(0, WHOLE_SIZE)` 持久映射；`staging_destroy` 逆序 unmap/free/destroy
- 每次上传调用一次性创建、用完即毁（后续可优化为池化）

### 3.3 描述符集分配（`alloc_desc_set`）

从 `g_vk_ctx.descriptor_pool`（带 `FREE_DESCRIPTOR_SET_BIT`）分配 1 个 set，用完 `vkFreeDescriptorSets` 归还。为支持 storage image，池容量中包含 `{STORAGE_IMAGE, 16}` 与 `{UNIFORM_TEXEL_BUFFER, 16}`。

### 3.4 命令缓冲与同步骨架

两条路径共用同一模式：

```
vg_lite_vulkan_flush_render_pass();      /* 若有进行中的 render pass 先收尾 */
vg_lite_vulkan_submit_command(1);        /* 提交并等 fence（参数 1 = 等待完成） */
vg_lite_vulkan_begin_command();          /* 重新 begin 命令缓冲 */
  ... barriers + bind + dispatch + barrier ...
vg_lite_vulkan_submit_command(1);        /* 提交并等待完成 */
```

提交前的 `submit_command(1)`（fence 等待）对 LINEAR 路径**还有别名安全性意义**：image 内存即将被一个新 buffer 别名绑定，必须保证没有在飞的使用。提交后的等待保证函数返回时上传已完成（tiled buffer 无 host 映射，调用方无法自行判断；linear buffer 调用方可能立即读映射内存）。

---

## 4. LINEAR 路径详细设计

### 4.1 目标 buffer 的形态

`vg_lite_allocate` 线性路径创建的 image：`VK_IMAGE_TILING_LINEAR`、HOST_VISIBLE|COHERENT 内存、持久映射（`buffer->memory` = 映射基址 + subresource offset）、usage 含 COLOR_ATTACHMENT|SAMPLED|TRANSFER_SRC|TRANSFER_DST。子资源布局（offset/rowPitch）可通过 `vkGetImageSubresourceLayout` 查询 —— rowPitch 通常 4 字节对齐（实测 BGRA8888 128 宽 = 512，RGB565 127 宽 = 256，L8 127 宽 = 128），但对齐性不做假设，不对齐则 CPU 兜底。

### 4.2 数据流

```
用户数据 (data, data_stride，任意字节对齐的行跨度)
   │ CPU 逐行 memcpy（去掉用户 padding，行尾补 0 到 4B 的倍数）
   ▼
staging SSBO（行紧打包: row_words*4 字节/行）
   │ compute: 每个 invocation 拷一个 u32
   ▼
dst 别名 buffer（vkBindBufferMemory(dst_buf, internal->memory, 0)，
   覆盖整个 image 内存；shader 按 offset + y*rowPitch + x 寻址）
   ═══ 物理上就是 linear image 的像素内存 ═══
```

### 4.3 CPU 侧步骤（`upload_buffer_linear`）

1. `row_words = ceil(row_bytes/4)`；`staging_size = row_words*4*height`。
2. `vkGetImageSubresourceLayout` 取 `offset/rowPitch`；若 `offset%4 || rowPitch%4` → `upload_buffer_cpu`（逐行 memcpy 进 `buffer->memory`，按 `buffer->stride`）。
3. `get_upload_pipeline` 失败 → CPU 兜底。
4. `staging_create` 失败 → CPU 兜底。
5. staging 打包：整块 `memset 0` 后逐行 `memcpy(row_bytes)`（行尾 padding 字节保持为 0）。
6. **别名前置检查（关键修复点）**：
   ```c
   vkGetImageMemoryRequirements(image, &img_req);
   if (img_req.size < staging_size ||
       layout.offset + layout.rowPitch*(height-1) + row_bytes > img_req.size ||
       internal->memory == VK_NULL_HANDLE)
       → CPU 兜底
   ```
   早期版本把别名 buffer 大小设成 `staging_size`，当 `rowPitch > row_bytes`（如 L8 96 宽、rowPitch=128）时 shader 散射写越过 buffer 边界被驱动丢弃，导致**底部若干行全零**。修复为：别名 buffer 尺寸 = `img_req.size`（覆盖整个 image 内存），并在创建前做上面的越界预检查。
7. 创建别名 buffer（usage STORAGE_BUFFER，size = img_req.size）并 `vkBindBufferMemory(dst_buf, internal->memory, 0)`。
8. 分配 descriptor set，写两个 `VkDescriptorBufferInfo`（均 offset 0 / range WHOLE_SIZE）。
9. 同步骨架 → 记录命令 → 提交等待 → 释放（free desc set、destroy dst_buf、destroy staging）。

### 4.4 shader（`shaders/upload.comp`，完整逻辑）

```glsl
layout(local_size_x = 64) in;                 // 每行 64 个 u32 一组

layout(std430, binding = 0) readonly buffer SrcBuf { uint src[]; };
layout(std430, binding = 1) buffer DstBuf { uint dst[]; };

layout(push_constant) uniform PushParams {    // 16 B
    uint row_words;        // 源行长度（u32 数）
    uint dst_stride_bytes; // 目标 rowPitch（字节，4 的倍数）
    uint dst_offset_words; // 目标 subresource offset（u32 数）
    uint height;
} p;

void main() {
    uint i = gl_GlobalInvocationID.x;   // 行内 u32 下标
    uint y = gl_GlobalInvocationID.y;   // 行号
    if (y >= p.height || i >= p.row_words) return;
    dst[p.dst_offset_words + y*(p.dst_stride_bytes/4u) + i] = src[y*p.row_words + i];
}
```

- 一个 invocation 拷一个 u32；dispatch 维度 `(ceil(row_words/64), height, 1)`。
- 目标寻址完全由 push constant 驱动，shader 对 image 布局零假设 —— subresource 的 offset/rowPitch 换个硬件/驱动也不需要改 shader。
- 边界检查防止 dispatch 向上取整产生的越界 invocation 写坏 image 内存外的字。

### 4.5 屏障

```
[pre]  buffer barrier: HOST_WRITE → SHADER_READ        (HOST → COMPUTE_SHADER)
[post] buffer barrier: SHADER_WRITE → MEMORY_READ|WRITE (COMPUTE_SHADER → ALL_COMMANDS)
```

post barrier 让后续任意使用者（采样、attachment、host 读映射内存）都能看到上传结果。

### 4.6 CPU 兜底（`upload_buffer_cpu`）

```c
for (y = 0; y < height; y++)
    memcpy((uint8_t*)buffer->memory + y*buffer->stride,
           data + y*data_stride, row_bytes);
```

仅在布局不对齐 / 管线或 staging 创建失败时进入；正常硬件路径下不可达（staging buffer 要求 host-visible 内存类型是 Vulkan 规范保证，总能满足）。

---

## 5. TILED 路径详细设计

### 5.1 为什么不能用 LINEAR 的别名方案

OPTIMAL tiling 的 image 内存是驱动私有的 swizzle 布局：没有可解释的 subresource rowPitch，直接按线性 offset 写会破坏 tile 结构。gpu-vglite 对 tiled 直接拒绝；本实现改用 **storage image + imageStore**，tile 寻址交由硬件。

### 5.2 单 shader + 无格式 imageStore 的设计取舍

演化过程（背景）：

1. 最初方案：texelFetch 从 texel buffer 读 + 带格式 imageStore 写。**问题**：GLSL storage image 的 format 限定符只有按位宽的整数/浮点集合，没有 `bgra8`、`r5g6b5` 这类格式；要覆盖 BGRA8888/L8/RGB565 需要两个 shader（rgba8/r8 变体）且 RGB565 无法表达。
2. **formatless-integer 技巧**：`uimage2D` **不带 format 限定符**（依赖特性 `shaderStorageImageWriteWithoutFormat`），image view 用纯整数格式 `R32_UINT / R16_UINT / R8_UINT`（按字节/像素选择），shader 写入的是**原始像素位模式** —— 不做任何颜色解释，颜色语义完全留给采样时的格式 view。这样一个 shader 覆盖全部 {4,2,1} BPP。

### 5.3 前置条件（不满足返回 NOT_SUPPORT）

1. 设备特性 `shaderStorageImageWriteWithoutFormat`（设备创建时通过 `VkPhysicalDeviceFeatures2` 链探测并按需开启，结果记录在 `g_vk_ctx.storage_image_wo_format`；不支持时打 WARNING 并禁用本路径）。
2. 所选 view 格式（R32/R16/R8_UINT）在 `optimalTilingFeatures` 中含 `STORAGE_IMAGE_BIT`（`vkGetPhysicalDeviceFormatProperties` 查询）。
3. bpp ∈ {4, 2, 1} 字节。

### 5.4 16bpp 的 image 创建配合（vg_lite_allocate）

部分驱动（含本机 Intel）不支持 `B5G6R5_UNORM_PACK16` 的 OPTIMAL+STORAGE+MUTABLE 组合（`vkGetPhysicalDeviceImageFormatProperties` 拒绝）。解决：**tiled 16bpp 的 image 直接以 `VK_FORMAT_R16_UINT` 创建**，因为 R16_UINT 与 B5G6R5 同属 16-bit packed 格式兼容类（format compatibility class，同 size 且同为 packed 16-bit），配合 image 的 `VK_FORMAT_CREATE_MUTABLE_FORMAT_BIT` 标志，可以用 B5G6R5 格式的 view 去采样、用 R16_UINT 的 view 去 storage 写。tiled image 额外的创建参数：

- tiling OPTIMAL；usage 追加 `STORAGE`；flags `MUTABLE_FORMAT`
- 内存优先 DEVICE_LOCAL（失败回退任意类型）；不映射（`buffer->memory = NULL`）
- 拒绝 YUV / INDEX_1/2/4（A4 经远端合并后由 shadow buffer + staging 路径支持，不走本路径）

### 5.5 CPU 侧步骤（`upload_buffer_tiled`）

1. 选 view_fmt（4→R32_UINT，2→R16_UINT，1→R8_UINT），检查特性。
2. `get_upload_tiled_pipeline`。
3. staging 打包：`row_padded = (width*bpp + 3) & ~3`，整块清零后逐行 memcpy `row_bytes`（行尾 0 padding，保证源侧 u32 访问不越界且确定性）。
4. **瞬态 dst view**：对 `internal->image` 创建 `VkImageView`（格式 = view_fmt，2D，mip 0 / layer 0），每次调用创建、用完销毁。
5. descriptor：binding 0 = `VkDescriptorBufferInfo{staging, 0, WHOLE_SIZE}`；binding 1 = `VkDescriptorImageInfo{NULL, dst_view, GENERAL}`。
6. 同步骨架（同 LINEAR）→ 命令记录 → 提交等待 → 释放。

### 5.6 屏障

```
[pre]  buffer barrier: HOST_WRITE → SHADER_READ                       (HOST → COMPUTE)
       image  barrier: MEMORY_R|W → SHADER_WRITE, GENERAL → GENERAL   (ALL_COMMANDS → COMPUTE)
[post] image  barrier: SHADER_WRITE → MEMORY_READ|WRITE, GENERAL→GENERAL (COMPUTE → ALL_COMMANDS)
```

本引擎所有 image 的生命周期布局是 allocate 时 UNDEFINED→GENERAL 后**恒为 GENERAL**（采样、attachment、resolve、storage 写全部在 GENERAL 下进行），所以两个 image barrier 都是 GENERAL→GENERAL 的纯所有权/可见性屏障，不做布局转换。

### 5.7 shader（`shaders/upload_tiled.comp`，完整逻辑）

```glsl
layout(local_size_x = 8, local_size_y = 8) in;      // 8x8 tile 式的二维工作组

layout(std430, binding = 0) readonly buffer SrcBuf { uint src[]; };
layout(binding = 1) writeonly uniform uimage2D dstImage;   // 无 format 限定符！

layout(push_constant) uniform PushParams {          // 16 B
    uint width; uint height; uint bytes_per_pixel; uint pad;
} p;

void main() {
    uint x = gl_GlobalInvocationID.x, y = gl_GlobalInvocationID.y;
    if (x >= p.width || y >= p.height) return;

    uint row_padded = (p.width * p.bytes_per_pixel + 3u) & ~3u;
    uint byte_off = y * row_padded + x * p.bytes_per_pixel;
    uint v;
    if (p.bytes_per_pixel == 4u) {          // 一个 u32 一个像素
        v = src[byte_off >> 2u];
    } else if (p.bytes_per_pixel == 2u) {   // 半字：按 word 内奇偶选择
        uint w = src[byte_off >> 2u];
        v = ((byte_off >> 1u) & 1u) != 0u ? (w >> 16) : (w & 0xffffu);
    } else {                                // 单字节：按 word 内字节位置选择
        uint w = src[byte_off >> 2u];
        v = (w >> ((byte_off & 3u) * 8u)) & 0xffu;
    }
    imageStore(dstImage, ivec2(int(x), int(y)), uvec4(v, 0u, 0u, 0u));
}
```

- dispatch 维度 `(ceil(width/8), ceil(height/8), 1)`，一个 invocation 处理一个**像素**（不是字节）。
- 源侧永远以 `uint[]` 读取，再按 bpp 提取 —— 这是"formatless"思想的另一半：**源也没有格式**，只有字节流。
- `imageStore` 写 `uvec4(v,0,0,0)`：对 R32/R16/R8_UINT view，只有第一个分量有效，写入的是像素的原始位模式。
- 寻址用 `y*row_padded` 而非 `y*width*bpp`：行紧打包到 4B 是 CPU 打包的既定事实，shader 与之严格一致。

### 5.8 替代方案（未采用，备查）

`vkCmdCopyBufferToImage` + TRANSFER usage：对 linear/optimal 都可移植，无需 storage 特性；但需要额外的 layout 转换屏障和 buffer-rowpitch 对齐处理（rowPitch 非 4 对齐时需要 texel copy 路径），且失去了本方案"与采样路径同一套 GENERAL 布局"的简化。DIY swizzle（按 Mesa freedreno/panfrost 的公开 tile 布局自己算地址）仅对布局已文档化的驱动可行，不予考虑。

---

## 6. 设备特性开启（vg_lite_vulkan.c）

设备创建使用 `VkPhysicalDeviceFeatures2` 链：

```
device_ci.pNext → &enable_feat2 (VkPhysicalDeviceFeatures2)
                    enable_feat2.pNext → &vk12_features (原有 1.2 特性链)
enable_feat2.features.shaderStorageImageWriteWithoutFormat =
    pd_features2.features.shaderStorageImageWriteWithoutFormat ? VK_TRUE : VK_FALSE;
```

- 只有物理设备支持时才开启（开启不支持的特性是验证层错误/未定义行为）。
- 创建后记录 `g_vk_ctx.storage_image_wo_format`；不支持时 tiled 路径整体禁用（NOT_SUPPORT），并打印 WARNING。
- 注意：扩展回退路径（无 scalarBlockLayout 核心特性时）不链 `enable_feat2`，该路径下 tiled 上传同样不可用。

---

## 7. 资源生命周期与错误处理

每次 `vg_lite_upload_buffer` GPU 路径的临时资源与释放顺序：

| 资源 | 创建 | 释放 |
|---|---|---|
| staging buffer + memory + mapping | 每次调用 | 提交完成后 `staging_destroy`（unmap → free memory → destroy buffer） |
| LINEAR：dst 别名 buffer | 每次调用 | 提交完成后 destroy（**注意**：不 free memory，那是 image 的） |
| TILED：dst storage image view | 每次调用 | 提交完成后 destroy |
| descriptor set | 每次调用 | `vkFreeDescriptorSets` 归还全局池 |
| pipeline / layouts | 首次调用 | `destroy_pipelines`（引擎关闭时） |

所有失败路径走统一的 `*_fail` 标签清理（或就地清理后 CPU 兜底），无泄漏；早期一个 `vkMapMemory` 失败路径的泄漏已在开发中修复。descriptor set 在提交并等待 fence 之后才释放，保证 GPU 不再引用。

---

## 8. 测试与验证

### 8.1 tests/uploadBuffer（线性路径）

- 3 个用例：BGRA8888 128×96（row 512，天然对齐）、RGB565 127×95（row 254，非对齐）、L8 127×95（row 127，非对齐）。
- 源数据 `row[i] = i*7 + y*13 + 0x5A`，**故意使用奇数 user_stride = row_bytes+13**，强制走 strided 路径，防止"整块 memcpy 恰好通过"的假阳性。
- 上传后（fence 已等待、host 映射可见）逐行 `memcmp(buf.memory + y*stride, src + y*user_stride, row_bytes)`，失败打印前 4 个字节差异；再 clear+blit 出图 `uploadBuffer_<fmt>_output.png` 做视觉 sanity。
- 结果：3/3 走 compute 路径（无 CPU 兜底）通过；实测 dst rowPitch 512/256/128。

### 8.2 tests/uploadTiled（tiled 路径 == 线性路径）

- 4 个用例：bgra8888 / rgba8888 / rgb565 / l8（rgb565 覆盖 16bpp 的 R16_UINT 兼容方案）。
- 每个用例：同一份源数据（奇数 user_stride）分别上传到 linear buffer 与 tiled buffer，各自 blit 到 BGRA8888 target（**每个 blit 后单独 `vg_lite_finish()`** —— 批量两个 blit 一次 finish 会丢第一个，是已记录的疑似 blit 批处理 bug，与本功能无关），读回逐字节对比，出图 `uploadTiled_<fmt>_lin.png/_tiled.png`。
- 结果：4/4 通过，tiled 与 linear 逐字节一致。

### 8.3 回归

全量测试套件（从 `build/tests/` 目录运行）：41 PASS / 2 FAIL（test_gfx3、test_imgIndex，本机预存在）/ 1 CRASH（test_sft_blit，预存在），与改动前基线一致，无回归。

---

## 9. 已知限制与后续方向

- 多平面 YUV（NV12/YV12 等）与 sub-byte 格式（A4、INDEX_1/2/4）不支持（gpu-vglite 参考实现同样拒绝多平面）。
- TILED 路径依赖 `shaderStorageImageWriteWithoutFormat`；主流桌面/移动驱动普遍支持，但极老设备会落到 NOT_SUPPORT（可考虑加 CopyBufferToImage 备胎路径）。
- staging 每次调用分配/释放，未池化；大图上传可优化为复用 staging 或拆分多次 dispatch。
- CPU 兜底分支正常情况下不可达，仅作防御。
- 相关已修 bug 记录见 `FIXES.md`：
  1. 管线缓存早退分支不回填出参 → NULL descriptor layout 崩溃（Intel igvk64.dll）。
  2. LINEAR 别名 buffer 尺寸不足（rowPitch > row_bytes 时底部行丢失）。
