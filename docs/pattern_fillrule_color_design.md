# vg_lite_draw_pattern 的 fill_rule / color 参数支持设计

> 对应需求：`vg_lite_draw_pattern` 的 `fill_rule` 与 `color` 参数当前被实现丢弃，需要落地支持。
> 先例参考：`docs/pattern_filter_support.md`（`filter` 参数，同样的"签名已有、实现丢弃"路径）。

## 1. 现状分析

### 1.1 参数已存在但被丢弃

API 签名（`inc/vg_lite.h:1256`）与官方一致，已含 `fill_rule`、`pattern_color`、`color`：

```c
vg_lite_error_t vg_lite_draw_pattern(target, path, fill_rule, path_matrix,
                                     pattern_image, pattern_matrix, blend,
                                     pattern_mode, pattern_color, color, filter);
```

但 `src/vg_lite_draw.c:771`：

```c
(void)fill_rule; (void)color;   /* 直接丢弃 */
```

同样丢弃 `fill_rule` 的还有 `vg_lite_draw_impl`（:451）、`draw_radial_internal`（:1125）、
`draw_grad_internal`（:1408）。`vg_lite_draw_grad` 100% 委托 `vg_lite_draw_pattern`（:1722），
所以 pattern 路径落地后线性渐变自动获得同一能力。

### 1.2 当前填充规则是"硬编码奇偶"

所有 4 条绘制路径共用 stencil + cover 双 pass（`src/vg_lite_vulkan.c`，pattern 管线在
`vg_lite_vulkan_init_pattern_pipeline` :1846）：

| Pass | 几何 | stencil 配置 | 语义 |
|------|------|--------------|------|
| 1 (stencil) | 扇形三角化后的三角形 | `passOp=INVERT`，`compare=ALWAYS`，front=back，`compareMask/writeMask=0x01` | 每被一个三角形覆盖翻转一次 bit0 → **奇偶规则** |
| 2 (cover) | path bbox 四边形 | `compare=NOT_EQUAL ref=0`（bit0），`passOp=ZERO` 自清理 | bit0=1 处写 pattern.frag 采样色 |

- stencil 附件为 `VK_FORMAT_D24_UNORM_S8_UINT`（:596）→ **8 bit stencil 可用**，目前只用 bit0。
- `cullMode=NONE` + `front=back`，所以 `frontFace`（draw/pattern/grad=CW、radial=CCW，:2380 不一致）
  目前无实际影响。

### 1.3 tessellator 的绕向属性（非零可行的前提）

`src/tessellator.c tessellate_path`：每个子路径（VLC_CMD_MOVE 起）做**扇形三角化**
`push_triangle(first_idx, prev_idx, curr_idx)`。三角形绕向跟随子路径顶点顺序 ——
即**外轮廓与内轮廓（洞）的绕向差被保留**，这正是非零绕向计数所需要的。

已知既有局限（与本设计无关、奇偶同样存在）：凹多边形扇形三角化会产生跨越凹口的
三角形，对奇偶规则会在凹口区引入奇覆盖（假阳性填充）。现有测试（tiger 等）未暴露。
非零规则下：同绕向重叠三角形计数 ≥2 仍 ≠0（正确）；跨空三角形 +1 假阳性与奇偶的
假阳性范围相同 —— **不引入新错误**。

### 1.4 测试现状

`tests/patternFill` 传 `VG_LITE_FILL_EVEN_ODD`、`pattern_color=0xffaabbcc`、**`color=0`**，
且 golden 校验通过 → **`color==0` 必须保持现状行为**（这是零回归的兼容基线）。

## 2. fill_rule 设计（支持 VG_LITE_FILL_NON_ZERO = 0x1901）

### 2.1 核心思路

非零绕向 = pass 1 中**正面三角形 +1、背面三角形 -1**，cover 测试 **≠0**。
Vulkan 原生支持 front/back 分离的 stencil op，无需 CPU 预排序绕向：

```
非零 stencil 管线：
  front: failOp=KEEP, passOp=INCREMENT_AND_WRAP, compare=ALWAYS,
         compareMask=0x01, writeMask=0xFF, reference=0
  back:  同上但 passOp=DECREMENT_AND_WRAP
非零 cover 管线：
  compare=NOT_EQUAL, reference=0, compareMask=0xFF,
  passOp=ZERO, writeMask=0xFF   /* 必须清全 8 bit */
```

三个关键点：

