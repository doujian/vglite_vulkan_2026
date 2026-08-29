# vg_lite_upload_buffers 批量上传设计文档

> 版本：v1.0（对照 src/vg_lite_upload.c:437-814、inc/vg_lite.h 声明、tests/uploadBatch/uploadBatch.c 逐行核对撰写）
>
> 前置阅读：`docs/vg_lite_upload_buffer_design.md`（单次上传两条路径的底层设计）、`docs/vg_lite_upload_optimization_migration.md`（texelFetch 优化与分配探测链）。本文只在必要时摘要底层细节，重点讲"批量"层面新增的东西。

---

## 0. 动机与收益

### 0.1 单次上传的开销模型

`vg_lite_upload_buffer` 是一次性的（one-shot）：

| 每次调用都要做 | 成本 |
|---|---|
| staging buffer + device memory 创建/映射/销毁 | 3~5 次 vk* 调用，驱动侧可能有锁 |
| `flush_render_pass` + `submit_command(1)`（等 fence）+ `begin_command` | 2 次队列提交 + 1 次全量 CPU 等待 |
| （compute 路径）transient VkBufferView + VkImageView + descriptor set | 3 次创建 + 3 次销毁 + 1 次写入 |

上传 N 张纹理时 CPU 侧要付 N 倍；两次 submit 之间的 fence 等待还把 GPU 流水线打碎成 N 段。加载 100 张纹理 = 100 次 staging 分配 + 200 次提交 + 100 次 fence 等待。

### 0.2 批量上传的核心思想

`vg_lite_upload_buffers` 把整批共用三样东西：

1. **一个 staging buffer**——所有 GPU 路径项的数据紧凑拼接在内，按 16 字节对齐分段；
2. **一个命令缓冲**——所有 compute dispatch 与 copy region 混录在同一录制里；
3. **一次提交**——单个 `vkQueueSubmit` + 单次 fence 等待。

GPU 侧执行仍按项串行（每项自带前后 barrier），但 CPU 侧驱动调用数从 O(N) 降到 O(1) 级别，fence 等待从 N 次降到 1 次。100 张纹理的场景预期约一个数量级的 CPU 开销下降。

### 0.3 与单次 API 的关系

- 批量 API **完全复用**单次路径的全部设施：同一个 compute 管线缓存（`get_upload_tiled_pipeline`）、同一个 shader（`shaders/upload_tiled.comp`）、同一个 descriptor pool；
- 单次 `vg_lite_upload_buffer` 保持不变——批量是"多路复用器"，不是替代品；
- 每一项的**路由决策逻辑与单次版本一致**（is_optimal + has_storage → compute，否则 copy / CPU / shadow 委托），差异只在资源是批内共享的。

---

## 1. API 契约

### 1.1 声明（inc/vg_lite.h，紧跟 vg_lite_upload_buffer 之后）

```c
vg_lite_error_t vg_lite_upload_buffers(vg_lite_buffer_t  **bufs,
                                       vg_lite_uint8_t   **datas,
                                       vg_lite_uint32_t   *strides,
                                       vg_lite_uint32_t    count);
```

| 参数 | 语义 |
|---|---|
| `bufs` | 已 `vg_lite_allocate` 的 buffer 指针数组（**必须是指针数组**，不能传数组退化的 `&array[0]` 再取址，见 §8 陷阱 5） |
| `datas` | 每项的源像素数据（VGLite CPU 布局，单平面） |
| `strides` | 每项的用户行距；`0` 表示紧凑（= row_bytes） |
| `count` | 项数 |

### 1.2 行为语义

- **失败粒度**：任一项参数非法（空指针 / 零尺寸 / YUV / stride < row_bytes）→ 立即返回 `VG_LITE_INVALID_ARGUMENT`，此时**尚未发生任何写入**（pass 1 在任何上传前完成全部校验）；
- **降级语义**：GPU 阶段失败（staging 分配、命令缓冲重置、视图中途创建失败）→ 内部退化为逐项顺序上传，尽力完成整批，函数返回首个错误或 SUCCESS（见 §6 完整矩阵）；
- **部分成功**：文档化契约是"返回首个失败项的错误，其之前的项已生效"；
- **单平面限制**：与单次版本一致，YUV 多平面（语义上会有 data[1]/data[2]）直接拒绝。

