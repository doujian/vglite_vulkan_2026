# OpenGL ES 3.2 vs 3.1 全面对比详解

> 基于 Khronos 官方规范、`ANDROID_extension_pack_es31a`（AEP）扩展文本、GLSL ES 3.20 规范整理。

---

## 一、版本总览

| 维度 | OpenGL ES 3.1 | OpenGL ES 3.2 |
|---|---|---|
| 发布时间 | 2014 年 3 月 | 2015 年 8 月 |
| 着色语言 | GLSL ES 3.10（`#version 310 es`） | GLSL ES 3.20（`#version 320 es`） |
| 核心思路 | 引入计算着色器、SSBO、image 等桌面 GL 4.x 特性 | **吸收 Android 扩展包（AEP）的全部着色器/纹理/混合类扩展**（唯一例外：`EXT_texture_sRGB_decode`），再加 robustness、base-vertex、浮点渲染目标保证 |
| 对应桌面 GL 能力 | ≈ GL 4.0~4.3 的子集（无 tess/geom） | ≈ GL 4.0~4.3 的子集（含 tess/geom，达到 D3D11 世代功能） |
| Android 支持 | Android 5.0 (API 21) 起强制 | Android 6.0 (API 23) 起可选（Adreno 5xx/6xx、Mali G 系、PowerVR 均支持） |
| 头文件 | `GLES3/gl31.h` | `GLES3/gl32.h` |
| 向后兼容 | 兼容 2.0 / 3.0 | 兼容 2.0 / 3.0 / 3.1（3.1 程序无需修改即可运行在 3.2 上） |

**一句话总结**：ES 3.2 ≈ ES 3.1 + AEP（20 个扩展中的 19 个）+ KHR_robustness + OES_draw_elements_base_vertex + EXT_color_buffer_float 的功能保证。因此**几乎每个 3.2 核心新特性在 3.1 上都有对应的官方扩展**。

---

## 二、ES 3.1 基线能力（3.2 全部继承）

作为对比基线，3.1 已有的核心能力（3.2 原样保留，用法不变）：

| 类别 | 内容 |
|---|---|
| 着色器阶段 | Vertex、Fragment、**Compute**（独立于图形管线） |
| 计算相关 | SSBO（`buffer` 块）、image load/store、atomic counter、`memoryBarrier()`/`glMemoryBarrier` |
| 绘制 | indirect draw（`glDrawArraysIndirect` / `glDrawElementsIndirect`）、`glDispatchCompute` |
| 程序 | separate shader objects、program interface query、explicit uniform location |
| 纹理 | 多采样 2D 纹理、gather、stencil texturing、texture view? 否——3.1 核心为 2D 多采样纹理（`sampler2DMS`）、数组数组 |
| GLSL | `#version 310 es`：数组数组、位域操作、layout location、位运算等 |

---

## 三、ES 3.2 新增特性逐项详解

### 3.0 总表：3.2 新增特性 → 3.1 对应扩展 一览

