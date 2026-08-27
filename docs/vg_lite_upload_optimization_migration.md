# vg_lite_upload_buffer 优化轮次总结（代码迁移指南）

> 覆盖范围：自 `vg_lite_upload_buffer_design.md`（初版设计）之后的所有优化轮次，
> 对应提交 `16704d6` / `fbb888b` / `7e46343`。
> 目标读者：要把这套实现迁移到其他 Vulkan 代码库的工程师。

---

## 0. 一页速览：优化前后对比

| 维度 | 优化前（初版） | 优化后（本轮） |
|---|---|---|
| 上传路径数 | 3 条手写路径（别名 compute / copy / cpu memcpy） | 1 条 compute + 1 条复用 `vg_lite_buffer_write` 的通用路径 |
| compute shader 源端读取 | SSBO + 手工 ALU 字节拆包（移位/掩码/奇偶判断） | `texelFetch(usamplerBuffer)`，专用取数单元拆字节，shader 零 ALU |
| push 常量 | 16 字节 `{row_words, dst_stride, dst_offset, height}` / `{w,h,bpp,pad}` | 8 字节 `{width, height}` |
| staging 行布局 | 按 4 字节对齐 padding | 完全紧凑（texel index = y*width + x），bpp<4 时 staging 更小 |
| TILED 分配兼容性 | OPTIMAL+STORAGE 不支持直接 NOT_SUPPORT | 三级探测链：STORAGE → base OPTIMAL → LINEAR，**一次定形** |
| 32bpp BGRA 兼容性 | 部分芯片拒绝 B8G8R8A8+STORAGE → 降级丢 compute | 同兼容类改用 R8G8B8A8 创建图像，保住 compute 路径 |
| 影子格式（A4/sRGBA） | OPTIMAL 下 NOT_SUPPORT | 经 `vg_lite_buffer_write` 完整支持（含 shadow sync） |
| cpu_cache 一致性 | copy 路径上传后缓存可能过期 | 复用 write 路径，上传前自动失效缓存 |
| 代码量 | vg_lite_upload.c ~700 行（曾） | ~435 行；删除 upload.comp、别名管线、两个手写函数 |

---

## 1. 修改点一：vg_lite_allocate 三级探测链（可移植性）

**文件**：`src/vg_lite.c`（`vg_lite_allocate`，约 156-236 行）
**提交**：`16704d6`（含 FIXES.md #32/#33）

### 决策链（每个 buffer 只在分配时决策一次，上传时永不降级重查）

```
tier 1: probe (image_fmt, OPTIMAL, usage|STORAGE, MUTABLE_FORMAT)
        ├─ 成功 → has_storage=1, 走 compute 上传
        ├─ 32bpp 且失败 → probe (R8G8B8A8, OPTIMAL, storage usage, MUTABLE)
        │       ├─ 成功 → image_fmt=R8G8B8A8, has_storage=1   ← 兼容类替换技巧
        │       └─ 失败 ↓
        ↓ tier 2: probe (vkfmt, OPTIMAL, base usage, 0 flags)
        ├─ 成功 → has_storage=0, 上传走 staging+CopyBufferToImage
        ↓ tier 3: LINEAR 兜底
        └─ tiling=LINEAR, buffer->tiled=VG_LITE_LINEAR（打印一条提示）
```

### 关键技巧（同族三次应用：**创建格式 ≠ 视图格式**）

| 场景 | 图像创建格式 | 采样/渲染视图格式 | 依据 |
|---|---|---|---|
| 16bpp packed（如 B5G6R5） | `R16_UINT` | 原 vkfmt | 同为 16-bit 兼容类 |
| 32bpp BGRA（芯片拒 STORAGE 时） | `R8G8B8A8_UNORM` | `B8G8R8A8_UNORM` | 同为 32-bit 兼容类，位模式相同 |