1. **writeMask 必须 0xFF**：现有 0x01 只允许写 bit0，INCR/DECR 会退化成 bit0 翻转
   （等价奇偶）。非零变体必须放开到 8 bit。
2. **cull 保持 NONE**：镜像矩阵（负行列式）翻转三角形朝向，front/back 分离的
   ±1 计数对镜像自动正确。
3. **8 bit 回绕**：INCREMENT/DECREMENT_AND_WRAP 按模 256，|绕向数| ≤ 127 内精确，
   现实路径安全；非零点 ≠0 ⇔ stencil ≠0，cover 判定成立。cover 的 ZERO 自清理
   契约保持（覆盖点 ≠0 → ZERO 归零；未覆盖点 =0 → 测试失败 KEEP 0）。

### 2.2 管线组织（保守方案，推荐）

- **奇偶管线一字节不动**（INVERT + 0x01 掩码）—— 已验证行为零风险。
- 新增非零变体：
  - `pattern_stencil_pipeline_nz`（与现有 `pattern_stencil_pipeline` 并列，init 时一并创建）；
  - cover 管线缓存 key 从 `(format, blend_group)` 扩为 `(format, blend_group, fill_rule)`
    —— 奇偶 key 复用现有管线，非零才新建。
- 绘制时按 `fill_rule` 选择：`!= VG_LITE_FILL_NON_ZERO` 一律走奇偶
  （宽容策略：垃圾值不炸，与现状最接近）。
- `set_msaa_samples` / 管线失效路径同步销毁新管线。

备选简化（后续项，不在本次）：数学上 INVERT+0xFF 掩码与 NOT_EQUAL-0 cover 等价
（从 0 出发重复翻转只会是 0 或 0xFF），可统一 8 bit 掩码、cover 单套。但要改动已验证
管线，需 8 配置回归证明 byte-identical，收益只是少一套变体。

### 2.3 frontFace 统一

front/back ops 分离后 `frontFace` 开始有实际语义。统一 4 处为同一约定
（顺带消除 radial=CCW 的不一致；在 cull NONE + front=back 下字节等价），
并用标定测试确定正负号方向（见 §5 测试），标反了只需对调 INCR/DECR。

## 3. color 设计

### 3.1 语义判定

`color` 的官方语义（NXP 手册描述模糊，参考实现 `/home/panda/gpu-vglite` 本机不可达，
**开放问题：需在 Linux 侧对参考实现核对定稿**）。候选：

| 候选语义 | 判定 |
|----------|------|
| **调制色（tint）**：采样纹素 × rgba(color) | **推荐落地**：与"color used to blend with the pattern image"的常见理解一致，实现最小 |
| 纯色填充兜底（pattern 不可用时用 color 填 path） | 排除：签名强制 pattern_image 非空，且与 pattern_mode=COLOR 的界外色职责（pattern_color，fix #32 已实现）重叠 |
| 与 pattern_color 二次合成 | 排除：同上，COLOR 模式界外已由 pattern_color 承担 |

### 3.2 落地方式

- `pattern.vert/frag` push constant 增加 `uint color`（124B → 128B，仍是 4 的倍数，不超 128B 限值）；
- `pattern.frag` 采样后调制：

```glsl
vec4 texel = texture(pattern_texture, final_uv);
if (pc.color != 0u)          /* 0 = 现状直通（兼容基线，见 1.4） */
    out_color = texel * unpackColorARGB(pc.color);
else
    out_color = texel;
```

- `color == 0` → 恒等直通（**不是**透明黑），保证 patternFill 等现有调用 byte-identical；
- COLOR 模式界外 fallback 仍走 `pattern_color`，不受 tint 影响；
- 若参考实现核对后语义不同（如仅 rgb 调制 / 非线性合成），只改 shader 一行。

## 4. 改动点清单

| # | 文件 | 改动 |
|---|------|------|
| 1 | `src/vg_lite_vulkan.h` | ctx 增 `pattern_stencil_pipeline_nz`；管线创建/缓存接口增 `fill_rule` 参数 |
| 2 | `src/vg_lite_vulkan.c` | `vg_lite_vulkan_init_pattern_pipeline` 增建非零 stencil 管线；cover 工厂与缓存 key 增 `fill_rule`；`invalidate`/`set_msaa_samples` 联动销毁；frontFace 统一 |
| 3 | `shaders/pattern.vert` `.frag` | push constant 增 `uint color`；frag 增 tint 调制 |
| 4 | `src/vg_lite_draw.c` | 删 `:771` 的 `(void)fill_rule; (void)color;`；按 fill_rule 选 pass1/cover 管线；`color` 填入 pc_data |
| 5 | `tests/fillrule_pattern/`（新） | §5 的标定 + 校验用例；`tests/CMakeLists.txt` 注册 |
| 6 | `util/util.c` | 期望缓冲 CPU 模型增非零绕向 + tint 支持（校验用） |
| 7 | `README.md` / `FIXES.md` | 功能、测试行、修复记录 |