| # | ES 3.2 核心特性 | 3.2 核心 API / 关键 token | 3.1 对应扩展 | 3.1 上的用法差异 | 缺失时回退方案 |
|---|---|---|---|---|---|
| 1 | 曲面细分着色器（TCS/TES） | `GL_PATCHES (0x000E)`、`glPatchParameteri`、`GL_TESS_CONTROL_SHADER (0x8E88)`、`MAX_PATCH_VERTICES≥32`、`MAX_TESS_GEN_LEVEL≥64` | `EXT_tessellation_shader` / `OES_tessellation_shader` | `glPatchParameteriEXT/OES` 需 `eglGetProcAddress`；token 带后缀；GLSL 需 `#extension` | 离线预细分多级 LOD / compute 顶点生成 |
| 2 | 几何着色器（GS） | `GL_GEOMETRY_SHADER (0x8DD9)`、`glFramebufferTexture`、adjacency 图元 `0x000A~0x000D` | `EXT_geometry_shader` / `OES_geometry_shader` | `glFramebufferTextureEXT/OES`；`GL_GEOMETRY_SHADER_EXT/OES` | VS 内 billboard / CPU 预处理 / 6-pass cube map |
| 3 | 接口块 + gpu_shader5 | GLSL 3.20 内置（in/out block、fma） | `EXT_shader_io_blocks`、`EXT_gpu_shader5` / `OES_gpu_shader5` | GLSL 需逐个 `#extension` | 改写为普通 in/out 变量、禁用 fma |
| 4 | ASTC 压缩（LDR） | `GL_COMPRESSED_RGBA_ASTC_4x4_KHR (0x93B0)` 等全套 LDR 格式 | `KHR_texture_compression_astc_ldr` | token 同名；**HDR（_hdr）在 3.2 上仍是扩展** | ETC2/EAC（3.0 核心），资产双格式打包 |
| 5 | 高级混合方程 | `GL_MULTIPLY (0x9293)` 等 15 个 token、`glBlendBarrier` | `KHR_blend_equation_advanced` | token 带 `_KHR`；`glBlendBarrierKHR`；均非相干，需 barrier | 两 pass FBO 离屏手工混合 |
| 6 | 逐 draw-buffer 混合/掩码 | `glEnablei/glDisablei`、`glBlendEquation(Separate)i`、`glBlendFunc(Separate)i`、`glColorMaski` | `EXT_draw_buffers_indexed` / `OES_draw_buffers_indexed` | 函数带 EXT/OES 后缀，`eglGetProcAddress` | 全局混合状态 + shader 内掩码 |
| 7 | 图像区域拷贝 | `glCopyImageSubData` | `EXT_copy_image` / `OES_copy_image` | `glCopyImageSubDataEXT/OES` | FBO + `glBlitFramebuffer` |
| 8 | 调试输出 | `glDebugMessageControl/Insert/Callback`、`glGetDebugMessageLog`、`glPush/PopDebugGroup`、`glObjectLabel` 等 11 个 | `KHR_debug` | 全部带 `KHR` 后缀 | no-op（纯诊断） |
| 9 | 健壮性 | `glGetGraphicsResetStatus`、`glReadnPixels`、`glGetnUniform*`、`GL_CONTEXT_LOST (0x0507)` | `KHR_robustness` | 全部带 `KHR` 后缀；robust access 仍是 EGL context flag | 按 `GL_NO_ERROR` 处理 |
| 10 | 纹理缓冲 | `glTexBuffer` / `glTexBufferRange`、`GL_TEXTURE_BUFFER (0x8C2A)` | `EXT_texture_buffer` / `OES_texture_buffer` | `glTexBufferEXT/OES`；GLSL `samplerBuffer` 需 `#extension` | SSBO（3.1 核心必有） |
| 11 | 立方体贴图数组 | `GL_TEXTURE_CUBE_MAP_ARRAY (0x9009)`、`samplerCubeArray` | `EXT_texture_cube_map_array` / `OES_…` | token 带后缀；GLSL 需 `#extension` | 6 个独立 cube map 采样 |
| 12 | 多采样 2D 纹理数组 | `glTexStorage3DMultisample`、`GL_TEXTURE_2D_MULTISAMPLE_ARRAY (0x9102)` | `OES_texture_storage_multisample_2d_array` | `glTexStorage3DMultisampleOES` | 逐层 resolve 到普通纹理 |
| 13 | STENCIL8 可采样纹理 | `GL_STENCIL_INDEX8 (0x8D48)` | `OES_texture_stencil8` | token 同名 | 采样 depth-stencil 的深度分量 |
| 14 | 边框颜色 | `GL_CLAMP_TO_BORDER`、`GL_TEXTURE_BORDER_COLOR (0x1004)` | `EXT_texture_border_clamp` | token 带 `_EXT` | `CLAMP_TO_EDGE` + shader 内手动处理 |
| 15 | 逐采样着色/插值 | `glMinSampleShading`、`gl_SampleID`、`interpolateAt*` | `OES_sample_shading`、`OES_sample_variables`、`OES_shader_multisample_interpolation` | `glMinSampleShadingOES` | 放弃逐采样，用 centroid |
| 16 | 片元 image 原子操作 | `imageAtomic*`（FS 可用） | `OES_shader_image_atomic` | GLSL `#extension`；且 FS image/SSBO/atomic 最低限可能为 0（AEP 后 ≥4/4/8） | SSBO 原子或 compute 预处理 |
| 17 | 图元包围盒 | `glPrimitiveBoundingBox`（8 float） | `EXT/OES_primitive_bounding_box` | `glPrimitiveBoundingBoxEXT/OES` | no-op（纯优化） |
| 18 | base-vertex 绘制 | `glDrawElementsBaseVertex` 系列 | `OES_draw_elements_base_vertex` | 函数带 `OES` 后缀 | CPU 重建索引加偏移 |
| 19 | 浮点渲染目标保证 | `RGBA16F/32F`、`R11F_G11F_B10F` 保证 color-renderable | `EXT_color_buffer_float` | 无新 API，仅保证项 | 降精度 half float / RGBA8 |
| 20 | sRGB 解码控制 | ❌ **未进 3.2 核心**（AEP 唯一例外） | `EXT_texture_sRGB_decode` | 两代上均为扩展 | 本来就靠扩展 |

> 注：以上 1~19 项在 3.1 上均以扩展形式存在（需运行时探测）；20 项是 AEP 中唯一没被 3.2 核心吸收的扩展。

以下为各特性的展开说明：

### 3.1 曲面细分着色器（Tessellation）

