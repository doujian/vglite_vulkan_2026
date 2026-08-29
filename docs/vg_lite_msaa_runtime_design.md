# 运行时 MSAA 2x/4x 切换 — 设计与实现文档

> 对应提交：`3c3ec55`（feat msaa）+ `1a56015`（tiger 修复，顺带产出）
> 代码基线：43 PASS / FAIL test_gfx3, test_imgIndex / CRASH test_sft_blit（均为既有问题）

---

## 0. 背景与动机

本项目原有的抗锯齿框架将 **4x MSAA 硬编码**在三个层面：

| 层面 | 位置 | 硬编码点数 |
|---|---|---|
| Render Pass 附件描述 | `vg_lite_vulkan.c` create_render_pass / create_render_pass_clear | 4 |
| 附件图像创建 | set_render_target_ex 的 create_attachment 调用 | 2（color + depth；resolve 恒 1x） |
| 管线 multisample state | `vg_lite_vulkan.c` ×6 + `vg_lite_draw.c`（pattern）×2 | 8 |

这三层必须**一致**，否则 Vulkan 校验层直接报错（RP 附件的 `samples` 与管线 `rasterizationSamples` 不匹配是非法的）。

本轮目标：把 `4` 参数化为全局 `g_msaa_samples`，支持 **2x / 4x 运行时动态切换**，默认仍为 4x。典型收益：性能敏感场景降 2x 省带宽/功耗，质量敏感场景升 4x，无需重编译。

```
为什么要"切换"而不是"配置期二选一"？
  ├─ MSAA 附件（color/depth/resolve）按样本数创建，尺寸不同，不可复用
  ├─ Render Pass 创建时把 samples 烧进附件描述，不可改
  └─ 管线的 rasterizationSamples 是不可变状态
  ⇒ 换样本数 = 这三类对象全部重建
```

---

## 1. 修改点总览

| 文件 | 修改内容 |
|---|---|
| `src/vg_lite_vulkan.h` | `extern g_msaa_samples`、`vg_lite_vulkan_set_msaa_samples()`、`register/unregister_buffer()` 声明 |
| `src/vg_lite_vulkan.c` | 全局变量、切换函数（本档 §3）、512 槽 buffer 注册表、`destroy_buffer_msaa_objects()`、全部硬编码替换（本档 §4） |
| `src/vg_lite.c` | init 读 `VGLITE_MSAA_SAMPLES` 环境变量；公共 API `vg_lite_set_msaa_samples()`；allocate/free 维护注册表 |
| `src/vg_lite_draw.c` | pattern stencil/cover 两个管线的 `ms.rasterizationSamples = g_msaa_samples` |
| `inc/vg_lite.h` | 公共 API 声明 |
| `tests/msaaSwitch/` | 切换冒烟测试（本档 §6） |
| `README.md` | 新增 "Runtime MSAA Sample Count" 章节、测试表、汇总行 38→43 PASS |

---

## 2. 用户视角：三个入口

```c
/* ① 进程级（不改代码）：环境变量，vg_lite_init 时生效 */
// set VGLITE_MSAA_SAMPLES=2

/* ② 运行时公共 API（本轮新增） */
vg_lite_set_msaa_samples(2);            // 返回值见下表

/* ③ 内部 API（模块间用，公共 API 的直通） */
vg_lite_vulkan_set_msaa_samples(2);
```

`vg_lite_set_msaa_samples()` 返回值语义：

| 返回值 | 条件 |
|---|---|
| `VG_LITE_SUCCESS` | 切换成功（含"已是该值"的幂等情形） |
| `VG_LITE_INVALID_ARGUMENT` | 传入非 2/4 |
| `VG_LITE_NO_CONTEXT` | 尚未 `vg_lite_init` |
| `VG_LITE_NOT_SUPPORT` | 设备能力不足被钳制（见 §3 步骤 0） |

---

## 3. 核心设计：切换函数的状态机

### 3.1 总体流程

