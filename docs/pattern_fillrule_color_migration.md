# vg_lite_draw_pattern fill_rule / color 支持 — 代码迁移指南

> 本文档完整记录 2026-10-29 一轮功能改动的每一个代码差异，供向其他代码库/分支迁移使用。
> 配套设计文档：`docs/pattern_fillrule_color_design.md`（原理推导与备选方案，随迁）。

## 1. 变更概述

| 项 | 内容 |
|----|------|
| 基线 commit | `0057757` feat(tools): geom_visualizer 'noaabb' flag and sidecar triangle-count summary |
| 改动性质 | 未提交的工作区改动（7 个文件修改 + 2 个新增路径） |
| 需求 | `vg_lite_draw_pattern` 签名中的 `fill_rule`、`color` 参数此前被 `(void)` 丢弃，本轮落地 |
| 规模 | +77 / −19 行（不含新增测试 ~330 行与设计文档） |
| 验证 | 8 配置全矩阵 48 PASS / 1 FAIL（唯一失败为既有 `test_sft_blit` 崩溃，与本改动无关），每配置 1035 张出图 |

**功能语义**：

- `fill_rule == VG_LITE_FILL_NON_ZERO`（`0x1901`）→ 走新的非零绕向 stencil 管线；
  其他任何值（含 `VG_LITE_FILL_EVEN_ODD` `0x1900` 与垃圾值）→ 保持原奇偶管线（宽容策略）。
- `color != 0` → pattern.frag 采样纹素乘以 `unpackColorARGB(color)` 调制（tint）；
  `color == 0` → 恒等直通（**兼容基线**：现有 patternFill 等调用传 0，输出必须 byte-identical）。
- `vg_lite_draw_grad` 100% 委托 `vg_lite_draw_pattern`，故线性渐变自动获得同一能力。

## 2. 原理速览（迁移前必须理解）

所有绘制路径共用 **stencil + cover 双 pass**。pass 1 把扇形三角化后的 path 三角形写入
stencil（D24_UNORM_S8_UINT 的 8bit 通道）；pass 2 用 path bbox 四边形做 cover，按 stencil
测试结果写 pattern.frag 采样色，并 `passOp=ZERO` 自清理。

两种填充规则只是 stencil 配置不同：

| 配置项 | 奇偶（既有，不动） | 非零（本轮新增） |
|--------|--------------------|------------------|
| pass1 passOp (front) | `INVERT` | `INCREMENT_AND_WRAP` |
| pass1 passOp (back) | `INVERT`（front=back） | `DECREMENT_AND_WRAP` |
| pass1 compareMask / writeMask | `0x01` / `0x01` | `0x01`(沿用) / **`0xFF`** |
| pass2 compareOp | `NOT_EQUAL` ref=0 | `NOT_EQUAL` ref=0 |
| pass2 compareMask / writeMask | `0x01` / `0x01` | **`0xFF` / `0xFF`** |

tint 公式（pattern.frag 末尾）：

```glsl
out_color = (pc.color != 0u) ? pattern_color * unpackColorARGB(pc.color)
                             : pattern_color;
```

COLOR 模式界外 fallback（`pattern_color`）**不受 tint 影响**，维持 fix #32 行为。

## 3. 文件清单

| # | 文件 | 性质 | 改动 |
|---|------|------|------|
| 1 | `src/vg_lite_vulkan.h` | 修改 | ctx 增 `pattern_stencil_pipeline_nz`；cover 管线接口增 `fill_rule` 参数 |
| 2 | `src/vg_lite_vulkan.c` | 修改 | 4 处：销毁、push constant 124→128B、新建非零 stencil 管线、cover 管线三元组缓存 key + 8bit 掩码 |
| 3 | `shaders/pattern.vert` | 修改 | push constant 块增 `uint color` |
| 4 | `shaders/pattern.frag` | 修改 | push constant 增 `uint color`、`unpackColorARGB()`、采样后 tint 调制 |
| 5 | `src/vg_lite_draw.c` | 修改 | 删 `(void)fill_rule; (void)color;`；按 fill_rule 选 pass1/cover 管线；`color` 填入两处 pc_data |
| 6 | `tests/CMakeLists.txt` | 修改 | 注册 `test_fillrule_pattern` |
| 7 | `README.md` | 修改 | 功能行 + 测试行 + 汇总 47→48 |
| 8 | `tests/fillrule_pattern/`（新） | 新增 | 新测试（整目录拷贝） |
| 9 | `docs/pattern_fillrule_color_design.md`（新） | 新增 | 设计文档（整文件拷贝） |