- **功能**：新增 TCS（Tessellation Control Shader / hull）与 TES（Tessellation Evaluation Shader / domain）两个管线阶段，可在 GPU 上动态细分 patch，支持 triangles / quads / isolines 三种域、多种 spacing、可选 point mode。
- **3.2 核心**：
  - 图元：`glDrawArrays(GL_PATCHES, ...)`；`GL_PATCHES = 0x000E`
  - 参数：`glPatchParameteri(GL_PATCH_VERTICES, n)`（另有 `GL_PATCH_DEFAULT_INNER_LEVEL` / `GL_PATCH_DEFAULT_OUTER_LEVEL`，需 `glTexParameterfv?`——实际为 `glPatchParameteri` + 查询 token）
  - 程序阶段：`glCreateShader(GL_TESS_CONTROL_SHADER)`（`0x8E88`）/ `GL_TESS_EVALUATION_SHADER`（`0x8E87`）；`GL_PROGRAM` 位掩码 `GL_TESS_CONTROL_SHADER_BIT (0x8)`、`GL_TESS_EVALUATION_SHADER_BIT (0x10)`
  - 关键限制查询：`GL_MAX_PATCH_VERTICES`（≥32）、`GL_MAX_TESS_GEN_LEVEL`（≥64）
- **3.1 对应扩展**：`EXT_tessellation_shader` 或 `OES_tessellation_shader`（两者功能一致，仅命名后缀不同；驱动可能只暴露其一）。
  - 函数：`glPatchParameteriEXT` / `glPatchParameteriOES`，经 `eglGetProcAddress` 加载
  - Token：`GL_PATCHES_EXT` / `GL_PATCHES_OES`、`GL_TESS_CONTROL_SHADER_EXT/_OES` 等（**数值与核心一致**，仅后缀不同）
  - GLSL：`#version 310 es` + `#extension GL_EXT_tessellation_shader : enable`（或 OES 变体）
- **降级注意**：无 tess 时需预细分网格（离线多级 LOD）或 compute shader 生成顶点。

### 3.2 几何着色器（Geometry Shader）

- **功能**：新增 GS 阶段，输入完整图元（含 4 种新 adjacency 图元），可输出 0~N 个点/线带/三角带，支持多实例（invocations）与 layered rendering（一次绘制写 cube map 六面/多层纹理）。
- **3.2 核心**：
  - 程序阶段：`glCreateShader(GL_GEOMETRY_SHADER)`（`GL_GEOMETRY_SHADER = 0x8DD9`）、`GL_GEOMETRY_SHADER_BIT = 0x4`
  - 新图元：`GL_LINES_ADJACENCY (0x000A)`、`GL_LINE_STRIP_ADJACENCY (0x000B)`、`GL_TRIANGLES_ADJACENCY (0x000C)`、`GL_TRIANGLE_STRIP_ADJACENCY (0x000D)`
  - 分层渲染：`glFramebufferTexture(target, attachment, texture, level)`
  - 链接后查询：`GL_GEOMETRY_LINKED_INPUT_TYPE (0x8917)`、`GL_GEOMETRY_LINKED_OUTPUT_TYPE (0x8918)`、`GL_GEOMETRY_LINKED_VERTICES_OUT (0x8916)`、`GL_GEOMETRY_SHADER_INVOCATIONS (0x887F)`
- **3.1 对应扩展**：`EXT_geometry_shader` / `OES_geometry_shader`
  - 函数：`glFramebufferTextureEXT` / `glFramebufferTextureOES`
  - Token：`GL_GEOMETRY_SHADER_EXT/_OES`（0x8DD9）、`GL_TRIANGLES_ADJACENCY_EXT/_OES` 等
  - GLSL：`#extension GL_EXT_geometry_shader : enable`
- **性能提醒**：移动端 GS 普遍较慢（TBR/TBDR 架构对 layered rendering 不友好），降级时通常直接删功能而不是模拟。
- **降级注意**：GS 的典型用途（billboard 扩展、shadow volume、单 pass 渲染 cube map）在无 GS 时分别回退为：顶点着色器内计算 billboard、CPU 扩展 volume、六 pass 渲 cube map。

### 3.3 配套着色器语言增强（随 tess/geom 进入核心）

| 3.2 核心能力 | 3.1 对应扩展 | 说明 |
|---|---|---|
| 接口块（in/out block）跨阶段传递 | `EXT_shader_io_blocks` | tess/geom 的 gl_in/gl_out 数组依赖它 |
| gpu_shader5 | `EXT_gpu_shader5` / `OES_gpu_shader5` | 更宽松的隐式类型转换、`fma()` 内建等 |
| patch 限定符 | （随 tess 扩展） | GLSL 中 `patch out` 变量，`GL_IS_PER_PATCH (0x92E7)` |
| 图元包围盒（见 3.12） | `EXT/OES_primitive_bounding_box` | TBDR GPU 前置剔除优化 |

### 3.4 ASTC 纹理压缩