---

## 2. 数据结构

### 2.1 batch_item_t（src/vg_lite_upload.c:442-453）

```c
typedef struct {
    vg_lite_buffer_t *buf;
    const uint8_t    *data;
    uint32_t          data_stride;  /* 用户行距（已归一化：0 → row_bytes） */
    uint32_t          row_bytes;    /* width*bpp/8 向上取整 */
    uint32_t          bytes_pp;     /* 1/2/4 —— compute 路径的每像素字节数 */
    VkDeviceSize      offset;       /* 该项在批量 staging 中的段起始（16 字节对齐） */
    VkDeviceSize      size;         /* 紧凑尺寸 row_bytes*height */
    uint32_t          res_idx;      /* 在 src_views/dst_views/sets 数组中的槽位 */
    int               use_compute;  /* OPTIMAL+storage：dispatch 路径 */
    int               use_copy;     /* OPTIMAL 无 compute：copy engine 路径 */
} batch_item_t;
```

设计要点：

- **offset/size 描述 staging 布局**：LINEAR 项不占 staging（CPU 直写），只有 OPTIMAL 项拼接进 staging；
- **use_compute/use_copy 是运行时可变的**：视图中途创建失败会把单项从 compute 降级为 copy（§5.3），所以它们不是 pass 1 的一锤定音；
- **res_idx 解决资源 FIFO 错配**：descriptor/view 槽位按"实际成功创建"的顺序分配，与项下标不一一对应（§8 陷阱 2）。

### 2.2 辅助函数

```c
static VkFormat batch_view_fmt(uint32_t bytes_pp);
/*   4 → VK_FORMAT_R32_UINT, 2 → R16_UINT, 其余 → R8_UINT
 *   与单次 upload_buffer_tiled 的 view_fmt 选择完全一致 */

static vg_lite_error_t seq_upload_one(vg_lite_buffer_t *b,
                                      vg_lite_uint8_t *data,
                                      vg_lite_uint32_t stride);
/*   组装 data[3]={data,NULL,NULL} / stride[3] 后调用 vg_lite_upload_buffer。
 *    存在原因见 §8 陷阱 1：降级循环不能把单个元素地址硬塞给 data[3] 参数 */
```

---

## 3. 执行流程总览

```
vg_lite_upload_buffers(bufs, datas, strides, count)
│
├─ Pass 1  分类规划（零副作用：只校验、算 staging 布局、定 use_compute/use_copy）
│    ├─ 非法项 → INVALID_ARGUMENT，整批中止（尚无任何写入）
│    └─ 管线创建失败 → 所有 OPTIMAL 项改判 use_copy
│
├─ CPU 阶段  LINEAR 非影子项：逐行 memcpy 进 buffer->memory + vg_lite_buffer_flush
│    └─ 失败 → 逐项 seq_upload_one 降级
│
├─ GPU 阶段（仅当 n_gpu > 0，即至少一个 OPTIMAL 项）
│    ├─ staging_create(total, TRANSFER_SRC|UNIFORM_TEXEL_BUFFER)
│    │    └─ 失败 → 逐项 seq_upload_one 降级，返回 SUCCESS（已尽力）
│    ├─ CPU 填充：各段逐行 memcpy（紧凑行，用户 stride → row_bytes）
│    ├─ flush_render_pass + submit(1) + begin_command
│    ├─ 一条全局 buffer barrier（HOST_WRITE → SHADER_READ|TRANSFER_WRITE）
│    ├─ 资源创建 pass：compute 项建 src VkBufferView + dst VkImageView + desc set
│    │    └─ 单项创建失败 → 该项降级 use_copy（槽位不浪费）
│    ├─ 录制 pass：compute 项 dispatch；copy 项 CopyBufferToImage；同序混录
│    ├─ vg_lite_vulkan_submit_command(1)      ← 整批唯一一次 GPU 等待
│    └─ 清理：desc sets / views / staging / items
│
└─ delegate 标签  LINEAR 影子格式（A4 / OPENVG_sRGBA_8888）项：
     逐项 upload_buffer_staging（内部走 vg_lite_buffer_write 的 shadow sync）
```