`inc/vg_lite.h` **无改动**（`VG_LITE_FILL_NON_ZERO` 枚举已存在，:515）。

## 4. 逐文件完整差异

以下代码块均为可直接套用的 before → after（`-` 旧行 / `+` 新行）。目标库若行号不同，
按"定位锚点"（函数名/相邻语句）搜索。

### 4.1 `src/vg_lite_vulkan.h`

**改动 A** — ctx 结构体，锚点：`VkPipeline pattern_stencil_pipeline;` 之后：

```c
     VkPipeline pattern_stencil_pipeline;
+    /* NON_ZERO winding variant: front INCREMENT_AND_WRAP / back DECREMENT_AND_WRAP,
+     * writeMask 0xFF (the LSB-masked even-odd pipeline would fold +/-1 into a
+     * bit0 parity toggle). See docs/pattern_fillrule_color_design.md */
+    VkPipeline pattern_stencil_pipeline_nz;
     VkBuffer pattern_cover_vbo;
```

**改动 B** — 接口声明，锚点：`vg_lite_vulkan_get_pattern_cover_pipeline`：

```c
-VkPipeline vg_lite_vulkan_get_pattern_cover_pipeline(VkFormat format, int blend_group);
+/* fill_rule: 0 = EVEN_ODD (bit0 masks), 1 = NON_ZERO (8-bit winding masks) */
+VkPipeline vg_lite_vulkan_get_pattern_cover_pipeline(VkFormat format, int blend_group, int fill_rule);
```

**前置依赖确认**（目标库需已具备，本仓库均已有，无需改）：

- `pipeline_cache_entry_t`（:108）已含 `int mode` 字段 —— cover 缓存直接复用它存 fill_rule；
- `MAX_PIPELINE_CACHE = 64`（`inc/vg_lite_config.h`）—— cover 缓存条目上界变为
  格式数 × blend 组数 × 2，容量充足。

### 4.2 `src/vg_lite_vulkan.c`

**改动 A** — `vg_lite_vulkan_destroy_pipelines()`，锚点：销毁 `pattern_stencil_pipeline` 的下一行：

```c
     if (g_vk_ctx.pattern_stencil_pipeline) { vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.pattern_stencil_pipeline, NULL); g_vk_ctx.pattern_stencil_pipeline = VK_NULL_HANDLE; }
+    if (g_vk_ctx.pattern_stencil_pipeline_nz) { vkDestroyPipeline(g_vk_ctx.device, g_vk_ctx.pattern_stencil_pipeline_nz, NULL); g_vk_ctx.pattern_stencil_pipeline_nz = VK_NULL_HANDLE; }
```

> MSAA 运行时切换（`vg_lite_set_msaa_samples`）的管线失效路径经由 `destroy_pipelines`，
> 新管线随之销毁重建，无需额外挂钩。

**改动 B** — `vg_lite_vulkan_init_pattern_pipeline()` 内 push constant range，锚点：`pc_range.size = 124`：

```c
     /* Push constant: mat3 path_matrix(48B) + mat3 pattern_matrix(48B) +
-     * pattern_mode(4B) + pattern_color(4B) + 4 ints(16B) + blend_mode(4B) = 124B */
+     * pattern_mode(4B) + pattern_color(4B) + 4 ints(16B) + blend_mode(4B) +
+     * tint color(4B) = 128B */
     VkPushConstantRange pc_range = {0};
     pc_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
     pc_range.offset = 0;
-    pc_range.size = 124;
+    pc_range.size = 128;
```