- **功能**：Adaptive Scalable Texture Compression，4x4~12x12 块尺寸、LDR profile。
- **3.2 核心**：`GL_COMPRESSED_RGBA_ASTC_4x4_KHR`（`0x93B0` 系列）等一整套 ASTC LDR 格式 token 可直接使用，`glCompressedTexImage2D/3D` 上传。
- **3.1 对应扩展**：`KHR_texture_compression_astc_ldr`（token 完全同名：`GL_COMPRESSED_RGBA_ASTC_4x4_KHR`——这个家族在核心里就保留 KHR 后缀，因为规范直接引用了扩展文本）；HDR profile 对应 `KHR_texture_compression_astc_hdr`。
- **重要差异**：**3.2 核心只保证 LDR profile；HDR 仍要查扩展**。切片 3D（`_sliced_3d`）也是扩展。
- **降级注意**：查不到 ASTC 时回退 ETC2/EAC（ES 3.0 核心必有，质量略差），资产管线需同时打包两套格式。

### 3.5 高级混合方程（Advanced Blending）

- **功能**：`glBlendEquation` 新增 11 种 Porter-Duff / 屏幕类混合模式。
- **3.2 核心**：`GL_MULTIPLY (0x9293)`、`GL_SCREEN (0x9294)`、`GL_OVERLAY (0x9295)`、`GL_DARKEN (0x9296)`、`GL_LIGHTEN (0x9298)`、`GL_COLORDODGE (0x9299)`、`GL_COLORBURN (0x929A)`、`GL_HARDLIGHT (0x929B)`、`GL_SOFTLIGHT (0x929C)`、`GL_DIFFERENCE (0x929E)`、`GL_EXCLUSION (0x92D0)`、`GL_HSL_HUE (0x92AD)`、`GL_HSL_SATURATION (0x92AE)`、`GL_HSL_COLOR (0x92AF)`、`GL_HSL_LUMINOSITY (0x92B0)`；配套 `glBlendBarrier()`。
- **3.1 对应扩展**：`KHR_blend_equation_advanced` → token 全带 `_KHR` 后缀（如 `GL_MULTIPLY_KHR`），barrier 为 `glBlendBarrierKHR`（`eglGetProcAddress`）。
- **共同的行为差异点（两版一致）**：默认**非相干**——同一像素先后两个 draw 之间必须插 `glBlendBarrier`，除非设备同时支持 `KHR_blend_equation_advanced_coherent`。
- **降级注意**：无扩展时这些模式只能用 FBO 两 pass 手工混合。

### 3.6 逐 draw-buffer 混合与颜色掩码

- **功能**：MRT 的每个颜色附件可独立设置 blend func / equation / color mask。
- **3.2 核心函数**：`glEnablei` / `glDisablei`、`glBlendEquationi`、`glBlendEquationSeparatei`、`glBlendFunci`、`glBlendFuncSeparatei`、`glColorMaski`。
- **3.1 对应扩展**：`EXT_draw_buffers_indexed` / `OES_draw_buffers_indexed` → `glBlendFuncSeparateiEXT`、`glColorMaskiOES` 等（EXT/OES 双变体都要试）。
- **降级注意**：无扩展时所有 buffer 共用全局 blend 状态（deferred shading 的 G-buffer 分通道混合需改 shader 做掩码写入）。

### 3.7 图像区域拷贝（Copy Image）

- **功能**：GPU 侧在两个纹理/渲染缓冲对象间直接像素级拷贝，支持兼容格式间的类型转换，无需 FBO。
- **3.2 核心函数**：`glCopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight, srcDepth)`。
- **3.1 对应扩展**：`EXT_copy_image`（`glCopyImageSubDataEXT`）/ `OES_copy_image`（`glCopyImageSubDataOES`）；另有桌面同源的 `KHR_copy_image`。
- **降级注意**：无扩展时回退 FBO + `glBlitFramebuffer`（需要格式兼容 + 附加 framebuffer，开销更大）；小数据量也可用 compute + imageStore 手搓。

### 3.8 调试输出（Debug Output）

- **3.2 核心函数**（无后缀）：`glDebugMessageControl`、`glDebugMessageInsert`、`glDebugMessageCallback`、`glGetDebugMessageLog`、`glPushDebugGroup`、`glPopDebugGroup`、`glObjectLabel`、`glObjectPtrLabel`、`glGetObjectLabel`、`glGetObjectPtrLabel`、`glGetPointerv`。
- **3.1 对应扩展**：`KHR_debug` → **ES 实现上所有入口必须带 `KHR` 后缀**（`glDebugMessageControlKHR`、`glDebugMessageCallbackKHR`、`glObjectLabelKHR`…），经 `eglGetProcAddress` 加载。
- **用途**：驱动性能警告、着色器编译诊断、对象命名（RenderDoc/PerfStudio 可读）。
- **降级注意**：纯诊断功能，缺失时直接 no-op 即可，不影响渲染结果。