**同步骨架**沿用单次版本的纪律：GPU 阶段开始前 `flush_render_pass` + `submit_command(1)` 等空 fence（确保没有 in-flight 命令引用即将被写入的图像），然后 `begin_command` 重新录制；结束时 `submit_command(1)` 再等一次。整批恰好两段 submit，无论项数多少。

---

## 4. Pass 1：分类规划（vg_lite_upload.c:493-549）

对每一项：

1. **校验**：`buf/handle/width/height/datas[i]` 非空、非 YUV（`vg_lite_is_yuv_format`）；
2. **几何**：`row_bytes = (width*bpp_bits+7)/8`，`data_stride = strides[i] ?: row_bytes`，`data_stride < row_bytes` → 非法；`size = row_bytes*height`；
3. **LINEAR 项**（`!is_optimal`）：不占 staging，跳过（CPU 阶段或 delegate 处理）；
4. **OPTIMAL 项**：
   - `free(cpu_cache)`——与 `vg_lite_buffer_write` 一致的缓存失效纪律（防 read_ptr 旧数据）；
   - `offset = ALIGN(total, 16)`，`total += size`——16 字节对齐同时满足 VkBufferView 的 offset 要求和 vkCmdCopyBufferToImage 的 bufferOffset 要求；
   - **compute 判定**（全部满足才走 dispatch 路径）：
     - `has_storage`（分配时探测链的结论，见 §7.1）
     - `bytes_pp ∈ {4,2,1}`（bpp 32/16/8 位）
     - `g_vk_ctx.storage_image_wo_format`（shaderStorageImageWriteWithoutFormat 特性）
     - `n_compute < 32`（descriptor pool 64 maxSets 的余量；超出部分自动落 copy 路径）
     - view 格式（R32/R16/R8_UINT）的 `optimalTilingFeatures & STORAGE_IMAGE_BIT`
     - `size / bytes_pp ≤ limits.maxTexelBufferElements`（texel buffer 容量上限）
   - 不满足任一条 → `use_copy = 1`。

Pass 1 之后还有一次**全局管线降级**：`get_upload_tiled_pipeline` 失败（shader 加载/管线创建问题）→ 所有 OPTIMAL 项强制 use_copy。注意 pipeline 局部变量在调用前已从 `g_vk_ctx` 预初始化（防垃圾值骗过缓存检查的历史 bug，见 §8 陷阱 4）。

> 注：pass 1 里 `vkGetPhysicalDeviceFormatProperties/Properties` 每项调用一次，属于冷路径开销，可接受；如成热点可缓存到 context（当前实现选择不缓存，保持改动面小）。

---

## 5. GPU 阶段细节

### 5.1 staging 与填充

```c
staging_create(total ? total : 4,
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, &staging);
```

- **双用途 usage** 是批量的关键设计：同一块 staging 既当 compute 项的 texel buffer 源（binding 0 的 `usamplerBuffer`），又当 copy 项的 `vkCmdCopyBufferToImage` 源。单次版本两条路径各建各的 staging，批量里合二为一；
- HOST_VISIBLE|COHERENT（staging_t 语义不变），CPU 填充后无需显式 flush；
- 填充：每项逐行 `memcpy(staging.mapped + offset + y*row_bytes, data + y*data_stride, row_bytes)`——**行与行之间紧凑**（无 rowPitch 填充），因此：
  - compute 项的 texel 索引是干净的 `y*width + x`；
  - copy 项的 `region.bufferRowLength = 0`（告诉 Vulkan 源是紧凑行）。