两者都依赖 `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT`。compute 侧用无格式
`uimage2D`（R32/R16/R8_UINT 视图）写**原始位模式**，通道序由采样视图解释——
"位模式不变，只换格式身份"，这是整套方案成立的核心公理。

### 新增字段

- `buffer_internal_t`（`src/vg_lite_vulkan.h`）：`int has_storage;`（紧邻 `is_optimal`）
  - `1` = 图像带 STORAGE usage → compute 上传
  - `0` + `is_optimal` = copy engine 上传

### 环境变量（调试/验证用）

| 变量 | 作用 |
|---|---|
| `VGLITE_DISABLE_STORAGE_UPLOAD=1` | 跳过 tier-1 探测，强制走 copy 路径（验证降级） |
| `VGLITE_FORCE_ALT_STORAGE_FMT=1` | 跳过主探测，强制 32bpp 走 R8G8B8A8 替换分支（在不需要的机器上也能测该分支） |

### 已知边界（FIXES.md #33 记录）

32bpp has_storage 图像（实际按 R8G8B8A8 创建）如果**运行时** compute 失败而
降级到 `upload_buffer_copy` 类路径，`CopyBufferToImage` 会按 R8G8B8A8 解释
BGRA 字节导致通道交换。当前架构下不可达（compute 失败的 NOT_SUPPORT 来源在
分配时已探测过），迁移时注意保持"分配时定形"不变量。

---

## 2. 修改点二：upload_buffer_staging 统一非 compute 路径

**文件**：`src/vg_lite_upload.c`
**提交**：`fbb888b`

### 删除的（迁移时不要带走）

- `upload_buffer_copy()`：85 行手写 staging TRANSFER_SRC + CopyBufferToImage
  （与 `vg_lite.c` 的 `upload_staging()` 逐行重复）
- `upload_buffer_cpu()`：逐行 memcpy 兜底
- 内存别名路径全套：`get_upload_pipeline`、图像内存绑 STORAGE_BUFFER 别名、
  `shaders/upload.comp`、`g_vk_ctx.upload_*` 三个管线字段
  （`vg_lite_vulkan.h/.c` 同步删除 + `destroy_pipelines` 清理行）
- `staging_t.size` 死字段；staging 整块 memset（shader 只寻址 x<width，
  尾部 padding 永不读取）

### 新增 `upload_buffer_staging(buffer, data, data_stride, row_bytes)`

```c
static vg_lite_error_t upload_buffer_staging(...)
{
    /* 情况 1：映射的 LINEAR 非影子图像 → 直接逐行 memcpy 进
     * buffer->memory + vg_lite_buffer_flush。零拷贝、零 GPU 命令。 */
    if (!internal->is_optimal &&
        format != VG_LITE_A4 && format != OPENVG_sRGBA_8888) {
        per-row memcpy(dst=memory+y*stride, src=data+y*data_stride, row_bytes);
        vg_lite_buffer_flush(buffer);
        return VG_LITE_SUCCESS;
    }
    /* 情况 2：其余（OPTIMAL 图像 / 影子格式）→ malloc 临时块，
     * 重排到 buffer->stride 布局，整体委托 vg_lite_buffer_write()。 */
    packed = malloc(stride * h); memset(packed, 0, total);
    per-row memcpy(packed+y*stride, data+y*data_stride, row_bytes);
    err = vg_lite_buffer_write(buffer, packed);   /* ← 复用点 */
    free(packed);
    return err;
}
```

### 为什么委托 `vg_lite_buffer_write`（`src/vg_lite.c:907`）

write 内部已实现全部形态分派，一次复用解决四件事：

1. **A4 / OPENVG_sRGBA_8888 影子格式**：memcpy 进 shadow +
   `*_sync_to_gpu()`（位展开/字节旋转）→ 这两种格式从 NOT_SUPPORT 变为支持；
2. **OPTIMAL 图像**：先 `free(internal->cpu_cache)` 失效过期缓存（旧 copy
   路径漏了这步，先 read_ptr 再 upload 会读到旧数据），再
   `upload_to_image()` → `upload_staging()`（staging + CopyBufferToImage）；