> ⚠️ 128B 是 Vulkan `maxPushConstantsSize` 保证下限，本改动**恰好顶满**。pattern
> 管线以后再加任何 push 字段必须改用 UBO。

**改动 C** — `vg_lite_vulkan_init_pattern_pipeline()` 内，紧接奇偶 stencil 管线
`vkCreateGraphicsPipelines(..., &g_vk_ctx.pattern_stencil_pipeline)` 之后、
`vkDestroyRenderPass(g_vk_ctx.device, rp, NULL)` 之前：

```c
     VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_vk_ctx.pattern_stencil_pipeline));

+    /* NON_ZERO winding variant: front +1 / back -1 over the full 8-bit stencil
+     * (writeMask 0x01 would fold the +/-1 count into a bit0 parity toggle,
+     * degenerating to even-odd). cullMode stays NONE so mirror matrices
+     * (negative determinant) keep the winding sign correct automatically.
+     * The matching cover pipeline switches to 8-bit masks in
+     * vg_lite_vulkan_get_pattern_cover_pipeline(). */
+    if (!g_vk_ctx.pattern_stencil_pipeline_nz) {
+        stencil_ds.front.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
+        stencil_ds.front.writeMask = 0xFF;
+        stencil_ds.back = stencil_ds.front;
+        stencil_ds.back.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
+        VK_CHECK(vkCreateGraphicsPipelines(g_vk_ctx.device, VK_NULL_HANDLE, 1, &gp_ci, NULL, &g_vk_ctx.pattern_stencil_pipeline_nz));
+    }
+
     vkDestroyRenderPass(g_vk_ctx.device, rp, NULL);
```

注意语句顺序：`back = front` 先整体复制（拿到 writeMask 0xFF 等全部配置），再单独覆盖
`passOp`。`gp_ci` / `rp` / `stencil_ds` 全部复用奇偶管线的现成局部变量，`pStencilState`
指向 `stencil_ds`，无需重新拼CreateInfo。

**改动 D** — `vg_lite_vulkan_get_pattern_cover_pipeline()` 三处：

签名与缓存查找：

```c
-VkPipeline vg_lite_vulkan_get_pattern_cover_pipeline(VkFormat format, int blend_group)
+VkPipeline vg_lite_vulkan_get_pattern_cover_pipeline(VkFormat format, int blend_group, int fill_rule)
 {
     for (int i = 0; i < g_vk_ctx.pattern_pipeline_cache_count; i++) {
         if (g_vk_ctx.pattern_pipeline_cache[i].format == format &&
-            g_vk_ctx.pattern_pipeline_cache[i].blend_group == blend_group)
+            g_vk_ctx.pattern_pipeline_cache[i].blend_group == blend_group &&
+            g_vk_ctx.pattern_pipeline_cache[i].mode == fill_rule)
             return g_vk_ctx.pattern_pipeline_cache[i].pipeline;
     }
```

stencil 掩码（锚点：`cover_ds.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;` 之后；此段在
本文件有 3 处相似块，务必锚定 pattern 的那一处——用 `if (!g_vk_ctx.pattern_vert_shader)`
判空行区分）：

```c
     cover_ds.front.compareOp = VK_COMPARE_OP_NOT_EQUAL;
-    cover_ds.front.compareMask = 0x01;
-    cover_ds.front.writeMask = 0x01;
+    /* EVEN_ODD tests only bit0 (INVERT parity); NON_ZERO must test the whole
+     * 8-bit winding count and the ZERO pass-op must clean all 8 bits. */
+    int nz = (fill_rule != 0);
+    cover_ds.front.compareMask = nz ? 0xFF : 0x01;
+    cover_ds.front.writeMask = nz ? 0xFF : 0x01;
     cover_ds.front.reference = 0;
```

缓存写入（锚点：`pattern_pipeline_cache[...].blend_group = blend_group;` 之后）：

