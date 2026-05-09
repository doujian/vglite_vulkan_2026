# VGLite Vulkan — Clear & Blit 设计文档

## 1. 总体架构

本项目用 Vulkan 实现 VGLite 的 clear 和 blit 操作，替代原 GPU-VGLite 的硬件加速路径。核心设计思路：

- **Clear**：直接使用 `vkCmdClearAttachments` 在 render pass 内清除颜色附件
- **Blit**：通过全屏三角形绘制 + Fragment Shader 实现，支持矩阵变换、格式转换、多种混合模式
- **CPU 验证**：独立的 CPU 侧像素验证系统，逐像素计算期望结果并与 GPU 输出比对

```
+------------------------------------------------------+
|                   VGLite API 层                       |
|  vg_lite_clear()    vg_lite_blit()    vg_lite_finish() |
+------------------------------------------------------+
|                   矩阵变换层                           |
|  3x3 仿射/透视矩阵 -> 逆矩阵 -> shader_mat             |
+------------------------------------------------------+
|                   Vulkan 渲染层                        |
|  Pipeline Cache (Format x BlendGroup)                 |
|  Push Constants -> Vertex Shader -> Fragment Shader   |
|  Native Blend (GPU) / Shader Blend (texture read)    |
+------------------------------------------------------+
|                   格式转换层                            |
|  L8/A8 ImageView Swizzle, RGB565 量化/反量化           |
|  L8/A8 Shader 输出转换 (luminance/alpha -> R通道)      |
+------------------------------------------------------+
|                   CPU 验证层                           |
|  Expected Buffer Tracker -> 逐像素比对                  |
|  Vulkan 规范一致的 LINEAR 采样 + 9种混合模式             |
+------------------------------------------------------+
```

---

## 2. Clear 实现

### 2.1 基本流程

```
vg_lite_clear(target, rect, color)
  -> vg_lite_vulkan_set_render_target()   // 开始 render pass
  -> vkCmdClearAttachments()              // Vulkan 原生清除
  -> (不结束 render pass，等 finish 统一提交)
```

### 2.2 L8/A8 颜色转换

Vulkan 的 `vkClearAttachments` 对 `VK_FORMAT_R8_UNORM` 只写 R 通道。但 VGLite 的 L8 语义要求 R = 亮度，A8 要求 R = Alpha。因此需要在 CPU 侧预转换：

**L8 清除颜色转换**（ITU-R BT.601 亮度公式）：
```
R_input = color[0:7]      // 输入 ARGB 的 R 分量
G_input = color[8:15]     // 输入 ARGB 的 G 分量
B_input = color[16:23]    // 输入 ARGB 的 B 分量
Luminance = 0.2126 * R + 0.7152 * G + 0.0722 * B
ClearValue.R = Luminance / 255.0   // float32 传给 Vulkan
ClearValue.G = 0.0
ClearValue.B = 0.0
ClearValue.A = 1.0
```

**A8 清除颜色转换**：
```
Alpha = color[24:31]      // 输入 ARGB 的 A 分量
ClearValue.R = Alpha / 255.0
ClearValue.G = 0.0
ClearValue.B = 0.0
ClearValue.A = 1.0
```

### 2.3 矩形裁剪

当传入非 NULL 的 `rect` 参数时，需要裁剪到缓冲区边界：
```
if (rect->x < 0) offset_x = -rect->x  // 左侧裁剪
if (rect->y < 0) offset_y = -rect->y  // 顶部裁剪
width  = min(rect->width,  buffer.width  - rect->x)
height = min(rect->height, buffer.height - rect->y)
```

---

## 3. Blit 实现

### 3.1 矩阵变换体系

#### 3.1.1 VGLite 矩阵约定

VGLite 使用 3x3 矩阵 `M`，约定为 **正向映射**：
```
dst_pixel = M * src_pixel
```

矩阵操作链（左乘）：
```
identity()           -> M = I
translate(tx, ty)    -> M = M * T(tx,ty)
scale(sx, sy)        -> M = M * S(sx,sy)
rotate(theta)        -> M = M * R(theta)
```

#### 3.1.2 Shader 矩阵推导

Shader 需要将归一化的 fragment 位置 `[0,1]` 映射到源纹理 UV `[0,1]`：