3. **LINEAR 影子**：写 shadow 后立即同步，不依赖懒同步时机；
4. staging/barrier/提交逻辑只有 `vg_lite.c` 一份真源。

代价：情况 2 多一次 CPU 临时块拷贝（换正确性 + 代码量 −147 行）。

---

## 3. 修改点三：texelFetch 源端优化（性能/功耗）

**文件**：`shaders/upload_tiled.comp`（重写）、`src/vg_lite_upload.c`（tiled 路径）
**提交**：`7e46343`

### 优化前（SSBO + ALU 拆包）

```glsl
layout(binding = 0) readonly buffer SrcBuf { uint src[]; };
// 按 bpp 分支：移位、掩码、字节奇偶判断，每像素 ~10 条 ALU 指令
uint v = (p.bpp == 4) ? src[idx>>2]
       : (p.bpp == 2) ? halfword(src[idx>>1], idx&1)
       : byte(src[idx>>2], idx&3);
```

### 优化后（完整 shader，46 行）

```glsl
layout(local_size_x = 8, local_size_y = 8) in;
layout(binding = 0) uniform usamplerBuffer srcTexels;   // 取代 SSBO
layout(binding = 1) writeonly uniform uimage2D dstImage; // 无格式限定符，不变

layout(push_constant) uniform PushParams { uint width, height; } p;  // 8 字节

void main() {
    uint x = gl_GlobalInvocationID.x, y = gl_GlobalInvocationID.y;
    if (x >= p.width || y >= p.height) return;
    uvec4 v = texelFetch(srcTexels, int(y * p.width + x));  // 硬件取数单元拆字节
    imageStore(dstImage, ivec2(int(x), int(y)), v);
}
```

收益机制：texel 的字节宽度由 **VkBufferView 的格式**定义（R32/R16/R8_UINT），
`texelFetch` 走专用采样取数硬件，零扩展放回 `.x`——shader 里没有任何字节
处理指令。单 shader 覆盖三种 bpp（usamplerBuffer 对三种 *_UINT 格式均合法，
这也是能保住单 shader 而旧版 texelFetch 双 shader 方案不能的原因）。

### C 侧配套改动（`upload_buffer_tiled`）

| 项 | 改动 |
|---|---|
| staging usage | `STORAGE_BUFFER` → `UNIFORM_TEXEL_BUFFER`（`staging_create` 增加 usage 参数） |
| 行布局 | 4 字节对齐 padding → **完全紧凑**（texel index = y*width+x；bpp<4 时 staging 缩小） |
| 源描述符 | `VkWriteDescriptorSet.pTexelBufferView = &src_view`（新工厂：每次调用创建 transient `VkBufferView{format=view_fmt, range=staging_size}`，成功/失败两路都 `vkDestroyBufferView`） |
| 新增前置检查 | `staging_size / bytes_pp > limits.maxTexelBufferElements` → NOT_SUPPORT → 自动降级 copy 路径 |
| descriptor layout | binding 0 → `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER` |
| push 常量 | 16 → 8 字节 |

### 描述符池要求（迁移必查）

池需含 `{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, n}` 和
`{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, n}` 桶 + `FREE_DESCRIPTOR_SET_BIT`
（每次上传后归还 set）。

---

## 4. 最终路由总表（`vg_lite_upload_buffer` 入口）

```
入口校验：指针 / YUV 或多平面拒绝 / stride >= row_bytes
│
├─ is_optimal && has_storage && bpp∈{32,16,8}bit
│    └─ upload_buffer_tiled  (texelFetch → formatless imageStore)
│         ├─ SUCCESS / OUT_OF_MEMORY → 返回
│         └─ 其他 NOT_SUPPORT（特性/格式/texel上限/管线失败）
│              └─ 落穿 ↓
└─ upload_buffer_staging
     ├─ LINEAR 非影子 → 直接 memcpy + flush
     └─ 其余 → 重排 + vg_lite_buffer_write
          ├─ A4        → shadow + expand sync
          ├─ sRGBA     → shadow + rotate sync
          ├─ OPTIMAL   → cpu_cache 失效 + staging + CopyBufferToImage
          └─ LINEAR 影子 → shadow 写 + 立即 sync
```