### 5.2 一条全局 barrier

```c
VkBufferMemoryBarrier buf_pre = { ... };
buf_pre.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
buf_pre.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
/* HOST → COMPUTE_SHADER | TRANSFER，覆盖整块 staging（WHOLE_SIZE） */
```

单次版本里 compute 路径只需要 SHADER_READ；批量版本因为 staging 同时服务两类消费者，dstAccessMask 并集两个位。**一条 barrier 覆盖所有分段**，不再按项各发一条。

### 5.3 资源创建 pass 与单项降级（635-697 行）

对每个 use_compute 项依次创建三件套，任何一步失败就把该项降级为 use_copy 并继续：

1. `VkBufferView`：`{buffer=staging.buffer, format=batch_view_fmt(bytes_pp), offset=it->offset, range=it->size}`——分段视图，每个 compute 项只看到自己的数据段（range 是该格式 texel 的整数倍，因为 size = row_bytes*h 且 row_bytes 是 bytes_pp 的倍数）；
2. `VkImageView`：`{image, 2D, format 同上}`——formatless storage image 的访问入口（图像本体由分配时的兼容类技巧保证可被此格式视图打开，16bpp 图像本来就是 R16_UINT 创建的）；
3. descriptor set：binding 0 = `UNIFORM_TEXEL_BUFFER`（pTexelBufferView 指向 src view），binding 1 = `STORAGE_IMAGE`（imageLayout=GENERAL）。

**只有三件全部成功**才 `it->res_idx = vi; vi++` 占用槽位；中途失败的项不留空洞（view 已建则销毁，槽位让给下一项）。res_idx 因此是"项 → 资源槽"的间接层。

### 5.4 录制 pass（699-772 行）

按项序混录两类命令（`rec_err` 贯穿，录制中任一 vk 调用失败即停止后续录制并进入降级）：

**compute 项**（k = it->res_idx）：

```
image barrier: GENERAL→GENERAL, ALL_COMMANDS→COMPUTE, 取 SHADER_WRITE 访问权
bindPipeline(COMPUTE, upload_tiled_pipeline)
bindDescriptorSets(sets[k])
pushConstants {width, height}          ← 8 字节，与单次版本相同
dispatch (w+7)/8 × (h+7)/8 × 1          ← 8×8 workgroup，texelFetch+imageStore
image barrier: SHADER_WRITE → MEMORY_READ|WRITE（COMPUTE→ALL_COMMANDS）
```

**copy 项**：

```
image barrier: GENERAL→TRANSFER_DST, dstAccess=TRANSFER_WRITE
vkCmdCopyBufferToImage(staging.buffer, image, TRANSFER_DST,
    {bufferOffset=it->offset, bufferRowLength=0 /* 紧凑行 */, extent=w×h×1})
image barrier: TRANSFER_DST→GENERAL（TRANSFER→ALL_COMMANDS）
```

顺序性保证：**每个图像的前后 barrier 让相邻项对该图像的访问天然串行**；不同图像之间无共享资源（staging 只读），无序执行也安全。整批录完一次 `vg_lite_vulkan_submit_command(1)`。

### 5.5 清理

submit 返回后（fence 已等）：按 `n_compute` 槽位循环 `vkFreeDescriptorSets` / `vkDestroyBufferView` / `vkDestroyImageView`，`staging_destroy`，`free(items)`。全部是批级一次性资源——这正是批量省掉的 N 倍开销所在。

---

## 6. 降级与错误路径矩阵（完整）