```
vg_lite_vulkan_set_msaa_samples(n)
        │
        ▼
┌─ 步骤 0：三道闸门 ─────────────────────────────┐
│  ① 无 device → 只记录值（pre-init），return    │
│  ② n ∉ {2,4} → 警告并 return                  │
│  ③ 设备上限钳制：                              │
│     supported = framebufferColorSampleCounts   │
│              & framebufferDepthSampleCounts    │
│              & framebufferStencilSampleCounts  │
│     （注意：Vulkan 没有 framebufferDepthStencil─│
│      SampleCounts 这个字段，depth/stencil 分开查）│
│  ④ 已是该值 → 幂等 return                     │
└───────────────────────────────────────────────┘
        │
        ▼
┌─ 步骤 1：排空在途工作 ─────────────────────────┐
│  flush_render_pass()   ← 结束 RP，触发 resolve │
│  submit_command(1)     ← 提交并等 fence        │
│  （保证下面摧毁的对象没有命令还在引用）          │
└───────────────────────────────────────────────┘
        │
        ▼
┌─ 步骤 2：解除当前 framebuffer 绑定 ────────────┐
│  current_fb / current_fb_image /               │
│  current_msaa_color_image / current_resolve_image│
│  current_fb_view / current_fb_internal 全部置空 │
└───────────────────────────────────────────────┘
        │
        ▼
┌─ 步骤 3：遍历 buffer 注册表 ───────────────────┐
│  for each registered internal:                 │
│    destroy_buffer_msaa_objects(internal)       │
│      ├─ 4x/2x color  附件 (view+image+memory)  │
│      ├─ 4x/2x depth  附件 (同上)               │
│      ├─ 1x resolve   附件 (同上)               │
│      └─ render_pass + clear_render_pass        │
│    internal->msaa_needs_seed = 1   ← 下次绘制重新播种│
│    internal->msaa_dirty = 0                    │
└───────────────────────────────────────────────┘
        │
        ▼
┌─ 步骤 4：摧毁全部管线缓存 ─────────────────────┐
│  vg_lite_vulkan_destroy_pipelines()            │
│  （管线把 rasterizationSamples 烧进不可变状态， │
│   必须重建；所有创建路径是 NULL 检查 → 懒重建）  │
└───────────────────────────────────────────────┘
        │
        ▼
   g_msaa_samples = 新值
   "[msaa] switched to 2x MSAA"
```

### 3.2 关键不变量

1. **目标图像本体不动**。切换摧毁的只是旁路 MSAA 附件；目标像素内容在步骤 1 的 resolve 中已安全落盘。
2. **decide-once + 懒重建**：切换成本一次付清（全量摧毁），之后 set_render_target 懒建附件、管线缓存懒建，**热路径零额外开销**——绘制代码读的只是一个全局变量。
3. **种子机制兜底正确性**：`msaa_needs_seed=1` 保证新附件首次使用前会从目标内容重新播种（seed_msaa 全屏三角形），切换前的画面不会丢。
4. **fence 隔离**：`submit_command(1)` 内部等待 fence，摧毁对象时 GPU 已无引用，无 use-after-free。

### 3.3 buffer 注册表

```c
static buffer_internal_t *s_buffer_registry[512];   /* swap-remove */
```

- `vg_lite_allocate` 成功后注册 internal；`vg_lite_free` 注销（swap-remove，O(n) 查找 + O(1) 删除）
- 只在切换瞬间遍历，512 上限远超常规场景；`vg_lite_close` 时全部 buffer 已释放，无需额外清理

### 3.4 一个容易踩的坑（实现时实际遇到）

设备能力查询用 `framebufferDepthSampleCounts` **和** `framebufferStencilSampleCounts` 分别按位与——Vulkan 规范里**不存在** `framebufferDepthStencilSampleCounts`（D24S8 附件要同时满足 depth 和 stencil 两个独立位域）。

---

## 4. 硬编码替换清单（验证过 grep）

替换后全仓 `VK_SAMPLE_COUNT_4_BIT` 仅剩：

- `vg_lite_vulkan.c` 4 处 —— 全局变量自身的定义（默认值）与 pre-init 赋值，**这正是它该在的地方**
- third_party Vulkan 头文件 —— 无关

业务代码全部改为读 `g_msaa_samples`：

```c
/* render pass 附件（4 处） */
attachments[0].samples = g_msaa_samples;            /* 4x/2x color */
attachments[2].samples = g_msaa_samples;            /* 4x/2x depth */
/* attachments[1] resolve 恒为 VK_SAMPLE_COUNT_1_BIT，不改 */

/* 附件创建（set_render_target_ex） */
create_attachment(w, h, vkfmt, g_msaa_samples)      /* color  */
create_attachment(w, h, D24_UNORM_S8_UINT, g_msaa_samples)  /* depth */

/* 管线 multisample state（8 处） */
ms.rasterizationSamples = g_msaa_samples;                       /* 普通 */
ms.rasterizationSamples = (mode==1) ? VK_SAMPLE_COUNT_1_BIT     /* no-msaa 模式 */
                                    : g_msaa_samples;           /* 保持不变 */
```

Shader 无任何样本数假设（grep 验证），不需要重编译 SPIR-V。

---

## 5. 切换后的绘制路径（以 2x 为例）

```
vg_lite_draw / vg_lite_blit
        │
        ▼
set_render_target 发现 msaa_color == NULL ── 懒重建 ──┐
│   msaa_color = create_attachment(fmt,     2x)      │
│   msaa_depth = create_attachment(D24S8,   2x)      │
│   resolve    = create_attachment(fmt,     1x)      │
│   render_pass = create_render_pass(2x,1x,2x)       │
└────────────────────────────────────────────────────┘
        │  msaa_needs_seed=1
        ▼
seed_msaa：全屏三角形把目标内容种进 2x 附件
        │
        ▼
管线缓存为 NULL ── 懒重建（rasterizationSamples = 2x）
        │
        ▼
┌─────────────────────────────────────────┐
│  光栅化 @ 2x 附件（边缘 2 样本覆盖）      │
│  ├─ 普通 blit/draw                       │
│  └─ pattern：stencil pass + cover pass   │
│     （两者都在 2x 附件上光栅化）           │
└─────────────────────────────────────────┘
        │  render pass 结束
        ▼
硬件自动 resolve（pResolveAttachments）→ 1x 附件
        │  本机驱动规避：不直接 resolve 到 LINEAR
        ▼
vkCmdCopyImage：1x resolve → LINEAR 目标
```

