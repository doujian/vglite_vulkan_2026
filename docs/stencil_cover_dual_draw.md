# stencil + cover 双 draw 实现详解

> 对应代码：`src/vg_lite_draw.c:632-678`（录制序列）、`src/vg_lite_draw.c:150-330`（管线与资源创建）、`shaders/draw.vert` / `shaders/draw.frag`
>
> 本文是 `docs/vg_lite_draw.md` 的专题深入，聚焦纯色填充路径末端的两次 `vkCmdDrawIndexed`。

## 目录

1. [设计动机：为什么矢量填充要两阶段](#1-设计动机为什么矢量填充要两阶段)
2. [前置数据流与 push constant](#2-前置数据流与-push-constant)
3. [Pass 1：stencil 路径打标](#3-pass-1stencil-路径打标)
4. [Pass 2：cover 包围盒着色](#4-pass-2cover-包围盒着色)
5. [共享 shader 与资源生命周期](#5-共享-shader-与资源生命周期)
6. [MSAA 语义](#6-msaa-语义)
7. [边界、坑与观察](#7-边界坑与观察)

---

## 1. 设计动机：为什么矢量填充要两阶段

GPU 三角化一条任意路径（可能自交、带孔、凹多边形）很难在 CPU 侧无损剖分。本仓库（与官方 gpu-vglite 同构）选择**把剖分问题扔给 stencil buffer**：

- **Pass 1（stencil）**：把路径细分的**全部小三角形**无脑画一遍，只写 stencil 不写颜色——利用"三角形覆盖次数"的奇偶性标记内部/外部；
- **Pass 2（cover）**：只画一个**路径包围盒四边形**，用 stencil 测试决定哪些样本真正着色——每个 bbox 像素至多执行一次片元着色，天然获得硬件混合与 MSAA resolve。

这就是经典的 "stencil-then-cover" 矢量填充法（OpenVG / Direct2D 同源），代价是每个 path 至少 2 次 draw call。

```
Pass 1: stencil                     Pass 2: cover
┌────────────────────┐              ┌────────────────────┐
│ 细分三角形 ×N        │              │ bbox 四边形 ×1       │
│ colorWriteMask = 0  │              │ compareOp = NOT_EQUAL│
│ passOp = INVERT(LSB)│              │ passOp = ZERO (自清) │
│                     │              │ 硬件 blend 输出颜色   │
└───────┬─────────────┘              └─────────┬──────────┘
        ▼                                      ▼
   stencil LSB: 1=内部(奇数覆盖)          仅内部样本通过测试并着色
                 0=外部(偶数覆盖)
```

---

## 2. 前置数据流与 push constant

进入双 draw 之前，`vg_lite_draw_impl` 已完成 VLC 解码 → CPU 细分为三角形几何（`geom` + `vbo/ibo`），随后组装两 pass 共用的 push constant（`vg_lite_draw.c:599-630`）：

```c
float screen_to_ndc[3][3] = { {2/w, 0, -1}, {0, 2/h, -1}, {0,0,1} };  // :599
mat3_multiply(screen_to_ndc, matrix->m, combined);                     // :608 combined = S×M
pc_data.m0/m1/m2 ← combined 三行（补 0 成 vec4）；                        // :610-615
pc_data.blend = 0;  pc_data.color = color;                             // :627-628
vkCmdPushConstants(..., 56 bytes, ...);                                // :630
```

**push constant 布局**（pipeline layout `vg_lite_draw.c:160-168`，仅 vertex stage，56 字节）：

| 偏移 | 内容 |
|------|------|
| 0-47 | `m0[4], m1[4], m2[4]`——3×3 矩阵按行补 0 |
| 48 | `int blend`（此处恒 0，**死字段**，见 §7） |
| 52 | `uint32_t color`——`0xAABBGGRR` 原始位型，由 draw.vert 的 `unpackColorARGB` 解包 |

这份 push constant **两个 pass 共用**——Pass 1 不重设，因为矩阵一样，color 因 `colorWriteMask=0` 无效。

---

## 3. Pass 1：stencil 路径打标

录制序列（`vg_lite_draw.c:632-637`）：

```c
vkCmdBindVertexBuffers(cmd, 0, 1, &vbo, &offset);          // 细分三角形顶点 (R32G32_SFLOAT, stride 8)
vkCmdBindIndexBuffer(cmd, ibo, 0, VK_INDEX_TYPE_UINT32);   // 三角形索引
vkCmdBindPipeline(cmd, GRAPHICS, g_draw_pipeline.stencil_pipeline);
vkCmdDrawIndexed(cmd, geom.index_count, 1, 0, 0, 0);
```

### stencil 管线状态全表（`init_draw_pipeline`，`vg_lite_draw.c:239-267` 创建）

| 状态 | 值 | 作用 |
|------|-----|------|
| `stencilTestEnable` | TRUE | |
| `compareOp`（front+back） | `ALWAYS` | 永远通过，纯为触发 op |
| `passOp` | **`INVERT`** | 每个被覆盖样本 LSB 翻转——偶数次覆盖=0（外），奇数次=1（内） |
| `failOp` / `depthFailOp` | `KEEP` | 无 depth（`depthTestEnable=FALSE`） |
| `compareMask` / `writeMask` | **`0x01`** | 只用最低位，与 cover 约定一致 |
| `reference` | 0 | |
| `colorWriteMask` | **0**（:259-263） | 不碰颜色附件 |
| 光栅化 | `CULL_MODE_NONE`、`FRONT_FACE_CCW`、`TRIANGLE_LIST` | 自交路径正反三角都要计数 |
| `ms.rasterizationSamples` | `g_msaa_samples`（4x） | **逐样本**执行 INVERT → even-odd 判定在样本级，AA 由此而来 |
| 动态状态 | viewport + scissor | |

**核心不变量：三角形每覆盖一个样本，该样本 stencil LSB 翻转一次。**

- 自交/重叠区域被覆盖偶数次 → LSB 回 0 → 外部；
- 覆盖奇数次 → LSB=1 → 内部。

这就是 even-odd 填充规则在 GPU 上的实现，且完全无视三角形朝向（cull NONE）。

---

## 4. Pass 2：cover 包围盒着色

### 4.1 bbox → NDC（`vg_lite_draw.c:641-655`）

```c
float corners[4][2] = { bbox左下, 右下, 右上, 左上 };      // :642-647 path->bounding_box 原始值
for (i = 0..3)
    cover_verts[i] = combined × corners[i];                 // :648-651 与 stencil 完全相同的矩阵变换
create_cover_vbo(cover_verts, &cover_vbo, &cover_vbo_mem);  // :655
```

关键点：cover 顶点在 **CPU 侧就乘完矩阵变成 NDC**，所以 shader 端 push constant 是**单位阵**（:657-668，注释明说）——顶点绕过变换直落 `gl_Position`。这保证 cover quad 与 stencil 几何使用**同一 `combined` 矩阵**，边界严格对齐。

`create_cover_vbo`（:393-413）：每次 draw 现场创建 32 字节 host-visible/coherent buffer（4 顶点 × vec2），map-memcpy-unmap。无 IBO——索引用全局共享的。

### 4.2 cover 录制（`vg_lite_draw.c:669-678`）

```c
cover_pc = { 单位阵, blend=0, color };                     // :658-671 第二份 push constant
vkCmdBindVertexBuffers(cmd, 0, 1, &cover_vbo, ...);        // :674
vkCmdBindIndexBuffer(cmd, g_draw_pipeline.cover_ibo, 0, UINT32);  // :675 全局 {0,1,2,0,2,3}
VkPipeline cover = get_draw_cover_pipeline(vkfmt, vg_lite_blend_to_group(blend));  // :676
vkCmdBindPipeline(cmd, GRAPHICS, cover);
vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);                      // :678 两个三角形
```

`cover_ibo`（init :313-330）：进程级静态 index buffer，内容固定 `{0,1,2, 0,2,3}`（quad 三角化），VK_INDEX_TYPE_UINT32。

### 4.3 cover 管线：模板测试 + 自清零 + 按 blend 缓存

depth/stencil 状态（固定版 :269-286 与缓存版 :72-81 一致）：

| 状态 | 值 | 作用 |
|------|-----|------|
| `compareOp` | **`NOT_EQUAL`** | `stencil LSB != reference(0)` 才通过 = **只着色内部样本** |
| `compareMask`/`writeMask` | `0x01` | 只测/只写 LSB |
| `passOp` | **`ZERO`** | **着色同时把 LSB 清 0——自清洁，下次 draw 无需手动 clear stencil** |
| `failOp`/`depthFailOp` | `KEEP` | 外部样本保持 0（本来就 0） |

颜色混合是 cover 专属维度的**管线缓存**（`get_draw_cover_pipeline` :51-148）：

- key = `(VkFormat, blend_group)`，上限 32 条（`MAX_DRAW_COVER_PIPELINES`），线性查找；
- `BG_NONE` → 直接返回 init 时创建的固定 `cover_pipeline`（blendEnable=FALSE）；
- 其他 blend group → 按需创建：`vg_lite_vulkan_get_blend_state(blend_group, &cba)` 取硬件混合状态，`BG_SRC_OVER` 特判 `srcColor=SRC_ALPHA / srcAlpha=ONE`（:67-70，premultiplied 语义）；
- RP 兼容性：临时 `vg_lite_vulkan_create_render_pass(format)` 建管线后立即销毁（:120, :139）——管线只要求 RP 兼容，不要求同一实例；
- 其余状态（顶点输入 vec2、TRIANGLE_LIST、cull NONE、4x MSAA、动态 viewport/scissor）与 stencil 管线一致，仅 depth/stencil 和 blend 不同。

---

## 5. 共享 shader 与资源生命周期

### 5.1 两个 pass 共用的 shader

- **draw.vert**：接收 push constant，`gl_Position = path_m × vec3(pos,1)`，`unpackColorARGB(pc.color)` 按 `0xAABBGGRR` 解包成 RGBA 经 `vert_color` 传给片元；
- **draw.frag**（全文 9 行）：`out_color = vert_color;`——纯直通。

即：颜色计算全部在**顶点**侧完成（4 顶点插值常数），片元只是转发；所有混合由固定功能 blend 单元按 cover 管线的 attachment state 完成。填充色是纯色，这样做零成本；radial/pattern 则换各自的 vert/frag（骨架不变）。

### 5.2 buffer 生命周期（`vg_lite_draw.c:680-683`）

```c
tess_geometry_free(&geom);  vlc_path_free(&vlc_path);       // CPU 侧几何立即释放
add_pending_buffer(vbo, vbo_mem, ibo, ibo_mem);            // GPU 侧细分几何延迟销毁
add_pending_buffer(cover_vbo, cover_vbo_mem, NULL, NULL);  // GPU 侧 cover quad 延迟销毁
```

**每次 draw 都新建并销毁一对 cover buffer**——GPU 还在用，不能立刻 free，挂进 pending 队列等提交后回收（每-draw 分配开销的来源，见 §7）。

---

## 6. MSAA 语义

stencil 附件随 RP 以 4x 承载（MSRTSS 下单采样目标由 tile 内存多样本承载，legacy 路径为独立 4x 附件）。Pass 1 的 INVERT **按样本**执行，Pass 2 的 NOT_EQUAL 也按样本判定：

```
边界样本: 一半样本 LSB=1(内部), 一半 LSB=0(外部)
    → cover 后 store/resolve 平均 → 抗锯齿边缘
```

无 sample shading、无 alpha-to-coverage，AA 完全来自"逐样本 even-odd + resolve"。

---

## 7. 边界、坑与观察

1. **`fill_rule` 被忽略**（:450 `(void)fill_rule`）——只实现 even-odd，`NON_ZERO` 被静默按 even-odd 处理（需 INC/DEC + 比较才能支持 nonzero，已知与官方实现的差异点）。
2. **push constant 的 `blend` 字段是死字段**——两个 pass 都填 0，无 shader 消费；实际混合由管线对象的 attachment state 决定。
3. **每 draw 一次 32B buffer 创建/销毁**（`create_cover_vbo` + pending 延迟 free）——正确但浪费；init 时建的 256B 持久 `g_draw_pipeline.cover_vbo`（:300-311）**从未被此路径使用**，疑似遗留。
4. **依赖 LSB 语义的自清洁链**：stencil INVERT 写 LSB → cover ZERO 清 LSB → 下一次 draw 直接复用。但若某次 draw 被 scissor 裁掉部分区域，stencil 可能残留脏 LSB——这是 scissor+seed 遗留 bug 的温床；RP 续用时 :552-560 有 stencil 全量 clear 兜底。
5. **cover quad = 变换后 bbox 的凸包四边形**：矩阵含旋转/剪切时，实际路径可能只占 quad 一部分——stencil 测试保证多余像素被 NOT_EQUAL 拒绝，代价只是这些像素的空顶点插值 + 早期片元剔除，不影响正确性。