```c
         g_vk_ctx.pattern_pipeline_cache[g_vk_ctx.pattern_pipeline_cache_count].blend_group = blend_group;
+        g_vk_ctx.pattern_pipeline_cache[g_vk_ctx.pattern_pipeline_cache_count].mode = fill_rule;
         g_vk_ctx.pattern_pipeline_cache_count++;
```

### 4.3 `shaders/pattern.vert`

push constant 块末尾加一个成员（注释块同步）：

```glsl
  * - blend_mode: blend mode enum
+ * - color: tint color multiplied into the sampled texel (0 = identity passthrough)
  */
 layout(push_constant) uniform PatternPushConstants {
     mat3  path_matrix;
     ...
     int   pattern_width;
     int   pattern_height;
     int   blend_mode;
+    uint  color;
 } pc;
```

### 4.4 `shaders/pattern.frag`

改动 A — push constant 块（与 vert 对齐）：

```glsl
     int   pattern_width;
     int   pattern_height;
     int   blend_mode;
+    uint  color;
 } pc;
```

改动 B — 新增辅助函数（放在 `apply_pattern_mode` 之后、`main` 之前）：

```glsl
 /* vg_lite_color_t = 0xAABBGGRR: R in the lower 8 bits, then G, B, alpha in
  * the upper 8 bits (same helper as pattern.vert). */
 vec4 unpackColorARGB(uint c)
 {
     float a = float((c >> 24) & 0xFFu) / 255.0;
     float b = float((c >> 16) & 0xFFu) / 255.0;
     float g = float((c >>  8) & 0xFFu) / 255.0;
     float r = float((c      ) & 0xFFu) / 255.0;
     return vec4(r, g, b, a);
 }
```

改动 C — `main()` 末尾的 tint 调制，及 COLOR 模式界外注释补注：

```glsl
     if (pc.pattern_mode == PATTERN_MODE_COLOR && !is_inside_pattern(pattern_uv)) {
-        /* Outside bounds: use pattern_color from push constants */
+        /* Outside bounds: use pattern_color from push constants (tint does
+         * not apply to the COLOR-mode fallback) */
         out_color = vert_color;
         return;
     }
     vec4 pattern_color = texture(pattern_texture, final_uv);
-    out_color = pattern_color;
+
+    /* Tint: API (color) modulates the sampled texel. 0 = identity
+     * passthrough — legacy callers pass 0 and expect the untinted texel
+     * (compatibility baseline, 0 is NOT transparent black). */
+    out_color = (pc.color != 0u) ? pattern_color * unpackColorARGB(pc.color)
+                                 : pattern_color;
```

（diff 里另有若干行尾空格清理，纯外观，可不迁。）

### 4.5 `src/vg_lite_draw.c` — `vg_lite_draw_pattern()`

改动 A — 函数开头（锚点：原 `(void)fill_rule; (void)color;`）：

```c
-    (void)fill_rule; (void)color;
+    /* fill_rule: NON_ZERO gets the INCR/DECR stencil pipeline (8-bit winding
+     * count); anything else keeps the legacy INVERT even-odd pipeline.
+     * color: tint modulated into the sampled texel in pattern.frag
+     * (0 = identity passthrough). See docs/pattern_fillrule_color_design.md */
+    int nonzero = (fill_rule == VG_LITE_FILL_NON_ZERO);
```

改动 B — push constant 结构体加字段（stencil/cover 两 pass 共用同一 struct）：

```c
-    /* Push constant struct: matches pattern.vert/frag layout (124B) */
+    /* Push constant struct: matches pattern.vert/frag layout (128B) */
     struct {
         float path_m[12];      /* mat3 as 3 vec4 columns (std140) */
         float pattern_m[12];   /* mat3 as 3 vec4 columns (std140) */
         int pattern_mode;
         int pattern_color;
         int pattern_width;
         int pattern_height;
         int blend_mode;
+        uint32_t color;        /* tint (0 = identity) */
     } pc_data = {0};
```

改动 C — **两处**（stencil pass 与 cover pass 各一处）填值，锚点均为
`pc_data.blend_mode = (int)blend;`：