**不变量**：缓冲形态（is_optimal / has_storage / 影子）在 `vg_lite_allocate`
一次定形；上传函数只读标志，不做会改变行为的重复探测。

---

## 5. 迁移清单

### 文件清单

| 文件 | 动作 | 内容 |
|---|---|---|
| `shaders/upload_tiled.comp` | 必带 | 唯一的 upload shader（46 行，见 §3） |
| `src/vg_lite_upload.c` | 必带 | 435 行全部（管线/staging/两条路径/入口） |
| `src/vg_lite.c` | 改动 | `vg_lite_allocate` 探测链（§1）；依赖已有的 `vg_lite_buffer_write` / `upload_staging` / 影子机制 |
| `src/vg_lite_vulkan.h/.c` | 改动 | `buffer_internal_t.has_storage`；`upload_tiled_*` 三管线字段 + `storage_image_wo_format`；`destroy_pipelines` 清理；描述符池桶位 |
| `src/CMakeLists.txt` + 顶层 CMake | 改动 | 编入 vg_lite_upload.c；`*.comp` glob → `NAME_comp.spv`（glslangValidator） |
| ~~`shaders/upload.comp`~~ | 已删除 | 不要迁移 |
| tests：`uploadBuffer` / `uploadTiled` | 建议携带 | 见 §6 |

### 设备能力依赖（启动时探测，缺一降级）

1. `shaderStorageImageWriteWithoutFormat`（Features2 链使能，采样到
   `g_vk_ctx.storage_image_wo_format`）→ 缺失则 tiled compute 不可用；
2. `R32/R16/R8_UINT` 的 `optimalTilingFeatures & STORAGE_IMAGE_BIT`；
3. `limits.maxTexelBufferElements` ≥ w*h；
4. `vkGetPhysicalDeviceImageFormatProperties` 三级探测（§1）。

任一不满足 → 自动落到 copy/memcpy 路径，**功能不丢失**，只是放弃 compute 加速。

### 验证基线

- `test_uploadBuffer` 3/3（BGRA8888 / RGB565 / L8，奇数 user_stride 防整块拷贝假阳性，逐字节 memcmp）
- `test_uploadTiled` 4/4（bgra8888/rgba8888/rgb565/l8，tiled==linear 逐字节一致；双路径：默认 + `VGLITE_DISABLE_STORAGE_UPLOAD=1`；R8G8B8A8 分支另用 `VGLITE_FORCE_ALT_STORAGE_FMT=1` 验证）
- 全套件 41 PASS / FAIL test_gfx3, test_imgIndex / CRASH test_sft_blit（均为预存问题）

### 迁移时的常见坑（本项目实测踩过）

1. **管线缓存调用点必须预初始化局部变量**：
   `VkPipeline pipeline = g_vk_ctx.upload_tiled_pipeline;` —— 未初始化的垃圾值
   会骗过 `*pipeline != VK_NULL_HANDLE` 缓存判断 → NULL layout → 驱动内崩溃；
2. 16bpp 图像必须以 `R16_UINT` **创建**（不只是视图），否则
   B5G6R5+OPTIMAL+STORAGE 探测在多数桌面驱动上直接失败；
3. texel buffer 的 `range` 必须是对应格式的 texel 整数倍（紧凑布局天然满足）；
4. compute 写图前后都要 image barrier（GENERAL→GENERAL，access 变更），layout
   不变不代表执行依赖免建；
5. staging 是一次性资源：desc set / 两个 view / staging buffer 在成功与失败
   两条路径上都要完整回收。
