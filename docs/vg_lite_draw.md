# vg_lite_draw 模块详细原理

## 目录

1. [总览](#1-总览)
2. [API 入口](#2-api-入口)
3. [路径处理流水线](#3-路径处理流水线)
4. [纯色填充：vg_lite_draw_impl](#4-纯色填充vg_lite_draw_impl)
5. [图案填充：vg_lite_draw_pattern](#5-图案填充vg_lite_draw_pattern)
6. [Shader 详解](#6-shader-详解)
7. [资源管理与生命周期](#7-资源管理与生命周期)
8. [已知限制与待实现项](#8-已知限制与待实现项)

---

## 1. 总览

`vg_lite_draw` 负责矢量路径的 GPU 光栅化绘制。整体架构为经典的 **CPU 曲面细分 + GPU 光栅化** 两阶段流水线：

```
vg_lite_path_t (VLC 编码)
      │
      ▼
┌─────────────┐     ┌──────────────┐     ┌──────────────────┐
│ VLC Parser  │ ──► │ Tessellator  │ ──► │  Vulkan Render   │
│ (CPU 解码)   │     │ (CPU 三角化)  │     │  (GPU 绘制)      │
└─────────────┘     └──────────────┘     └──────────────────┘
                                                │
                                    ┌───────────┴───────────┐
                                    │                       │
                              纯色填充               图案填充
                          (stencil buffer)      (texture sampling)
```

涉及的核心文件：

| 文件 | 职责 |
|------|------|
| `src/vlc_parser.c/h` | VLC 路径解码器 |
| `src/tessellator.c/h` | 三角化器（fan tessellation + bezier 展平） |
| `src/vg_lite_draw.c` | 主绘制逻辑（纯色填充 + 图案填充） |
| `shaders/draw.vert` / `draw.frag` | 纯色填充 shader |
| `shaders/pattern.vert` / `pattern.frag` | 图案填充 shader |
| `src/draw_vert_spv.h` / `draw_frag_spv.h` | 编译后的 SPV 头文件 |

---

## 2. API 入口

### 2.1 vg_lite_draw（纯色填充）

```
vg_lite_draw(target, path, fill_rule, matrix, blend, color)
```

定义在 `vg_lite.c:657`，直接转发到 `vg_lite_draw_impl()`（`vg_lite_draw.c:321`）。

| 参数 | 类型 | 说明 |
|------|------|------|
| `target` | `vg_lite_buffer_t*` | 渲染目标缓冲区 |
| `path` | `vg_lite_path_t*` | VLC 编码的矢量路径 |
| `fill_rule` | `vg_lite_fill_t` | 填充规则（EVEN_ODD / NON_ZERO）—— **当前被忽略** |
| `matrix` | `vg_lite_matrix_t*` | 变换矩阵（可为 NULL） |
| `blend` | `vg_lite_blend_t` | 混合模式 —— **当前被忽略** |
| `color` | `vg_lite_color_t` | 填充颜色，格式 `0xAARRGGBB` |

### 2.2 vg_lite_draw_pattern（图案填充）

```
vg_lite_draw_pattern(target, path, fill_rule, path_matrix, pattern_image,
                     pattern_matrix, blend, pattern_mode, pattern_color, color, filter)
```

定义在 `vg_lite_draw.c:537`。

| 参数 | 说明 |
|------|------|
| `pattern_image` | 图案纹理图像 |
| `pattern_matrix` | 图案坐标变换矩阵 |
| `pattern_mode` | `COLOR(0x1D00)` / `PAD(0x1D01)` / `REPEAT(0x1D02)` / `REFLECT(0x1D03)` |
| `pattern_color` | COLOR 模式下超出纹理范围的回退颜色 |

---

## 3. 路径处理流水线

### 3.1 VLC 解码器（vlc_parser.c）

VGLite 使用变长命令（VLC）编码矢量路径。每个命令由 1 字节操作码 + N 个坐标值组成，坐标值的数据类型由 `vg_lite_path_t.format` 指定。

**支持的命令：**

| 操作码 | 含义 | 坐标数 |
|--------|------|--------|
| `0x00` END | 路径结束 | 0 |
| `0x01` CLOSE | 闭合当前子路径 | 0 |
| `0x02` MOVE | 移动到 (x, y) | 2 |
| `0x04` LINE | 直线到 (x, y) | 2 |
| `0x06` QUAD | 二次贝塞尔曲线 | 4 (控制点 + 终点) |
| `0x08` CUBIC | 三次贝塞尔曲线 | 6 (两个控制点 + 终点) |

还有对应的相对坐标版本（`_REL` 后缀，0x03/0x05/0x07/0x09），但当前实现仅解析绝对坐标版本。

**坐标格式支持：**

| 格式 | 字节大小 | C 类型 |
|------|----------|--------|
| `VG_LITE_S8` | 1 | `int8_t` |
| `VG_LITE_S16` | 2 | `int16_t` |
| `VG_LITE_S32` | 4 | `int32_t` |
| `VG_LITE_FP32` | 4 | `float` |

**二次贝塞尔转三次贝塞尔：**

QUAD 命令在解析阶段即被转换为 CUBIC，使用 2/3 近似公式：

```
qc0 = prev + 2/3 * (control - prev)
qc1 = end  + 2/3 * (control - end)
```

这样后续曲面细分器只需处理三次贝塞尔，简化了代码路径。

**输出：** `VlcPath` 结构体，包含命令数组、命令数和包围盒。

### 3.2 三角化器（tessellator.c）

将 `VlcPath` 转换为三角形网格（`TessGeometry`）。

**核心算法：扇形三角化（Fan Tessellation）**

```
        v2
       / \
      /   \
     /     \
    v0──────v1
     \     /
      \   /
       \ /
        v3
```

所有三角形共享第一个顶点 `v0`（`first_idx`），形成扇形：
- `triangle(v0, prev, curr)` 对每个新顶点生成

**三次贝塞尔展平：**

```c
flatten_cubic(p0, c1, c2, p3, geom, first_idx, &prev_idx)
```

1. 估算曲线偏离直线的程度 `deviation`
2. 计算需要的段数 `segments = deviation / tolerance + 1`（最小 2，最大 256）
3. 沿曲线等间距采样 `B(t)`，每个采样点作为新顶点加入，生成扇形三角形

贝塞尔求值公式：
```
B(t) = (1-t)^3 * P0 + 3(1-t)^2*t * P1 + 3(1-t)*t^2 * P2 + t^3 * P3
```

**输出：** `TessGeometry`，包含顶点数组 `vertices[]`（float2）、索引数组 `indices[]`（uint32）、顶点/索引计数、包围盒。

> **注意：** 扇形三角化仅适用于**凸多边形**。对于凹多边形或复杂路径，需要实现更高级的曲面细分算法（如 ear-clipping 或 monotone polygon tessellation）。

---

## 4. 纯色填充：vg_lite_draw_impl

### 4.1 整体流程

```
1. VLC 解码 + 三角化
2. 创建/上传 VBO + IBO
3. 开启命令录制
4. 设置渲染目标
5. 设置 viewport + scissor
6. 计算变换矩阵 (screen_to_ndc × matrix)
7. 清除 stencil buffer
8. ── Pass 1: Stencil pass ──
9. ── Pass 2: Cover pass  ──
10. 注册延迟释放缓冲区
```

### 4.2 Pipeline 初始化（init_draw_pipeline）

首次调用时创建 3 条流水线（`vg_lite_draw.c:50-240`）：

#### Pipeline 1: stencil_pipeline（模板通道）

| 状态 | 配置 |
|------|------|
| 图元拓扑 | `TRIANGLE_LIST` |
| MSAA | 4x |
| Stencil | **INVERT** on pass, compare ALWAYS, mask 0x01 |
| Color Write | **关闭**（`colorWriteMask = 0`） |
| Blend | 禁用 |

**用途：** 将三角化后的路径三角形绘制到 stencil buffer。每个三角形覆盖的像素 LSB 被 INVERT（0→1→0）。对于重叠区域，奇数次覆盖 = 1，偶数次 = 0 → 实现 **EVEN-ODD** 填充规则。

#### Pipeline 2: cover_pipeline（覆盖通道）

| 状态 | 配置 |
|------|------|
| 图元拓扑 | `TRIANGLE_STRIP`（全屏四边形） |
| Stencil | **NOT_EQUAL 0** (仅 stencil ≠ 0 的像素通过), passOp = ZERO（使用后清除） |
| Color Write | **开启** |
| Blend | SRC_OVER (src=ONE, dst=ONE_MINUS_SRC_ALPHA) |

**用途：** 绘制覆盖全屏的四边形（`[-1,-1]` 到 `[1,1]`），但只在 stencil buffer 标记为填充的区域写入颜色，同时清除 stencil。

#### Pipeline 3: fill_pipeline（直接填充，未使用）

创建了但代码中**从未使用**。为单 pass 直接渲染颜色，不经过 stencil 流程。

### 4.3 矩阵变换

Push constants 结构（56 字节）：

```c
struct {
    float m0[4];    // 矩阵第 0 行（第 4 分量填充 0）
    float m1[4];    // 矩阵第 1 行
    float m2[4];    // 矩阵第 2 行
    int   blend;    // 混合模式（当前固定为 0）
    uint32_t color; // 填充颜色 0xAARRGGBB
} pc_data;
```

矩阵计算：
```c
screen_to_ndc = | 2/w    0    -1 |
                |  0   2/h   -1 |
                |  0    0     1 |

combined = screen_to_ndc × matrix   // 矩阵左乘，将路径坐标变换到 NDC
```

其中 `matrix` 来自 API 参数，若为 NULL 则使用单位矩阵。

### 4.4 两 Pass 渲染流程

```
┌─────────────────────────────────────────────────────┐
│ Pass 1: Stencil                                      │
│                                                      │
│  vkCmdClearAttachments(stencil = 0)                  │
│  Bind stencil_pipeline                               │
│  Push constants: combined matrix + color             │
│  Bind path VBO/IBO                                   │
│  vkCmdDrawIndexed(path triangles)                    │
│                                                      │
│  效果: 路径覆盖区域 stencil LSB = 1 (偶数次翻转后=0) │
│        使用 INVERT 实现 even-odd 规则                │
├─────────────────────────────────────────────────────┤
│ Pass 2: Cover                                        │
│                                                      │
│  Bind cover_pipeline                                 │
│  Push constants: identity matrix + color             │
│  Bind cover VBO/IBO (全屏四边形)                     │
│  vkCmdDrawIndexed(6 indices → 全屏四边形)            │
│                                                      │
│  效果: stencil ≠ 0 的像素写入颜色, stencil 归零      │
└─────────────────────────────────────────────────────┘
```

Cover VBO 包含全屏四边形的 4 个顶点：`{(-1,-1), (1,-1), (1,1), (-1,1)}`，使用 `TRIANGLE_STRIP` 拓扑 + 6 个索引 `{0,1,2, 0,2,3}` 渲染为两个三角形。

### 4.5 颜色格式

颜色为 `uint32_t` 格式 `0xAARRGGBB`，在 vertex shader 中解包：

```glsl
vec4 unpackColor(uint c) {
    float b = float((c)       & 0xFF) / 255.0;
    float g = float((c >>  8) & 0xFF) / 255.0;
    float r = float((c >> 16) & 0xFF) / 255.0;
    float a = float((c >> 24) & 0xFF) / 255.0;
    return vec4(b, g, r, a);
}
```

> 注意解包顺序为 BGRA（因为 VGLite 颜色字节序为 BGRA），输出 `vec4(b, g, r, a)`。

---

## 5. 图案填充：vg_lite_draw_pattern

### 5.1 整体流程

```
1. VLC 解码 + 三角化
2. 创建/上传 VBO + IBO
3. 开启命令录制
4. 设置渲染目标
5. 分配 Descriptor Set（绑定图案纹理 sampler）
6. 计算路径变换矩阵 (path_screen_to_ndc × path_matrix)
7. 计算图案 UV 变换矩阵 (pattern_matrix 逆 × 归一化)
8. 单 Pass 渲染：绘制路径三角形, fragment shader 采样图案纹理
```

### 5.2 与纯色填充的区别

| 特性 | 纯色填充 | 图案填充 |
|------|----------|----------|
| Pass 数 | 2（stencil + cover） | 1（直接渲染） |
| 填充规则 | EVEN_ODD（通过 stencil） | 无（直接三角形覆盖） |
| 颜色来源 | push constant 单色 | 纹理采样 |
| 描述符集 | 无 | 1 个（图案 sampler） |
| 混合 | 固定 SRC_OVER | 支持 blend mode |

### 5.3 图案坐标变换

这是整个图案填充中最复杂的部分。

**路径矩阵**（path_matrix）：变换路径顶点到屏幕坐标，再变换到 NDC：
```
path_combined = screen_to_ndc × path_matrix
```

**图案矩阵**（pattern_matrix）：将屏幕像素坐标映射到图案纹理 UV 坐标：
```
1. 求逆: pattern_inv = inverse(pattern_matrix)
2. 归一化: pattern_inv /= (pattern_width, pattern_height)
   → 使 UV 范围归一化到 [0, 1]
```

在 vertex shader 中：
```glsl
// 路径顶点 → NDC
vec3 transformed = pc.path_matrix * vec3(in_pos, 1.0);
gl_Position = vec4(transformed.xy, 0.0, 1.0);

// NDC → 屏幕像素坐标 → 图案 UV
vec2 screen_norm = (transformed.xy + 1.0) * 0.5;
vec2 screen_pixel = screen_norm * vec2(target_width, target_height);
vec3 pattern_coords = pc.pattern_matrix * vec3(screen_pixel, 1.0);
pattern_uv = pattern_coords.xy / pattern_coords.z;
```

### 5.4 图案模式

在 fragment shader（`pattern.frag`）中处理 4 种模式：

| 模式 | Shader 值 | 行为 |
|------|-----------|------|
| COLOR | 0 | UV 在 [0,1] 内采样纹理；超出范围使用 `pattern_color` |
| PAD | 1 | `clamp(uv, 0, 1)` —— 边缘像素延伸 |
| REPEAT | 2 | `fract(uv)` —— 平铺重复 |
| REFLECT | 3 | `abs(fract(uv*2)*2 - 1)` —— 镜像翻转 |

---

## 6. Shader 详解

### 6.1 draw.vert（纯色填充顶点着色器）

```glsl
layout(push_constant) uniform DrawPushConstants {
    vec4  m0, m1, m2;   // 3×3 变换矩阵（列优先存储，每行 vec4 填充）
    int   blend_mode;
    uint  color;         // 0xAARRGGBB
} pc;

void main() {
    mat3 matrix = transpose(mat3(pc.m0.xyz, pc.m1.xyz, pc.m2.xyz));
    vec3 transformed = matrix * vec3(in_pos, 1.0);
    gl_Position = vec4(transformed.xy, 0.0, 1.0);
    vert_color = unpackColor(pc.color);
}
```

- 矩阵存储为 3 个 `vec4`（浪费 3 个 float），transpose 恢复为 mat3
- 颜色在 vertex stage 解包，通过 `vert_color` varying 传给 fragment

### 6.2 draw.frag（纯色填充片元着色器）

```glsl
void main() {
    out_color = vert_color;  // 直接输出颜色，不做额外处理
}
```

### 6.3 pattern.vert（图案填充顶点着色器）

见 [5.3 节](#53-图案坐标变换)，完成路径变换 + 图案 UV 计算。

### 6.4 pattern.frag（图案填充片元着色器）

```glsl
void main() {
    vec2 final_uv = apply_pattern_mode(pattern_uv);

    if (pattern_mode == COLOR && !is_inside_pattern(pattern_uv)) {
        out_color = vert_color;  // 超出范围用 pattern_color
        return;
    }

    out_color = texture(pattern_texture, final_uv);
}
```

---

## 7. 资源管理与生命周期

### 7.1 全局状态

```c
static draw_pipeline_t g_draw_pipeline = {0};
```

包含所有 pipeline 资源，在首次 `vg_lite_draw_impl()` 调用时懒初始化。

### 7.2 延迟释放缓冲区

每帧绘制的 VBO/IBO 不能立即释放（GPU 可能仍在使用），因此使用 pending buffer 队列：

```c
#define MAX_PENDING_BUFFERS 512

typedef struct {
    VkBuffer vbo; VkDeviceMemory vbo_mem;
    VkBuffer ibo; VkDeviceMemory ibo_mem;
} pending_buffer_t;
```

| 函数 | 调用时机 |
|------|----------|
| `add_pending_buffer()` | 每次 `vg_lite_draw_impl` / `vg_lite_draw_pattern` 结束时 |
| `vg_lite_draw_cleanup_pending_buffers()` | 命令提交后（`vg_lite_vulkan_submit_command` 后） |
| `vg_lite_draw_cleanup()` | `vg_lite_close()` 时，销毁所有 pipeline 资源 |

### 7.3 生命周期图

```
vg_lite_init()
  └─ 首次 vg_lite_draw() 调用
       └─ init_draw_pipeline()  ← 创建 3 条 pipeline + cover VBO/IBO

每次 vg_lite_draw():
  ├─ create_vertex_buffer()  ← 创建临时 VBO/IBO
  ├─ upload_geom()           ← 填充顶点/索引数据
  ├─ vkCmdDraw*()            ← GPU 执行绘制
  └─ add_pending_buffer()    ← 加入延迟释放队列

vg_lite_vulkan_submit_command()  (外部调用)
  └─ vg_lite_draw_cleanup_pending_buffers()  ← 释放本帧 VBO/IBO

vg_lite_close()
  └─ vg_lite_draw_cleanup()  ← 销毁 pipeline/shader/layout/cover buffer
```

---

## 8. 已知限制与待实现项

### 8.1 功能缺失

| 项目 | 当前状态 | 影响 |
|------|----------|------|
| `fill_rule` 参数 | **被忽略**（`vg_lite_draw.c:325`） | NON_ZERO 规则未实现，统一使用 stencil INVERT 的 EVEN_ODD |
| `blend` 参数 | **被忽略**（`vg_lite_draw.c:325`） | 固定使用 pipeline 配置的 SRC_OVER，不支持其他混合模式 |
| 凹多边形 | 扇形三角化会产生错误三角形 | 复杂路径渲染不正确 |
| 相对坐标命令 (`_REL`) | 解析器跳过不处理 | 包含相对坐标的路径数据丢失 |
| `fill_pipeline` | 已创建但未使用 | 可用于 NON_ZERO 或非 stencil 场景 |
| 线条绘制（stroke） | 未实现 | 仅支持填充 |

### 8.2 性能优化空间

| 项目 | 当前实现 | 优化方向 |
|------|----------|----------|
| 覆盖区域 | 绘制全屏四边形 | 改用路径包围盒，减少 fragment shader 调用 |
| VBO/IBO | 每帧 alloc/free | 使用 ring buffer 或 VMA |
| Stencil 清除 | `vkCmdClearAttachments` | 已合理 |
| MSAA | 硬编码 4x | 可配置化 |

### 8.3 图案填充特有限制

- `fill_rule` 参数被忽略（`vg_lite_draw.c:549`）
- 不使用 stencil buffer，因此图案填充**不支持** even-odd 填充规则
- `color` 和 `filter` 参数被忽略
- blend_mode 传入 shader 但 shader 中未实际使用（pattern.frag 不做混合计算）