### 3.9 健壮性（Robustness）

- **功能**：安全查询（带 bufSize 防越界）+ GPU reset 检测与恢复。
- **3.2 核心函数**：`glGetGraphicsResetStatus()`、`glReadnPixels`、`glGetnUniformfv/iv/uiv`；token `GL_CONTEXT_LOST (0x0507)`、`GL_GUILTY_CONTEXT_RESET (0x8253)`、`GL_INNOCENT_CONTEXT_RESET (0x8254)`、`GL_UNKNOWN_CONTEXT_RESET (0x8255)`。
- **3.1 对应扩展**：`KHR_robustness` → 规范明确要求 **ES 上入口点必须带 `KHR` 后缀**：`glGetGraphicsResetStatusKHR`、`glReadnPixelsKHR`、`glGetnUniformfvKHR` 等；token 如 `GL_CONTEXT_LOST_KHR`。
- **两版共同的启用方式**：robust buffer access 与 reset notification 都是**创建 context 时的 flag**（`EGL_EXT_create_context_robustness`：`EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT`、`EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT`），不是运行时开关。
- **降级注意**：缺失时按 `GL_NO_ERROR` 处理；Android 还有 `EGL_KHR_create_context` / 平台层的 reset 回调可替代。

### 3.10 纹理缓冲（Texture Buffer）

- **功能**：把 buffer 对象当作一维无过滤纹理（每元素采样），适合查表。
- **3.2 核心函数**：`glTexBuffer(target, internalformat, buffer)`、`glTexBufferRange(target, internalformat, buffer, offset, size)`；`GL_TEXTURE_BUFFER = 0x8C2A`、`GL_MAX_TEXTURE_BUFFER_SIZE`（≥65536）。
- **3.1 对应扩展**：`EXT_texture_buffer` / `OES_texture_buffer` → `glTexBufferEXT`、`glTexBufferRangeEXT` 等；GLSL 需 `#extension GL_EXT_texture_buffer : enable`，采样器 `samplerBuffer` / `isamplerBuffer` / `usamplerBuffer`（`GL_SAMPLER_BUFFER` 系列 token 如 `GL_INT_SAMPLER_BUFFER_EXT = 0x8DD0`）。
- **降级注意**：用 SSBO（3.1 核心必有）替代——语义几乎等价，只是采样方式从 texelFetch 变数组访问。

### 3.11 立方体贴图数组 + 多采样 2D 数组 + STENCIL8 纹理

| 特性 | 3.2 核心 | 3.1 对应扩展 | 差异 |
|---|---|---|---|
| 立方体贴图数组 | `GL_TEXTURE_CUBE_MAP_ARRAY = 0x9009`；GLSL `samplerCubeArray`（及 i/u 变体，`GL_INT_SAMPLER_CUBE_MAP_ARRAY = 0x900E`）；image 版 `imageCubeArray (0x9054)` | `EXT_texture_cube_map_array` / `OES_…` | 仅后缀 |
| 多采样 2D 纹理数组 | `glTexStorage3DMultisample(target, samples, ifmt, w, h, depth, fixedSampleLocations)`；`GL_TEXTURE_2D_MULTISAMPLE_ARRAY = 0x9102`；`sampler2DMSArray` | `OES_texture_storage_multisample_2d_array`（`glTexStorage3DMultisampleOES`） | 仅后缀；3.1 核心只有非数组的 2D 多采样 |
| STENCIL8 可采样纹理 | `GL_STENCIL_INDEX8 (0x8D48)` 纹理可直接采样 | `OES_texture_stencil8` | 3.1 核心只能 depth-stencil 附件，采样受限 |
| 边框颜色 | `GL_CLAMP_TO_BORDER` + `GL_TEXTURE_BORDER_COLOR (0x1004)` | `EXT_texture_border_clamp` | 仅后缀 |

### 3.12 图元包围盒（Primitive Bounding Box）

- **功能**：给驱动提示图元经 tess/geom 后的空间范围，让 TBDR（Mali/PowerVR）做前置 tile 剔除。
- **3.2 核心函数**：`glPrimitiveBoundingBox(minX, minY, minZ, minW, maxX, maxY, maxZ, maxW)`（8 个 float）。
- **3.1 对应扩展**：`EXT_primitive_bounding_box` / `OES_primitive_bounding_box` → `glPrimitiveBoundingBoxEXT/_OES`。
- **降级注意**：纯优化 hint，缺失时 no-op 无任何渲染影响。

### 3.13 逐采样处理三件套

