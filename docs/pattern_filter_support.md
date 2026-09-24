# vg_lite_draw_pattern 的 filter 支持

> 对应提交:`feat(pattern): honor vg_lite_draw_pattern filter parameter`
> 日期:2026-09-24 · 验证环境:Mesa lavapipe 26.2.0(MSRTSS enabled)

## 1. 背景与问题

`vg_lite_draw_pattern` 的 API 签名带 `vg_lite_filter_t filter` 参数,但实现中原样丢弃:

```c
(void)filter;    /* 原 vg_lite_draw.c draw_pattern 内 */
```

纹理采样固定使用专用 `s_pattern_sampler`(NEAREST)。也就是说无论调用方传
POINT / LINEAR / BI_LINEAR / GAUSSIAN,GPU 都按最近邻采样,filter 能力缺失。

而 blit / draw_image 路径早已有按 filter 选 sampler 的缓存函数
`get_or_create_sampler`(vg_lite.c:113),pattern 是唯一没接上的纹理消费方。

## 2. filter 枚举与 sampler 映射

`vg_lite_filter_t`(vg_lite.h:529-533):

| 枚举 | 值 | 映射 VkFilter | 说明 |
|---|---|---|---|
| `VG_LITE_FILTER_POINT` | 0x0000 | NEAREST | 最近邻,块状 texel |
| `VG_LITE_FILTER_LINEAR` | 0x1000 | LINEAR | 双线性 |
| `VG_LITE_FILTER_BI_LINEAR` | 0x2000 | LINEAR | 双线性(同上) |
| `VG_LITE_FILTER_GAUSSIAN` | 0x3000 | LINEAR | 折叠进 LINEAR(与 blit 路径同约定) |

映射实现在 `get_or_create_sampler`(vg_lite.c:113 起):按 filter 缓存两个
`VkSampler`(NEAREST / LINEAR),blit、draw_image、pattern 三路共用。

## 3. 修改点(全部在 src/vg_lite_draw.c,7 处)

| # | 位置 | 修改 |
|---|---|---|
| 1 | draw_pattern | 删 `(void)filter;` |
| 2 | :927(原 :951) | `VkSampler sampler = get_or_create_sampler(filter);` —— filter 首次真实生效 |
| 3 | :1222(原 :1245) | radial LUT sampler 钉 `VG_LITE_FILTER_POINT` + 注释 |
| 4 | :1514(原 :1536) | grad_internal LUT sampler 钉 `VG_LITE_FILTER_POINT` + 注释 |
| 5 | — | 删 `s_pattern_sampler` 静态声明 |
| 6 | — | 删 cleanup 释放块 + 整个 `get_or_create_pattern_sampler` 函数(净 -32 行) |
| 7 | vg_lite_draw_grad 委托 | 硬编码 `FILTER_LINEAR` → `VG_LITE_FILTER_POINT` + 注释 |

## 4. 设计决策

### 4.1 为什么 LUT(radial / grad)钉 POINT

radial 渐变的 256 级(或 128×N)查找表由 CPU `update_radial_grad` **预量化**生成,
每个 u 坐标已对应唯一颜色。CPU 参考实现(测试校验基准)就是查表取整——
若 LUT 用 LINEAR 采样,GPU 会在两级之间插值,打破与 CPU 参考的**字节级**一致
(linearGrad 测试的 153600/153600 PERFECT MATCH 依赖这一点)。

原 `get_or_create_pattern_sampler` 恒返回 NEAREST,行为等价于钉 POINT;
显式写 `VG_LITE_FILTER_POINT` 是把隐式行为变成显式契约。

### 4.2 为什么 draw_grad 委托改 LINEAR→POINT

`vg_lite_draw_grad`(线性渐变入口)100% 委托 `vg_lite_draw_pattern`,用
256×1 ramp 纹理当 pattern。它原本硬编码 `FILTER_LINEAR`,但 ramp 从未被
LINEAR 采样过——委托目标(pattern)原实现是 NEAREST。filter 接通后若不改,
linearGrad 的 LUT 采样会真的变成 LINEAR,打破既有字节级基线。钉 POINT 保持
数学等价:ramp 已按 stop 量化,POINT 采样 = CPU 参考的直接对应。

### 4.3 为什么不需要改 shader

Vulkan 的纹理过滤是**固定功能采样硬件**,由 `VkSampler` 的
`magFilter/minFilter` 决定,shader 无感知:

```
vg_lite_draw_pattern(filter)                     ← API 参数
  → get_or_create_sampler(filter)                ← POINT→NEAREST,其余→LINEAR
  → 写入 COMBINED_IMAGE_SAMPLER descriptor       ← vg_lite_draw.c:935
  → pattern.frag: texture(uPattern, uv)          ← SPIR-V 不变
  → 硬件按 sampler 状态做 nearest / 双线性
```

这也是 blit / draw_image 早已支持 filter 却从未动 shader 的原因,与官方
gpu-vglite 一致:filter 是采样器参数,不是着色行为。

唯一需要 shader 配合的情形:实现"真" GAUSSIAN(3×3 多 tap 卷积)——当前
按仓库既有约定折叠进 LINEAR。

## 5. 验证(lavapipe,MSRTSS enabled)

### 5.1 回归:POINT 路径零退化

5 个纹理相关测试全部 EXIT=0,19 个输出 PNG 与改动前基线 **MD5 逐字节一致**:

| 测试 | 结果 |
|---|---|
| test_patternFill(POINT) | exit 0,pattern_0/1.png 哈希不变 |
| test_linearGrad | 153600/153600 PERFECT MATCH |
| test_radialGrad | exit 0,既有 ARGB8888 通道旋转 bug 维持 57600 mismatch(无新退化,见 FIXES 遗留) |
| test_stroke | exit 0,13 PNG 哈希不变 |
| test_blit_mixed(POINT) | exit 0,all pixels match |

### 5.2 生效性:BI_LINEAR A/B 实验

测试源码单变量切换 `filter = POINT → BI_LINEAR` 后:

- pattern_0.png MD5 `FB687BD1…→D88620F0…`,pattern_1.png `D7BB017D…→3A14C6D5…`(字节改变)
- 逐像素差异指纹符合双线性数学签名:
  - 差异**只出现在纹理采样区**:pattern_0 36637/153600(23.9%),pattern_1 26110/153600(17.0%,第二帧含 COLOR 纯色区不采样纹理故更少)
  - 背景、纯色区零差异
  - avg delta ≈ 17/765,max 217 集中于 texel 颜色边界(landscape 天空/地形交界)——NEAREST 边界两侧各取硬值,LINEAR 变加权混合
- 还原 POINT 后重跑,哈希恢复基线 —— 可逆闭环

## 6. 边界与遗留

- GAUSSIAN 折叠进 LINEAR(与 blit 同约定);真高斯需 shader 多 tap 实现
- radialGrad 的 57600 mismatch 是**目标格式 ARGB8888 通道旋转**既有 bug
  (GPU 原生 [R,G,B,A] vs CPU 约定 [A,R,G,B]),与本轮 filter 改动无关,
  修复方案另案备档