```c
     pc_data.blend_mode = (int)blend;
+    pc_data.color = color;
```

改动 D — pass 1 绑定按规则选管线：

```c
-    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk_ctx.pattern_stencil_pipeline);
+    vkCmdBindPipeline(g_vk_ctx.cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
+                      nonzero ? g_vk_ctx.pattern_stencil_pipeline_nz
+                              : g_vk_ctx.pattern_stencil_pipeline);
```

改动 E — pass 2 cover 管线获取带 fill_rule：

```c
-    VkPipeline cover_pipeline = vg_lite_vulkan_get_pattern_cover_pipeline(vkfmt, vg_lite_blend_to_group(blend));
+    VkPipeline cover_pipeline = vg_lite_vulkan_get_pattern_cover_pipeline(vkfmt, vg_lite_blend_to_group(blend), nonzero);
```

> C 侧 pc_data 与 GLSL push constant 块必须**严格同布局**：31 → 32 个 4 字节槽，
> `uint32_t color` 对应 `uint color`，std140 下 mat3 仍按 3 个 vec4 列（`path_m[12]`）。

### 4.6 `tests/CMakeLists.txt`

锚点：`test_roundrect` 注册块之后：

```cmake
+add_executable(test_fillrule_pattern fillrule_pattern/fillrule_pattern.c)
+add_dependencies(test_fillrule_pattern spirv_compilation)
```

（依赖 `spirv_compilation`：测试链接加载 SPIR-V 的库，shader 改动必须先重编。）

### 4.7 `README.md`

功能行（`vg_lite_draw_pattern` 条目）：

```markdown
-- **vg_lite_draw_pattern** - Pattern image fill with transform and blend
+- **vg_lite_draw_pattern** - Pattern image fill with transform and blend; fill rules EVEN_ODD/NON_ZERO (non-zero via front/back stencil INCR/DECR, see docs/pattern_fillrule_color_design.md); `color` parameter tints the sampled texel (0 = identity)
```

测试表加一行 + 汇总 47 → 48：

```markdown
+| test_fillrule_pattern | draw_pattern fill_rule (EVEN_ODD/NON_ZERO: overlap/donut/triple-nesting/mirror) + color tint (0/identity/half), analytic point sampling | PASS |
-**Summary: 47 PASS / 1 FAIL**
+**Summary: 48 PASS / 1 FAIL**
```

### 4.8 新增文件（整目录/整文件拷贝）

- `tests/fillrule_pattern/fillrule_pattern.c`（~330 行）— 8 帧验证用例，见 §6。
- `docs/pattern_fillrule_color_design.md` — 设计文档（含 §7 实现状态与偏差记录）。

## 5. 迁移检查清单（绝不能改错的点）

| # | 不变量 | 违反后果 |
|---|--------|----------|
| 1 | 非零 pass1 `writeMask = 0xFF` | 写回只作用于 bit0，INCR/DECR 退化成 bit0 翻转 = 奇偶规则（本轮最核心的坑） |
| 2 | 非零 pass2 `compareMask = writeMask = 0xFF` | 掩码不匹配则 cover 误判或 ZERO 清不干净，污染下一帧 stencil |
| 3 | `cullMode` 保持 `NONE`，front/back 用不同 passOp | 剔除背面或镜像矩阵（负行列式）下绕向符号错误 |
| 4 | `back = front` 先复制再覆盖 `passOp` | back 丢失 writeMask 0xFF / compare 配置 |
| 5 | `color == 0` 恒等直通（不是透明黑） | 现有调用方（patternFill 等）全线回归 |
| 6 | vert/frag 的 push constant 块与 C 侧 `pc_data` 布局逐槽一致，128B 顶满下限 | SPIR-V 偏移错位 → 乱色/废图 |
| 7 | cover 缓存 key 必须是三元组（含 `mode == fill_rule`） | 奇偶/非零 cover 管线互相错取（掩码不同的两条管线） |
| 8 | 奇偶管线及其 0x01 掩码**一字节不动** | 已验证路径引入回归 |
| 9 | shader 改动后重编 SPIR-V（`spirv_compilation` 目标 / glslang） | 运行时加载旧 SPIR-V，push constant 布局与 C 侧不符 |