| 3.2 核心 | 3.1 对应扩展 | 内容 |
|---|---|---|
| `glMinSampleShading(v)`；`GL_MIN_SAMPLE_SHADING_VALUE (0x8C1E)` | `OES_sample_shading`（`glMinSampleShadingOES`） | 强制每像素多个采样点各自执行 FS |
| GLSL `gl_SampleID`、`gl_SamplePosition`、`gl_SampleMaskIn[]`、`gl_SampleMask[]`、`sample` 插值限定符 | `OES_sample_variables` | 采样相关内建变量 |
| `interpolateAtSample` / `interpolateAtOffset` / `interpolateAtCentroid` | `OES_shader_multisample_interpolation` | 显式插值位置控制 |

### 3.14 片元着色器 image 原子操作

- **3.2 核心**：`imageAtomicAdd/Min/Max/And/Or/Xor/Exchange/CompSwap` 可用于 fragment shader。
- **3.1 对应扩展**：`OES_shader_image_atomic`（GLSL `#extension GL_OES_shader_image_atomic : enable`）。
- **AEP 附带的最低保证**（3.1 无 AEP 时这些最小值可能为 0，3.2 核心强制）：`MAX_FRAGMENT_ATOMIC_COUNTER_BUFFERS ≥ 1`、`MAX_FRAGMENT_ATOMIC_COUNTERS ≥ 8`、`MAX_FRAGMENT_IMAGE_UNIFORMS ≥ 4`、`MAX_FRAGMENT_SHADER_STORAGE_BLOCKS ≥ 4`。

### 3.15 3.2 在 AEP 之外追加的核心特性

| 特性 | 3.2 核心 API | 3.1 对应扩展 | 说明 |
|---|---|---|---|
| base-vertex 绘制 | `glDrawElementsBaseVertex`、`glDrawElementsInstancedBaseVertex`、`glDrawRangeElementsBaseVertex` | `OES_draw_elements_base_vertex`（函数带 `OES` 后缀） | 索引绘制整体加偏移，常用于网格拼接 |
| 浮点渲染目标保证 | `RGBA16F/RG16F/R16F/RGBA32F/...` + `R11F_G11F_B10F` 保证 color-renderable | `EXT_color_buffer_float` | 3.1 核心只保证 half-float（且部分格式需 `EXT_color_buffer_half_float`） |
| sRGB 解码控制 | ❌ **未吸收** | `EXT_texture_sRGB_decode` | **AEP 中唯一没进 3.2 核心的扩展**，降级时此项本来就靠扩展 |

---

## 四、完整函数名对照表（3.2 核心 → 3.1 扩展变体）

| 功能 | 3.2 核心函数（无后缀） | 3.1 扩展函数（需 eglGetProcAddress） |
|---|---|---|
| 曲面细分参数 | `glPatchParameteri` | `glPatchParameteriEXT` / `glPatchParameteriOES` |
| 分层/深度附加 | `glFramebufferTexture` | `glFramebufferTextureEXT` / `glFramebufferTextureOES` |
| 图像拷贝 | `glCopyImageSubData` | `glCopyImageSubDataEXT` / `glCopyImageSubDataOES` |
| 高级混合屏障 | `glBlendBarrier` | `glBlendBarrierKHR` |
| 逐 buffer 混合 | `glEnablei` / `glDisablei` / `glBlendEquationi` / `glBlendEquationSeparatei` / `glBlendFunci` / `glBlendFuncSeparatei` / `glColorMaski` | `glEnableiEXT`… / `glColorMaskiOES`…（EXT、OES 双变体） |
| 调试输出 | `glDebugMessageControl` 等 11 个 | `glDebugMessageControlKHR` 等（全部带 KHR） |
| 健壮性 | `glGetGraphicsResetStatus` / `glReadnPixels` / `glGetnUniformfv/iv/uiv` | `glGetGraphicsResetStatusKHR` / `glReadnPixelsKHR` / `glGetnUniformfvKHR` 等（全部带 KHR） |
| 纹理缓冲 | `glTexBuffer` / `glTexBufferRange` | `glTexBufferEXT` / `glTexBufferRangeEXT`（及 OES 变体） |
| 多采样数组 | `glTexStorage3DMultisample` | `glTexStorage3DMultisampleOES` |
| 逐采样着色 | `glMinSampleShading` | `glMinSampleShadingOES` |
| 包围盒 | `glPrimitiveBoundingBox` | `glPrimitiveBoundingBoxEXT` / `glPrimitiveBoundingBoxOES` |
| base-vertex | `glDrawElementsBaseVertex` 系列 | `glDrawElementsBaseVertexOES` 系列 |
| 几何着色器创建 | `glCreateShader(GL_GEOMETRY_SHADER)` | `glCreateShader(GL_GEOMETRY_SHADER_EXT/_OES)`（函数同名，仅参数 token 不同） |

> 注：`GL_COMPRESSED_*_ASTC_*` 是例外——token 在 3.2 核心里保留了 KHR 字样。

---

## 五、主要枚举 Token 对照表