```
目标: src_uv = f(frag_pos)

已知:
  dst_pixel = M * src_pixel          (VGLite 约定)
  frag_pos = dst_pixel / dst_size    (归一化)
  src_uv   = src_pixel / src_size    (归一化)

推导:
  src_pixel = inv(M) * dst_pixel
            = inv(M) * (frag_pos * dst_size)
  src_uv    = src_pixel / src_size
            = (1/src_size) * inv(M) * (frag_pos * dst_size)

合并为单次矩阵乘法:
  shader_mat = Scale(1/src_w, 1/src_h) * inv(M) * Scale(dst_w, dst_h)

  src_uv = shader_mat * vec3(frag_pos, 1.0)
```

**关键推导**：`shader_mat` 将两次缩放和一次逆矩阵合并，使 GPU 只需一次 `mat3 * vec3` 即可完成从 fragment 位置到源 UV 的映射。

#### 3.1.3 矩阵求逆

使用余子式法（cofactor expansion）计算 3x3 矩阵逆：

```
det(M) = m00*(m11*m22 - m12*m21) - m01*(m10*m22 - m12*m20) + m02*(m10*m21 - m11*m20)

inv(M)[i][j] = Cofactor(j,i) / det(M)    // 注意转置
```

**奇异矩阵保护**：当 `|det| < 1e-7` 时，强制 `det = 1e-7`，避免除零。

#### 3.1.4 Push Constant 布局

```c
struct PushConstants {     // 共 68 字节
    float m[12];          // mat3，按 vec4 列对齐 (48 字节)
    int   blend_mode;     // 4 字节
    uint  color;          // 4 字节
    int   image_mode;     // 4 字节
    int   filter_mode;    // 4 字节
    int   flags;          // 4 字节
};
```

矩阵存储为列主序（Vulkan/GLSL 要求）：
```c
pc.m[j*4+i] = shader_mat[i][j]   // 行->列转换，步长4（vec4对齐）
```

`mat3` 在 GLSL 中每列按 `vec4` 对齐（16 字节），因此 3 列共 48 字节，实际 `float m[12]` 中索引 3,7,11 为 padding。

**Flags 位掩码**：
| 位 | 值 | 含义 |
|----|----|------|
| 0 | 1 | FLAG_OUTPUT_L8 — 目标为 L8 格式 |
| 1 | 2 | FLAG_OUTPUT_A8 — 目标为 A8 格式 |
| 2 | 4 | FLAG_NATIVE_BLEND — 使用 GPU 原生混合 |

---

### 3.2 Shader 设计

#### 3.2.1 Vertex Shader

使用单三角形覆盖全屏（比四边形少 3 个顶点）：

```glsl
const vec2 positions[3] = vec2[3](
    vec2(-1.0, -1.0),   // 左下
    vec2( 3.0, -1.0),   // 右侧延伸（覆盖右半屏）
    vec2(-1.0,  3.0)    // 上方延伸（覆盖上半屏）
);

frag_pos = pos * 0.5 + 0.5;   // NDC [-1,1] -> 归一化 [0,1]
```

三个顶点构成一个超大三角形，仅光栅化覆盖视口范围的部分。比传统四边形（2 个三角形、6 个顶点）更高效。

#### 3.2.2 Fragment Shader 主流程

```
main():
  1. src_coords = matrix * vec3(frag_pos, 1.0)
  2. 透视除法（仅非仿射变换）:
     if |src_coords.z - 1.0| < 0.001:
         src_uv = src_coords.xy              // 仿射：直接使用
     else:
         src_uv = src_coords.xy / src_coords.z  // 透视：除以 w
  3. 边界检查 (epsilon = 0.001):
     if src_uv < -0.001 or > 1.001:
         native_blend -> 输出 (0,0,0,0)      // 让 Vulkan blend 保留 dst
         shader_blend -> 读取 dst_texture     // 直接返回 dst 像素
  4. 纹理采样:
     src = texture(src_texture, src_uv)
  5. 图像模式:
     src = apply_image_mode(src, color)
  6. 分支:
     native_blend -> PREMULTIPLY预乘 -> L8/A8输出转换 -> 直接输出
     shader_blend -> 读取dst_texture -> 混合计算 -> L8/A8输出转换
```

#### 3.2.3 透视除法详解

**为什么需要**：3x3 矩阵的第三行 `[m20, m21, m22]` 在仿射变换时为 `[0, 0, 1]`，此时 `z = 1.0`，除法无效果。但在透视变换中 `m20 != 0` 或 `m21 != 0`，导致 `z != 1.0`，必须除以 z 才能得到正确的 UV。

**仿射优化**：当 `|z - 1.0| < 0.001` 时跳过除法，避免浮点除法引入微小的精度误差（在边界像素上可能导致 `uv` 偏移 +-1e-8 而被误判为越界）。