| 失败点 | 已生效的项 | 未生效的项的处理 | 返回值 |
|---|---|---|---|
| pass 1 校验失败 | 无 | 无（尚未写入） | INVALID_ARGUMENT |
| pass 1 管线创建失败 | — | 全部 OPTIMAL 改判 copy，正常走批 | （继续批流程） |
| CPU 阶段 LINEAR memory 为空 | 之前的 LINEAR 项 | 全部逐项 seq_upload_one | OUT_OF_MEMORY |
| staging_create 失败 | CPU 阶段的 LINEAR 项 | 全部逐项 seq_upload_one | **SUCCESS**（已尽力） |
| flush/submit/begin 失败（rec_err） | CPU 阶段项 | begin_command 恢复上下文 + OPTIMAL 项逐项 seq_upload_one | SUCCESS* |
| 单项 view/set 创建失败 | 其余项 | **仅该项**降级 use_copy，批继续 | （继续） |
| 录制中途 rec_err | CPU 阶段项 | begin_command + OPTIMAL 项逐项 seq_upload_one | SUCCESS* |
| delegate 阶段影子项失败 | 之前所有项 | 该项起中止 | 该项错误码 |

\* 降级路径的返回值语义：尽力完成后返回 SUCCESS（所有 buffer 实际都拿到了数据）；这是"批优先、顺序兜底"策略。若上层需要严格错误传播，可在 seq 循环里收集首个非 SUCCESS 返回（当前实现 uploadBatch 测试两条路径均验证通过）。

`seq_upload_one` 是全形状兜底：它调用的 `vg_lite_upload_buffer` 内部本身就覆盖 compute/copy/CPU/shadow 四种形状，所以任何降级组合都不会漏。

---

## 7. 与底层的接口约定（复用清单）

### 7.1 从 vg_lite_allocate 继承的决定

批量**不做**任何分配探测——每项的 `is_optimal`/`has_storage` 是分配时一次定形的结论（三级探测链：STORAGE 探测 → 32bpp R8G8B8A8 替代探测 → base OPTIMAL → LINEAR 兜底；`VGLITE_DISABLE_STORAGE_UPLOAD`/`VGLITE_FORCE_ALT_STORAGE_FMT` 环境变量）。批量只消费结论。

### 7.2 从单次 upload 复用的设施

| 设施 | 复用方式 |
|---|---|
| `get_upload_tiled_pipeline`（管线缓存于 g_vk_ctx） | 批开始时取一次，全批共用（失败才整体降级 copy） |
| `shaders/upload_tiled.comp` | 原样复用：texelFetch(usamplerBuffer) → imageStore(formatless uimage2D)，push {w,h} |
| `staging_t` / staging_create(usage) / staging_destroy | usage 参数化后支持 TRANSFER_SRC|UNIFORM_TEXEL_BUFFER 双用途 |
| `alloc_desc_set`（全局 pool，FREE_DESCRIPTOR_SET_BIT） | 批尾统一归还；n_compute<32 上限保证不超 64 maxSets |
| barrier 模式（GENERAL→GENERAL / →TRANSFER_DST→GENERAL） | 与单次版本逐字相同 |

### 7.3 委托出去的部分

LINEAR 影子格式（A4 / OPENVG_sRGBA_8888）不进 GPU 阶段——它们的 CPU 布局与 GPU 位模式有变换（pack/rotate/sRGB），统一在 `delegate:` 标签处逐项走 `upload_buffer_staging` → `vg_lite_buffer_write`（shadow sync 路径）。这保证影子语义只有一份实现。

---

## 8. 实现中踩过的坑（迁移者必读）