| 3.2 核心 token（值） | 3.1 扩展变体 |
|---|---|
| `GL_PATCHES (0x000E)` | `GL_PATCHES_EXT` / `GL_PATCHES_OES` |
| `GL_TESS_CONTROL_SHADER (0x8E88)` / `GL_TESS_EVALUATION_SHADER (0x8E87)` | `_EXT` / `_OES` 后缀变体 |
| `GL_TESS_CONTROL_SHADER_BIT (0x8)` / `GL_TESS_EVALUATION_SHADER_BIT (0x10)` | 同上 |
| `GL_GEOMETRY_SHADER (0x8DD9)` / `GL_GEOMETRY_SHADER_BIT (0x4)` | `GL_GEOMETRY_SHADER_EXT` / `_OES` |
| `GL_LINES_ADJACENCY (0x000A)` / `GL_LINE_STRIP_ADJACENCY (0x000B)` / `GL_TRIANGLES_ADJACENCY (0x000C)` / `GL_TRIANGLE_STRIP_ADJACENCY (0x000D)` | `_EXT` / `_OES` 变体 |
| `GL_ISOLINES (0x8E7A)` | `GL_ISOLINES_EXT` / `_OES` |
| `GL_TEXTURE_BUFFER (0x8C2A)` / `GL_MAX_TEXTURE_BUFFER_SIZE (0x8C2B)` | `_EXT` / `_OES` |
| `GL_TEXTURE_CUBE_MAP_ARRAY (0x9009)` | `_EXT` / `_OES` |
| `GL_TEXTURE_2D_MULTISAMPLE_ARRAY (0x9102)` | `_OES` |
| `GL_MIN_SAMPLE_SHADING_VALUE (0x8C1E)` | `_OES` |
| `GL_MULTIPLY (0x9293)` … `GL_HSL_LUMINOSITY (0x92B0)` 等 15 个 | 全部带 `_KHR` |
| `GL_CONTEXT_LOST (0x0507)` / `GL_GUILTY_CONTEXT_RESET (0x8253)` 等 | 全部带 `_KHR` |
| `GL_DEBUG_OUTPUT` / `GL_DEBUG_OUTPUT_SYNCHRONOUS` / `GL_MAX_DEBUG_*` 等 | 全部带 `_KHR` |
| `GL_TEXTURE_BORDER_COLOR (0x1004)` | `GL_TEXTURE_BORDER_COLOR_EXT` |
| `GL_FRAMEBUFFER_ATTACHMENT_LAYERED (0x8DA7)` | `_EXT` / `_OES` |
| `GL_PRIMITIVE_BOUNDING_BOX` | `_EXT` / `_OES` |

规律：**同族 EXT/OES 变体的数值与核心一致，仅命名后缀不同**；KHR 类扩展在 ES 上入口点和 token 全部强制 KHR 后缀，核心化后去掉。

---

## 六、GLSL ES 3.10 vs 3.20 对照

| 维度 | GLSL ES 3.10 | GLSL ES 3.20 |
|---|---|---|
| 版本行 | `#version 310 es` | `#version 320 es` |
| 新阶段 | 无 | `GL_TESS_CONTROL_SHADER`、`GL_TESS_EVALUATION_SHADER`、`GL_GEOMETRY_SHADER` |
| tess 相关 | 需 `#extension GL_EXT/OES_tessellation_shader` | 内置：`gl_PatchVerticesIn`、`gl_TessLevelInner[2]`、`gl_TessLevelOuter[3]`、`gl_TessCoord`、`patch` 限定符、`gl_in[]/gl_out[]`；layout：`triangles/quads/isolines`、`equal_spacing/fractional_odd/even_spacing`、`cw/ccw`、`point_mode`、`vertices=N` |
| geom 相关 | 需 `#extension GL_EXT/OES_geometry_shader` | 内置：`EmitVertex()`、`EndPrimitive()`、`gl_InvocationID`、`gl_PrimitiveID`、`gl_Layer`、`gl_PrimitiveIDIn`；layout：`points/lines/triangles`、`max_vertices=N`、`invocations=N` |
| 接口块 | 需 `EXT_shader_io_blocks` | 核心化 |
| gpu_shader5 | 需 `EXT_gpu_shader5` | 核心化（fma、更宽的隐式转换等） |
| 新采样/图像类型 | 需各自扩展 | `samplerBuffer/isamplerBuffer/usamplerBuffer`、`samplerCubeArray(+i/u)`、`sampler2DMSArray(+i/u)`、`imageBuffer`、`imageCubeArray`、`image2DMSArray` |
| 逐采样 | 需 `OES_sample_variables`、`OES_shader_multisample_interpolation` | 内置：`gl_SampleID`、`gl_SamplePosition`、`gl_SampleMaskIn[]/gl_SampleMask[]`、`sample` 限定符、`interpolateAtSample/Offset/Centroid` |
| image 原子 | 需 `OES_shader_image_atomic` | 内置 |
| 一次开全家 | `#extension GL_ANDROID_extension_pack_es31a : enable`（同时启用 12 个 EXT 着色器扩展） | 不需要 |