#### 3.2.4 边界 Epsilon

**问题**：CPU 和 GPU 的浮点计算顺序不同，在变换边界处累积误差方向可能相反。例如 CPU 算出 `src_uv.y = 0.0`（刚好在范围内），GPU 算出 `src_uv.y = -1e-8`（刚好越界），导致 GPU 返回背景色而 CPU 采样了源像素。

**解决**：边界检查使用 +-0.001 容差，略微越界的 UV 仍被视为有效。Vulkan 的 `clamp-to-edge` 寻址模式会正确处理这些边界采样。

#### 3.2.5 图像模式（Image Mode）

| 模式 | 值 | 效果 |
|------|----|------|
| NORMAL | 0x1F00 | 原样输出 |
| MULTIPLY | 0x1F01 | src.rgb *= mix_color.rgb |
| STENCIL | 0x1F02 | src.a *= mix_color.a |

---

### 3.3 混合模式

#### 3.3.1 Native Blend（GPU 原生混合）

利用 Vulkan 的 `VkPipelineColorBlendAttachmentState` 直接在 GPU 上完成混合，**无需拷贝目标缓冲区**（最大性能提升点）。

| 混合组 | VGLite 模式 | Vulkan 配置 | 公式 |
|--------|------------|-------------|------|
| BG_SRC_OVER | SRC_OVER, NORMAL_LVGL | src=ONE, dst=ONE_MINUS_SRC_ALPHA | S + D*(1-Sa) |
| BG_DST_OVER | DST_OVER | src=ONE_MINUS_DST_ALPHA, dst=ONE | S*(1-Da) + D |
| BG_ADDITIVE | ADDITIVE | src=ONE, dst=ONE | S + D |
| BG_SUBTRACT | SUBTRACT | src=ZERO, dst=ONE_MINUS_SRC_ALPHA | D*(1-Sa) |
| BG_SHADER | 其余所有 | blendEnable=FALSE | Shader 内计算 |

**关键**：VGLite 的 SRC_OVER 使用**非预乘**公式 `S + D*(1-Sa)`，因此 Vulkan 的 `srcBlendFactor` 是 `ONE` 而非 `SRC_ALPHA`。这与常见的预乘 SRC_OVER（`S*Sa + D*(1-Sa)`）不同。

**限制**：Native blend 仅适用于 BGRA8888 目标。L8/A8/RGB565 目标回退到 shader blend，因为 GPU 在 4 通道上混合但只存储部分结果（L8 只写 R，RGB565 打包为 16 位）。

**Native Blend 越界处理**：当源纹理 UV 越界时，shader 输出 `(0,0,0,0)`（而非源像素颜色）。这样 Vulkan 的混合公式自然保留 dst：
- SRC_OVER: `0 + D*(1-0) = D`
- ADDITIVE: `0 + D = D`
- SUBTRACT: `D*(1-0) = D`

但 SRC_IN 和 DST_IN 不适合 native blend，因为 `0*Da = 0`（丢失 dst），`D*0 = 0`（也丢失 dst）。

#### 3.3.2 Shader Blend（Shader 内混合）

对于不支持 native blend 的模式，shader 读取目标纹理并手动计算混合：

```glsl
vec4 dst = texture(dst_texture, frag_pos);
vec4 result = blend_func(src, dst);
```

**代价**：需要在 blit 之前将目标缓冲区拷贝到临时图像，作为 `dst_texture` 传入。这涉及：
1. 创建临时图像（LINEAR tiling，HOST_VISIBLE 内存）
2. `vkCmdCopyImage` 拷贝目标到临时图像
3. 布局转换
4. 绘制完成后销毁临时图像

#### 3.3.3 完整混合模式列表

**非预乘混合** (`blend_non_premul`):

| 值 | 模式 | 公式 | 说明 |
|----|------|------|------|
| 0 | NONE | S | 直接拷贝 |
| 1 | SRC_OVER | S + D*(1-Sa) | 标准 Porter-Duff |
| 2 | DST_OVER | S*(1-Da) + D | |
| 3 | SRC_IN | S * Da | |
| 4 | DST_IN | D * Sa | |
| 5 | MULTIPLY | S*(1-Da) + D*(1-Sa) + S*D | |
| 6 | SCREEN | S + D - S*D | |
| 7 | DARKEN | min(srcOver, dstOver) | 非简单 min(S,D) |
| 8 | LIGHTEN | max(srcOver, dstOver) | 非简单 max(S,D) |
| 9 | ADDITIVE | S + D | 不截断 |
| 10 | SUBTRACT | D*(1-Sa) | |
| 11 | NORMAL_LVGL | S*Sa + D*(1-Sa) | 预乘版本，Alpha 强制 0xFF |
| 12-14 | *_LVGL | 对应模式 + Alpha=1.0 | LVGL 专用 |