draw/radial/grad 三条路径的同构改造**不在本次范围**（同一配方，pattern 先行验证）。

## 5. 测试计划

新测试 `test_fillrule_pattern`（BGRA8888 目标 + CPU 期望缓冲逐像素校验）：

1. **同绕向双矩形重叠**：奇偶 → 重叠区镂空；非零 → 全实心（两规则可区分的最小用例）；
2. **反绕向嵌套矩形（donut）**：奇偶与非零都镂空（标定 frontFace/INCR-DECR 方向）；
3. **三重同绕向嵌套**：奇偶 → 镂空（奇数层填、偶数层空），非零 → 实心（覆盖 8bit 计数 >1）；
4. **镜像矩阵 + 非零**：负行列式下 donut 仍正确（front/back 对称性）；
5. **tint**：`color=0` byte-identical 基线 + `color=0xFFFFFFFF`（恒等）+ 彩色 tint 乘法校验。

回归：
- `test_patternFill`（4 种 pattern_mode、COLOR 界外色）、`test_linearGrad`（100% 委托
  draw_pattern，FILTER_POINT 字节精确基线）golden 必须不变；
- **8 配置全矩阵 47/48**（Tiling × MSAA × OBB），含 `test_msaaSwitch` 联动
  （非零管线随 sample count 重建）；
- lavapipe 运行（MSRTSS 必需）。

## 6. 风险与开放问题

| 风险 | 缓解 |
|------|------|
| `color` 官方语义未核对（参考实现在 Linux 侧） | 按 tint 落地 + `0=identity` 兼容；核对后如有出入只改 shader 一行 |
| frontFace/INCR-DECR 方向标反 | donut 标定用例先行，方向错则对调两个 passOp |
| 8bit 绕向回绕（>127 层嵌套） | 现实不可达，与 GCNano 硬件同为 8bit stencil，记录为已知边界 |
| 凹轮廓扇形三角化假阳性（既有） | 本设计不引入新错误；如需修复属 tessellator 独立工作项 |
| cover 缓存 key 扩维后缓存膨胀 | 上界 = 格式数 × blend 组数 × 2，与现有同数量级 |

## 7. 实现状态（2026-10-29 落地）

已按 §2–§5 全部实现并验证：

- `pattern_stencil_pipeline_nz`（front INCREMENT_AND_WRAP / back DECREMENT_AND_WRAP /
  writeMask 0xFF）与 cover 缓存 `(format, blend_group, fill_rule)` 三元组 key（复用
  `pipeline_cache_entry_t.mode` 字段，容量 64 充足）。
- `pattern.vert/frag` push constant 124B→128B（`uint color`），frag 采样后乘法调制，
  `0 == identity` 直通。
- 新测试 `tests/fillrule_pattern`（8 帧：overlap EO/NZ、donut EO/NZ、triple EO/NZ、
  镜像 NZ、tint 三档），分析式内点采样（离边 ≥16px，规避 MSAA 边缘）。

**对 §4/§5 的偏差**：
1. `util/util.c` 期望缓冲模型未扩展——分析式点采样对轴对称矩形几何是更强的验证
   （逐点期望值可手推），无需通用绕向模型。
2. `frontFace` 统一未做（radial 仍为 CCW，其余 CW）：非零变体仅 pattern 路径新增，
  其 cull NONE + front≠back 配置已由 donut 标定用例验证方向正确；其余路径不动。
3. 实现中发现并记录的两个坑（对新测试作者有价值）：
   - `vg_lite_buffer_write` 的输入必须按 `buffer->stride` 垫行（同 `vg_lite_load_raw`
     的用法），紧凑打包数据会被当整块 memcpy 越界读；
   - "反转子路径绕向"必须真正逆序遍历（TL→BL→BR→TR），仅换起始顶点
     （BR→BL→TL→TR）不改变循环方向——鞋带公式验证同号。

**遗留开放问题**：`color` 官方语义仍需在 Linux 侧对 `/home/panda/gpu-vglite` 核对
（当前按纹素调制色实现，出入仅在 shader 一行）。