**着色器源码移植规则**：几何/细分着色器的 body 通常一行不改，只改两处——版本行、extension 行。

---

## 七、3.2 新保证的最低实现限制（3.1 上可能更低或不保证）

| 限制 | 3.2 最低值 | 3.1 情况 |
|---|---|---|
| `GL_MAX_PATCH_VERTICES` | 32 | 无（需扩展） |
| `GL_MAX_TESS_GEN_LEVEL` | 64 | 无（需扩展） |
| `GL_MAX_GEOMETRY_OUTPUT_VERTICES` | 256 | 无（需扩展） |
| `GL_MAX_TEXTURE_BUFFER_SIZE` | 65536 | 无（需扩展） |
| `GL_MAX_FRAGMENT_IMAGE_UNIFORMS` | 4 | 核心最低 0；AEP 后 ≥4 |
| `GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS` | 4 | 核心最低 0；AEP 后 ≥4 |
| `GL_MAX_FRAGMENT_ATOMIC_COUNTERS` | 8 | 核心最低 0；AEP 后 ≥8 |
| 16F/32F 颜色可渲染 | 保证 | 需 `EXT_color_buffer_float` |

---

## 八、行为差异与常见的坑

1. **ASTC HDR**：3.2 核心只有 LDR；两代上用 HDR profile 都要查 `KHR_texture_compression_astc_hdr`。
2. **高级混合非相干**：3.2 核心也不保证 coherent，`glBlendBarrier` 的语义与扩展完全相同；查 `_coherent` 扩展免 barrier。
3. **robustness 是 context 属性**：函数虽进了 3.2 核心，但 robust buffer access 仍需建 context 时传 EGL flag。
4. **EXT vs OES 双变体**：同一功能 3.1 上可能叫 `EXT_xxx` 也可能叫 `OES_xxx`（如 `EXT_draw_buffers_indexed`/`OES_draw_buffers_indexed`），探测和取函数指针时两个名字都要试。
5. **别名陷阱**：Android 上很多驱动的后缀版入口与核心入口同地址，直接链接核心名"碰巧能用"，但规范上 3.1 必须走 `eglGetProcAddress`，跨厂商代码不要依赖别名。
6. **AEP ≠ 全有**：3.1 + AEP 只代表那 20 个扩展；`KHR_robustness`、`OES_draw_elements_base_vertex`、`EXT_color_buffer_float` 不在 AEP 内，3.1 设备上要单独查。
7. **GS/tess 性能**：即便 3.1 设备有对应扩展，移动 GPU 上 GS 尤其慢；降级决策先看性能而不是只看功能存在性。

---

## 九、降级适配 Checklist（配合前一轮的代码框架）

1. 运行时探测：`glGetString(GL_VERSION)` 判 "ES 3.2" → 3.1 时用 `glGetStringi(GL_EXTENSIONS, i)` 枚举扩展（3.0+ 方式）。
2. 入口点：3.1 全部经 `eglGetProcAddress`，EXT/OES/KHR 名字逐一尝试；3.2 直接链接 `gl32.h` 符号。
3. Token：能力探测结果驱动宏映射（`#define GL_GEOMETRY_SHADER_T OK` 式封装）。
4. 着色器：按版本拼接 `#version` 行与 `#extension` 行；3.1 上可用 `GL_ANDROID_extension_pack_es31a` 一次开全家。
5. 功能回退表：

| 3.2 特性 | 扩展缺失时的回退方案 |
|---|---|
| 曲面细分 | 离线预细分多级 LOD / compute 顶点生成 |
| 几何着色器 | VS 内做 billboard；CPU 预处理；cube map 分 6 pass |
| `glCopyImageSubData` | FBO + `glBlitFramebuffer`；或 compute + imageStore |
| ASTC | ETC2/EAC（3.0 核心），资产双格式打包 |
| 逐 buffer 混合 | 全局混合状态 + shader 内掩码 |
| 高级混合方程 | 两 pass FBO 离屏手工混合 |
| 纹理缓冲 | SSBO（3.1 核心必有） |
| base-vertex | 重建索引缓冲时加偏移（CPU 侧） |
| 浮点渲染目标 | 降精度到 half float / RGBA16F（若支持）或 RGBA8 |
| debug / robustness / 包围盒 | no-op，无功能影响 |

---

*参考来源：Khronos OpenGL ES 3.2 Specification "New Features"、GL_ANDROID_extension_pack_es31a 扩展文本、GLSL ES 3.20 Specification、KHR_robustness / KHR_debug 扩展规范、Khronos SIGGRAPH 2015 3D BOF 幻灯片。*