**预乘混合** (`blend_premul`, OpenVG)：

先反预乘源（`S_rgb / Sa`），执行标准混合，再预乘结果。适用于 `VG_LITE_BLEND_OPENVG_*` 系列（0x2000+）。

---

### 3.4 L8/A8 渲染目标支持

#### 3.4.1 问题

Vulkan 的 `VK_FORMAT_R8_UNORM` 帧缓冲只存储 R 通道。但 VGLite 的语义是：
- **L8**：R 通道存储亮度值，R = G = B = L，A = 0xFF
- **A8**：R 通道存储 Alpha 值，R = G = B = 0，A = alpha

GPU shader 输出 RGBA 四通道，Vulkan 只保存 R，导致 G/B/A 信息丢失。

#### 3.4.2 解决方案

**Shader 输出转换**：在 fragment shader 末尾，根据目标格式转换输出：

```glsl
if (flags & FLAG_OUTPUT_L8) {
    float lum = 0.2126 * src.r + 0.7152 * src.g + 0.0722 * src.b;
    out_color = vec4(lum, 0.0, 0.0, src.a);   // L8: 亮度写R，保留Alpha
}
else if (flags & FLAG_OUTPUT_A8) {
    out_color = vec4(src.a, 0.0, 0.0, src.a);  // A8: Alpha写R
}
```

**Native Blend 时 Alpha 处理**：L8/A8 在 native blend 模式下，Alpha 不能硬编码为 1.0，因为 Vulkan 的混合公式需要正确的 Alpha 值。例如 SRC_OVER 需要源 Alpha 来计算 `D * (1-Sa)`。

#### 3.4.3 双 ImageView 策略

Vulkan 规范要求帧缓冲附件使用 **identity componentMapping**，但纹理采样需要 **swizzle**：

| 格式 | 帧缓冲 View (identity) | 纹理采样 View (swizzle) |
|------|----------------------|----------------------|
| L8 | R->R, G->G, B->B, A->A | R->R, G->R, B->R, A->ONE |
| A8 | R->R, G->G, B->B, A->A | R->ZERO, G->R, B->R, A->ONE |

**L8 Swizzle 解释**：采样 L8 纹理时，R 通道复制到 G 和 B，Alpha 设为 1.0。这样 shader 中读出的颜色 `vec4(L, L, L, 1.0)` 符合亮度语义。

**A8 Swizzle 解释**：采样 A8 纹理时，R 通道（存储的 Alpha 值）复制到 G 和 B（方便调试），Alpha 设为 1.0。shader 根据 `FLAG_OUTPUT_A8` 标志知道 R 通道实际是 Alpha 值。

---

### 3.5 RGB565 渲染目标支持

#### 3.5.1 问题

RGB565 是 16 位格式（R5 G6 B5），无 Alpha 通道。Vulkan 的 `VK_FORMAT_R5G6B5_UNORM_PACK16` 帧缓冲只存储 RGB，不支持 Alpha 混合。

#### 3.5.2 解决方案

- **Native Blend 禁用**：RGB565 目标强制使用 shader blend（`BG_SHADER`），因为 GPU 原生混合在 4 通道上进行但 RGB565 只有 3 通道且打包为 16 位，混合结果不正确
- **Shader 输出**：shader 正常输出 RGBA，Vulkan 在写入帧缓冲时自动截断 Alpha 并量化为 5/6/5 位
- **CPU 验证**：量化误差容忍度适当放宽（RGB565 每通道最多 +-4 误差）

---

### 3.6 Pipeline Cache 设计

#### 3.6.1 缓存键

```c
typedef struct {
    VkFormat       format;       // 目标格式 (BGRA8888, R8_UNORM, R5G6B5)
    blend_group_t  blend_group;  // 混合组 (BG_SRC_OVER, BG_SHADER, etc.)
} pipeline_key_t;
```

#### 3.6.2 混合组分类

将 15 种混合模式归为 5 个混合组，减少 pipeline 数量：