1. **`&d` 塞给 `data[3]` 参数**：降级循环最初把单个元素地址传给期望 `vg_lite_uint8_t* [3]` 的参数，`data[1]/data[2]` 越界读（gcc stringop-overflow 警告抓到的）。修法：`seq_upload_one` 显式组装 `{data,NULL,NULL}`。教训：C 数组参数不做边界检查，指针形状要人肉保证。
2. **资源 FIFO 序号错配**：最初用 `vi--`/`vi++` 现场回退槽位，但项降级发生在资源 pass 中途时，后续项的槽位与已写 descriptor 的槽位对不上。修法：`res_idx` 字段显式记录"项 → 槽"，降级项不占槽。
3. **降级循环漏影子格式**：早期版本降级只重放 OPTIMAL 项，LINEAR A4/sRGBA 项被跳过。修法：所有降级路径统一走 `seq_upload_one`（其内部是全形状的单次 API）。
4. **管线局部变量预初始化**：`VkPipeline pipeline = g_vk_ctx.upload_tiled_pipeline;` 必须从 context 初始化——未初始化的垃圾值会骗过 `get_upload_tiled_pipeline` 的 `*pipeline != NULL` 缓存检查，导致 desc_layout 为 NULL 后崩在驱动里（历史 bug，批量实现延续防御写法）。
5. **`bufs` 必须是指针数组**：调用方要 `vg_lite_buffer_t *buf_ptrs[N]; buf_ptrs[i] = &bufs[i];`——直接把 `vg_lite_buffer_t bufs[N]` 数组当 `bufs` 传是类型错误（`vg_lite_buffer_t**` vs `vg_lite_buffer_t (*)[N]` 语义差异），测试初期踩过。

---

## 9. 测试策略（tests/uploadBatch/uploadBatch.c）

6 buffer 混合批，覆盖全部路径组合：

| # | 形状 | 格式 | 尺寸 | 内部路径 |
|---|---|---|---|---|
| 0 | LINEAR | BGRA8888 | 96×64 | CPU 直写 mapped memory |
| 1 | LINEAR | RGB565 | 95×63 | 同上（非对齐宽） |
| 2 | LINEAR | L8 | 95×63 | 同上 |
| 3 | TILED | BGRA8888 | 96×64 | compute dispatch |
| 4 | TILED | RGB565 | 96×64 | compute（R16_UINT 视图） |
| 5 | TILED | L8 | 95×63 | compute（R8_UINT，95 非对齐宽验证 texel 索引） |

防假阳性设计：

- **奇数用户 stride**（row_bytes+7）：逼迫所有路径做真正的逐行重排，杜绝"整块 memcpy 恰好对齐也能过"的假阳性；
- **非对齐尺寸**（95×63）：覆盖 row_bytes 与 staging 紧凑布局的边界；
- **回读验证**：upload 后对每个 buffer `vg_lite_buffer_download`，逐行逐字节与源数据比对（不是抽样，不是只看 PNG）；
- **双路径跑**：默认（compute dispatch）与 `VGLITE_DISABLE_STORAGE_UPLOAD=1`（TILED 项全部降级 copy 路径）都必须 6/6 PASS。

基线：全套件 **42 PASS**（+1 = 本测试）/ FAIL test_gfx3, test_imgIndex / CRASH test_sft_blit（均为预存在，与批量改动无关）。

---

## 10. 已知限制与后续方向

1. **staging 无池化**：每批仍是 create/destroy 一对。批频低（资源加载）无所谓；高频小批可做分级 staging 池（单次优化方案 3 的延续）；
2. **format properties 每项查询**：pass 1 对每个 compute 候选项各调一次 `vkGetPhysicalDeviceFormatProperties/Properties`，可缓存到 context；
3. **compute 上限 32 是拍脑袋值**：由 descriptor pool maxSets=64 推出，超出静默落 copy 路径（正确但可观测性差）；可加一行 log；
4. **无异步上传**：批结束仍等 fence（同步 API 语义）。若要真正异步需要双缓冲 staging + 时间线 semaphore，超出本轮范围；
5. **单平面**：YUV 多平面批被整体拒绝——与单次 API 一致，等有需求再议。

---

## 11. 文件清单

| 文件 | 角色 |
|---|---|
| `inc/vg_lite.h` | vg_lite_upload_buffers 声明 + doc 注释 |
| `src/vg_lite_upload.c` :437-814 | 批量实现（batch_item_t / seq_upload_one / vg_lite_upload_buffers） |
| `shaders/upload_tiled.comp` | 复用的 compute shader（未改动） |
| `tests/uploadBatch/uploadBatch.c` | 6-buffer 混合批验证 |
| `tests/CMakeLists.txt` | test_uploadBatch 注册 |