## 6. 测试迁移与验证

### 新测试 `test_fillrule_pattern`（256×256 BGRA8888 目标 + 8×8 纯红源图 + PAD 模式 + BLEND_NONE）

| 帧 | 用例 | 期望 |
|----|------|------|
| 1/2 | 同绕向重叠双矩形 × EO / NZ | EO：重叠区镂空；NZ：全实心（两规则可区分的最小用例） |
| 3/4 | 反绕向 donut × EO / NZ | 两者都环形填充、中心镂空（标定 INCR/DECR 方向：标反则中心实心、环镂空） |
| 5/6 | 三重同绕向嵌套 × EO / NZ | EO：填/空/填（奇偶性 1/2/3）；NZ：全填（绕向数 1/2/3） |
| 7 | 非对称 donut + 镜像矩阵（translate(256,0)·scale(-1,1)）NZ | 负行列式下绕向符号仍正确（镜像后镂空带平移到对侧） |
| 8 | tint 三档：`color = 0` / `0xFFFFFFFF` / `0xFF808080` | 前两档恒等，第三档 R 通道 255→0x80（±1 容差） |

校验方式：**分析式内点采样**——采样点距任意边 ≥16px（4x MSAA 只影响边界像素，
内部精确），逐点比对 BGRA 字节值。背景 `0xFF3020A0`（0xAABBGGRR），填充红 `0x00,0x00,0xFF,0xFF`。
输出 8 张 PNG：`fillrule_{eo,nz}_{overlap,donut,triple}.png`、`fillrule_nz_mirror.png`、`fillrule_tint.png`。

### 回归要求

- 8 配置全矩阵（`VGLITE_TARGET_OPTIMAL` × `VGLITE_BLIT_MSAA` × `VGLITE_BLIT_OBB`）
  **48 PASS / 1 FAIL**（唯一失败 `test_sft_blit` 为既有崩溃）；
- `test_patternFill`、`test_linearGrad` 等 golden 必须 byte-identical；
- 含 `test_msaaSwitch`（非零管线随 sample count 重建）；
- lavapipe（MSRTSS 必需）：`VK_DRIVER_FILES=<lvp_icd.json>`。

本轮实测：8 配置全部 `PASS=48 FAIL=1 images=1035`。

## 7. 实现中踩过的坑（写新用例/迁移测试时注意）

1. **`vg_lite_buffer_write` 输入必须按 `buffer->stride` 垫行**（同 `vg_lite_load_raw`
   契约）：内部按 `stride * height` 整块 memcpy，紧凑打包的 8×8 图（32B/行 vs 64B stride）
   会越界读垃圾 → 全图黑。测试里用 `calloc(stride*height)` 布局填充。
2. **"反转子路径绕向"必须真正逆序遍历**（如 TL→BL→BR→TR 对 TL→TR→BR→BL）；
   只换起始顶点（BR→BL→TL→TR）是循环同向旋转，绕向不变——鞋带公式验签可证。
   标定 donut 用例曾因此误判 NZ 管线有 bug。
3. **MSAA 下只采样内点**：边界像素混合背景色，非确定；距边 ≥16px 的内点在 4x MSAA
   下与 1x 完全一致。

## 8. 开放问题

- `color` 的官方语义尚未对参考实现核对（`/home/panda/gpu-vglite`，本机 Windows 不可达）。
  当前按"纹素调制色（tint）+ 0=恒等"实现；若核对结果不同（如仅 rgb 调制、非线性合成），
  只需改 `pattern.frag` 末尾一行。
- draw / radial / grad 三条绘制路径的同构非零支持**未在本轮范围**（配方已验证，
  与 pattern 完全同构：加 nz stencil 管线 + cover 缓存 key 扩维 + 掩码切换）。