2x 与 4x 的差异只在样本数本身：2x 边缘 AA 略弱，带宽/着色开销约减半。路径结构完全相同。

---

## 6. 测试用例：tests/msaaSwitch

### 6.1 用例设计

```
init 320×480 → alloc BGRA8888 target (LINEAR)
→ alloc 64×64 BGRA8888 src，直接写映射内存填 0xFF44CC88

 ① 默认 4x：clear(0xFF3366AA) + blit + finish → content_present()
 ② 切 2x：vg_lite_set_msaa_samples(2) 期望 SUCCESS → 再画再验
 ③ 切回 4x：期望 SUCCESS → 再画
 ④ 非法值：set_msaa_samples(3) 期望 INVALID_ARGUMENT

content_present()：在 64×64 区域按 8 像素步长采样，
若仍有像素 == clear 色 0xFF3366AA 则失败
（验证内容真的落进了目标，而不是只有 clear）
```

### 6.2 覆盖的故障模式

| 步骤 | 若实现有 bug，会在这里暴露 |
|---|---|
| ② 首次切换后绘制 | 注册表漏 buffer → 摧毁遗漏/悬空附件崩溃；懒重建用错样本数 → 校验层报错 |
| ② 内容验证 | resolve/copy 回目标路径断裂 → 画面丢失 |
| ③ 二次切换 | 重建后的对象再次被正确摧毁（幂等性） |
| ④ 非法值 | 参数校验缺失 |

### 6.3 运行与结果

```
> test_msaaSwitch.exe
[vglite] scalarBlockLayout: supported (Vulkan 1.2 core)
[msaa] switched to 2x MSAA
[msaa] switched to 4x MSAA
msaaSwitch test PASSED
```

PNG 输出按编译配置路由：`build/tests/dump_lin_msaa_obb/msaaSwitch_output.png`（Config 1）。

### 6.4 回归矩阵

| 验证项 | 结果 |
|---|---|
| 默认 4x 全套件 | 43 PASS / FAIL gfx3+imgIndex / CRASH sft_blit（与基线一致，零回归） |
| `VGLITE_MSAA_SAMPLES=2` 环境变量 | patternFill 等通过，打印 switched to 2x |
| 运行时 API 切换 | msaaSwitch PASSED |
| test_uploadTiled / test_uploadBuffer | 4/4、3/3（确认 MSAA 改动不干扰 upload 路径） |

---

## 7. 顺带产出：tiger 金图修复（FIXES #34）

全量测试时 test_tiger 新增崩溃，排查发现**与 MSAA 无关**：

- `vg_lite_save_png` 早已把输出路由到 dump 子目录，但 tiger.c 仍按工作目录路径比对金图
- 此前"通过"依赖 build/tests 里一张**过期的旧 PNG**（早期清理时被删），属于环境性假通过
- 修复：新增公共 helper `const char *vg_lite_dump_subdir()`（util），tiger.c 用它拼出 `dump_*/tiger_output.png` 比对
- 结果：tiger 真实通过（3.90% 像素差 < 5.5% 容差），套件 42→43 PASS

---

## 8. 边界与已知限制

- **仅 2/4**：1x 走 `VGLITE_BLIT_MSAA=OFF` 的既有 no-msaa 路径（1x 管线 + 无 resolve RP），8x 未开启（附件内存 ×8，本机设备也未必支持）
- **切换有一次性开销**：全量管线重建 + 附件重播种，适合帧间/场景级切换，不适合逐 draw 切换
- **注册表 512 上限**：超过会漏注册（当前场景够用，扩展时可改动态数组）
- **线程模型**：与引擎其余部分一致，非线程安全

---

## 9. 文件清单

```
src/vg_lite_vulkan.h    +12   声明（g_msaa_samples / set / register / unregister）
src/vg_lite_vulkan.c    +190  切换函数、注册表、destroy_buffer_msaa_objects、15 处硬编码替换
src/vg_lite.c           +40   env 读取、公共 API、注册表维护
src/vg_lite_draw.c       ±2   pattern 两管线
inc/vg_lite.h            +8   公共 API 声明
tests/msaaSwitch/      +101   冒烟测试（新）
tests/CMakeLists.txt     +2   注册
README.md                +30  文档章节 + 测试表
FIXES.md                 +12  #34 tiger 修复（随 1a56015）
```