| 混合组 | 包含的模式 | Vulkan Blend 配置 |
|--------|-----------|------------------|
| BG_SRC_OVER | SRC_OVER, NORMAL_LVGL | src=ONE, dst=ONE_MINUS_SRC_ALPHA |
| BG_DST_OVER | DST_OVER | src=ONE_MINUS_DST_ALPHA, dst=ONE |
| BG_ADDITIVE | ADDITIVE | src=ONE, dst=ONE |
| BG_SUBTRACT | SUBTRACT | src=ZERO, dst=ONE_MINUS_SRC_ALPHA |
| BG_SHADER | 其余所有 | blendEnable=FALSE |

同组内的模式共享同一个 Vulkan Pipeline（因为 Vulkan blend 配置相同），shader 内根据 `blend_mode` uniform 选择具体混合公式。

#### 3.6.3 懒创建

Pipeline 在首次使用时创建（lazy initialization），避免启动时创建所有可能的组合。缓存上限 64 条目，防止内存泄漏。

---

## 4. CPU 验证系统

### 4.1 Expected Buffer Tracker

每个 VGLite 缓冲区关联一个 CPU 侧的 "expected buffer"，跟踪 GPU 应该产生的结果：

```
vg_lite_clear()  -> 在 expected buffer 上执行 CPU 侧清除
vg_lite_blit()   -> 在 expected buffer 上执行 CPU 侧混合
vg_lite_finish() -> 将 expected buffer 与 GPU 输出逐像素比对
```

### 4.2 CPU 侧 LINEAR 采样

CPU 验证必须精确模拟 Vulkan 的 `VK_FILTER_LINEAR` 采样行为：

```
vulkan_linear_sample(u, v, width, height, pixels):

  1. 像素中心偏移: u = u * width  - 0.5
                    v = v * height - 0.5

  2. 4 个相邻像素:
     i0 = floor(u), i1 = i0 + 1
     j0 = floor(v), j1 = j0 + 1

  3. 小数部分: fu = u - i0, fv = v - j0

  4. Clamp-to-edge:
     i0 = clamp(i0, 0, width-1)
     i1 = clamp(i1, 0, width-1)
     j0 = clamp(j0, 0, height-1)
     j1 = clamp(j1, 0, height-1)

  5. 双线性插值:
     result = (1-fu)*(1-fv)*p[i0][j0] + fu*(1-fv)*p[i1][j0]
            + (1-fu)*fv*p[i0][j1]     + fu*fv*p[i1][j1]
```

**关键**：像素中心偏移 `-0.5` 是 Vulkan 规范要求的，确保采样坐标正确对齐纹素中心。省略此偏移会导致 CPU 和 GPU 在边界像素处出现不一致。

### 4.3 比对容差

| 场景 | 基础容差 | 额外容差 | 原因 |
|------|---------|---------|------|
| 无变换（恒等矩阵） | 2 | 0 | 基本量化误差 |
| 缩放/旋转/透视 | 2 | +4 | 浮点累积误差 |
| Native Blend | 2 | +1 | GPU 混合额外精度损失 |
| RGB565 目标 | 2 | +4 | 5/6/5 位量化误差 |

### 4.4 9 种 CPU 混合实现

CPU 验证实现了 9 种混合模式，与 shader 中的 `blend_non_premul` 函数对应：

| 模式 | CPU 公式 |
|------|---------|
| NONE | `S` |
| SRC_OVER | `S + D * (1 - Sa/255)` |
| DST_OVER | `S * (1 - Da/255) + D` |
| SRC_IN | `S * Da / 255` |
| DST_IN | `D * Sa / 255` |
| MULTIPLY | `S*(1-Da/255) + D*(1-Sa/255) + S*D/255` |
| ADDITIVE | `min(S + D, 255)` |
| SUBTRACT | `D * (1 - Sa/255)` |
| NORMAL_LVGL | `S*Sa/255 + D*(1-Sa/255)` |

CPU 使用整数运算（0-255 范围），GPU 使用浮点，两者结果在容差范围内应一致。

---

## 5. 已知限制与待改进

| 项目 | 说明 |
|------|------|
| 采样器固定 LINEAR | GPU 采样器始终使用 `VK_FILTER_LINEAR`，忽略 `POINT`/`BI_LINEAR` 参数 |
| Shader Blend 性能 | 需要 `vkCmdCopyImage` 拷贝目标缓冲区，额外开销 |
| 临时图像分配 | 每次 shader blend 都创建/销毁临时图像，可改为池化复用 |
| 透视除法精度 | 边界 epsilon 0.001 是经验值，极端变换可能需要调整 |
| SPIR-V 字数 | `g_frag_spv_size` 必须是字数（word count），不是字节数 |
