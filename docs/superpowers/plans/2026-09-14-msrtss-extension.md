# VK_EXT_multisampled_render_to_single_sampled Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render MSAA (2x/4x) passes directly against single-sampled attachments via `VK_EXT_multisampled_render_to_single_sampled` (MSRTSS), eliminating the per-buffer MSAA color sidecar image and the fullscreen-tri seed draw, with the existing seed/resolve path retained as automatic fallback on unsupported devices.

**Architecture:** All render passes are created through exactly two factories (`vg_lite_vulkan_create_render_pass` / `_clear` in `src/vg_lite_vulkan.c:498,587`) and all 12 graphics pipelines are created against fresh RPs from those factories — so the MSRTSS variant is added *inside* the factories via `vkCreateRenderPass2` + `VkMultisampledRenderToSingleSampledInfoEXT` chained on `VkSubpassDescription2`. In MSRTSS mode the RP has 2 attachments (1x color = existing `resolve_image`, 1x depth) instead of 3 (4x color + 1x resolve + 4x depth); the deferred `resolve_msaa_to_target()` copy (resolve_image → LINEAR target) is **kept unchanged** because the LINEAR-target "smear" workaround still requires it. `seed_msaa()` becomes a `vkCmdCopyImage` (target → resolve_image) instead of a fullscreen-tri draw, and is only needed after non-MSRTSS-visible writes (no-MSAA RP writes, CPU writes, sample-count switches).

**Tech Stack:** C11, Vulkan 1.2 (`vkCreateRenderPass2` is core 1.2 — no extra extension needed for it), volk (already loads everything), CMake.

**Runtime switch:** env var `VGLITE_MSRTSS` — `1` = force on, `0` = force off, unset = auto (use when device supports it). Logged at init. This enables A/B comparison in the 8-config test matrix without new compile-time axes.

**Hard constraints (from AGENTS.md):**
- All 8 build configs must reach **37/38 PASS** (only `test_sft_blit` pre-existing crash allowed).
- No regression allowed in MSAA configs (1, 2, 5, 6) — these exercise the new path on supporting GPUs; configs 3/4/7/8 (VGLITE_BLIT_MSAA=OFF) must be untouched by the change.
- Every bug fix goes into `FIXES.md`; `README.md` updated if behavior/logs change.
- Build commands: `cmake -B <dir> -DVGLITE_TARGET_OPTIMAL=.. -DVGLITE_BLIT_MSAA=.. -DVGLITE_BLIT_OBB=..` then `cmake --build <dir>`, tests run from `<dir>/tests/Debug/`.

---

## Background: current MSAA data flow (what MSRTSS replaces)

```
target(1x, often LINEAR)
   │  seed_msaa() draw  (fullscreen tri, blit target content into 4x image)   ← REMOVED by MSRTSS
   ▼
msaa_color_image(4x) ──render──▶ resolve_image(1x OPTIMAL) ──vkCmdCopyImage──▶ target
   ▲ sidecar allocations (vg_lite_vulkan.c:834-866)                          ← color sidecar REMOVED;
                                                                             copy KEPT (smear workaround)
```

MSRTSS flow (new):
```
resolve_image(1x OPTIMAL, used DIRECTLY as the color attachment with
              VkMultisampledRenderToSingleSampledInfoEXT{rasterizationSamples=4})
   ──render at 4x, HW loads 1x content & resolves on store──▶ vkCmdCopyImage ──▶ target
seed (=target→resolve_image copy) only needed after no-MSAA-RP/CPU writes to target
```

Key invariant that makes this semantically equivalent: today `seed_msaa` blits *resolved* 1x content into the 4x image (samples are replicated, sub-sample data is already lost). MSRTSS hardware load does the same replication. Multi-pass accumulation semantics (deps at vg_lite_vulkan.c:542-560) are preserved.

---

### Task 1: Context fields + runtime toggle plumbing

**Files:**
- Modify: `vglite_vulkan_2026/src/vg_lite_vulkan.h` (vk_context_t, ~line 220 before the closing brace)
- Modify: `vglite_vulkan_2026/src/vg_lite_vulkan.c` (init, after `volkLoadDevice` ~line 336)

- [ ] **Step 1.1: Add fields to `vk_context_t`** in vg_lite_vulkan.h, right before the `#if VGLITE_BLIT_PERF` block (~line 210):

```c
/* VK_EXT_multisampled_render_to_single_sampled (MSRTSS).
 * supported=1: device exposes the extension AND it was enabled at device creation.
 * enabled=1: MSAA render passes use single-sampled attachments + HW resolve.
 * Runtime override via env VGLITE_MSRTSS=1|0 (default: auto = supported). */
int msrtss_supported;
int msrtss_enabled;
```

- [ ] **Step 1.2: Add public toggle + query to vg_lite_vulkan.h** (near the g_msaa_samples extern, ~line 235):

```c
/* MSRTSS (multisampled render to single sampled) control. */
int vg_lite_vulkan_msrtss_enabled(void);        /* 1 = active MSRTSS path */
void vg_lite_vulkan_set_msrtss_enabled(int on); /* flushes+invalidates; -1 = auto */
```

- [ ] **Step 1.3: Implement them in vg_lite_vulkan.c** (place next to `vg_lite_vulkan_set_msaa_samples`, after line 123):

```c
int vg_lite_vulkan_msrtss_enabled(void) { return g_vk_ctx.msrtss_enabled; }

void vg_lite_vulkan_set_msrtss_enabled(int on)
{
    if (!g_vk_ctx.device) return;              /* pre-init: resolved at init */
    int want = (on < 0) ? g_vk_ctx.msrtss_supported : (on && g_vk_ctx.msrtss_supported);
    want = !!want;
    if (want == g_vk_ctx.msrtss_enabled) return;
    /* Reuse the sample-switch invalidation: flush, destroy per-buffer RPs,
     * attachments and pipelines; they rebuild lazily in the new mode.
     * needs_seed=1 is correct for MSRTSS too (resolve_image is destroyed,
     * first pass must re-seed it from the target). */
    vg_lite_vulkan_set_msaa_samples((g_msaa_samples == VK_SAMPLE_COUNT_2_BIT) ? 2 : 4);
    /* set_msaa_samples early-returns when sample count is unchanged; force
     * invalidation explicitly instead: */
    /* (if the call above early-returned, do the invalidation manually) */
    g_vk_ctx.msrtss_enabled = want;
    fprintf(stderr, "[msrtss] %s\n", want ? "enabled" : "disabled");
}
```

**IMPORTANT implementation note:** `vg_lite_vulkan_set_msaa_samples` early-returns at line 78 when the sample count is unchanged, so it cannot be used as the invalidation hammer here. Refactor instead: extract lines 92–121 of `set_msaa_samples` (flush → drop current fb → `destroy_buffer_msaa_objects` for all registry entries with `msaa_needs_seed=1; msaa_dirty=0` → `vg_lite_vulkan_destroy_pipelines()`) into a new static helper `invalidate_all_msaa_state(void)`, call it from both `set_msaa_samples` and `set_msrtss_enabled`. Do not change observable behavior of `set_msaa_samples`.

- [ ] **Step 1.4: Verify build**

Run: `cmake --build build --config Debug` (config 1 dir; create with the AGENTS.md command first if missing)
Expected: compiles clean (fields unused so far), 0 errors.

---

### Task 2: Enumerate + enable the extension at device creation

**Files:**
- Modify: `vglite_vulkan_2026/src/vg_lite_vulkan.c` `vg_lite_vulkan_init()`, lines 266–337

- [ ] **Step 2.1: Enumerate the extension unconditionally.** After the `vkGetPhysicalDeviceFeatures2` call (line 279) and BEFORE the `if (vk12_features_query.scalarBlockLayout)` branch (line 297), add a standalone enumeration block (do NOT nest it inside the scalar fallback — the current `vkEnumerateDeviceExtensionProperties` call at lines 304-306 only runs in the fallback branch):

```c
/* MSRTSS: enumerate once, unconditionally, for both enablement and
 * auto-detection. */
uint32_t msrtss_dext_count = 0;
vkEnumerateDeviceExtensionProperties(g_vk_ctx.physical_device, NULL,
                                     &msrtss_dext_count, NULL);
VkExtensionProperties *msrtss_dexts =
    malloc(sizeof(VkExtensionProperties) * (msrtss_dext_count ? msrtss_dext_count : 1));
vkEnumerateDeviceExtensionProperties(g_vk_ctx.physical_device, NULL,
                                     &msrtss_dext_count, msrtss_dexts);
int msrtss_avail = 0;
for (uint32_t i = 0; i < msrtss_dext_count; i++) {
    if (strcmp(msrtss_dexts[i].extensionName,
               VK_EXT_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_EXTENSION_NAME) == 0) {
        msrtss_avail = 1;
        break;
    }
}
free(msrtss_dexts);
```

- [ ] **Step 2.2: Enable extension + chain its feature struct.** After the scalar-block-layout if/else (line 323), before `dev_ci.pEnabledFeatures = NULL;` (line 327):

```c
static VkPhysicalDeviceMultisampledRenderToSingleSampledFeaturesEXT msrtss_features = {0};
msrtss_features.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_FEATURES_EXT;
msrtss_features.multisampledRenderToSingleSampled = VK_TRUE;

int msrtss_enable = msrtss_avail;
{
    const char *env = getenv("VGLITE_MSRTSS");
    if (env && env[0] == '0') msrtss_enable = 0;
    if (env && env[0] == '1' && !msrtss_avail)
        fprintf(stderr, "[msrtss] VGLITE_MSRTSS=1 but extension not supported\n");
}
if (msrtss_enable) {
    dev_exts[dev_ext_count++] =
        VK_EXT_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_EXTENSION_NAME;
    dev_ci.enabledExtensionCount = dev_ext_count;
    dev_ci.ppEnabledExtensionNames = dev_exts;
    /* Append to the existing pNext chain. The chain head is the static
     * enable_feat2; its pNext is either &vk12_features (1.2 core path,
     * line 299) or NULL. Link msrtss_features at the tail in both cases. */
    msrtss_features.pNext = enable_feat2.pNext;   /* vk12_features or NULL */
    enable_feat2.pNext = &msrtss_features;
    g_vk_ctx.msrtss_supported = 1;
    fprintf(stderr, "[msrtss] VK_EXT_multisampled_render_to_single_sampled enabled\n");
} else {
    fprintf(stderr, "[msrtss] not available%s\n",
            msrtss_avail ? " (disabled via VGLITE_MSRTSS=0)" : "");
}
```

**Chain-ordering caveat:** `vk12_features.pNext` must stay NULL (it currently is); only `enable_feat2.pNext` is rewired, so the chain is `enable_feat2 → msrtss_features → vk12_features` or `enable_feat2 → msrtss_features`. Both are valid (structs are static, alive until after vkCreateDevice; note `msrtss_features` and the existing `enable_feat2` are declared `static` for exactly this reason).

**Array-size caveat:** `dev_exts[8]` — worst case now is `VK_EXT_scalar_block_layout` + MSRTSS = 2 entries. Fits.

- [ ] **Step 2.3: Resolve the auto mode after device creation.** After `volkLoadDevice` (line 336) add:

```c
g_vk_ctx.msrtss_enabled = g_vk_ctx.msrtss_supported; /* auto; VGLITE_MSRTSS=0 already zeroed supported-side enable above */
```

(When forced off, `msrtss_supported` is still recorded as 1 only in the enable branch; if forced off we never set supported — acceptable: `VGLITE_MSRTSS` is a process-lifetime env, runtime toggling goes through `vg_lite_vulkan_set_msrtss_enabled` which requires the extension to have been enabled at device creation. Document this in the function comment.)

- [ ] **Step 2.4: Verify with validation layers**

Run config 1 build; run any one test (e.g. `build\tests\Debug\test_blit.exe` or equivalent from the tests dir listing).
Expected: stderr shows `[msrtss] ... enabled` on supporting GPUs (or the "not available" line elsewhere); validation layer shows NO errors about the feature chain. If the GPU does not support it, all remaining tasks must still leave the fallback path pixel-identical (the `msrtss_enabled=0` branches are all no-ops).

---

### Task 3: MSRTSS render pass factories (`vkCreateRenderPass2`)

**Files:**
- Modify: `vglite_vulkan_2026/src/vg_lite_vulkan.c` — add one static helper + branch in both factories (lines 498–661)

- [ ] **Step 3.1: Add a static MSRTSS RP builder** above `vg_lite_vulkan_create_render_pass` (line 498). One builder serves both LOAD and CLEAR variants via the `color_load` parameter, keeping the two public factory signatures unchanged:

```c
/* MSRTSS render pass: 2 single-sampled attachments (color = the resolve
 * scratch, depth D24S8), rasterizationSamples = g_msaa_samples via the
 * subpass pNext chain. Hardware loads/replicates the 1x content and
 * resolves on store — replaces the 4x color sidecar + resolve attachment. */
static VkRenderPass create_render_pass_msrtss(VkFormat format,
                                              VkAttachmentLoadOp color_load)
{
    VkAttachmentDescription2 att[2] = {0};
    /* [0] color: 1x OPTIMAL scratch; store = the HW resolve; still copied
     * to the LINEAR target afterwards (direct-to-LINEAR smears). */
    att[0].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    att[0].format = format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = color_load;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = (color_load == VK_ATTACHMENT_LOAD_OP_LOAD)
        ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; /* same as legacy resolve att */
    /* [1] depth/stencil: also single-sampled under MSRTSS. */
    att[1].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    att[1].format = VK_FORMAT_D24_UNORM_S8_UINT;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 color_ref = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference2 depth_ref = {VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2};
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkMultisampledRenderToSingleSampledInfoEXT msrtss = {0};
    msrtss.sType = VK_STRUCTURE_TYPE_MULTISAMPLED_RENDER_TO_SINGLE_SAMPLED_INFO_EXT;
    msrtss.multisampledRenderToSingleSampledEnable = VK_TRUE;
    msrtss.rasterizationSamples = g_msaa_samples;

    VkSubpassDescription2 sub = {VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;
    sub.pDepthStencilAttachment = &depth_ref;
    sub.pNext = &msrtss;   /* no pResolveAttachments — the store IS the resolve */

    /* Same dependency triple as the legacy factories (multi-pass accumulation
     * + seed-write→blend-read visibility; the seed is now a copy but the
     * extern→subpass dependency still covers target writes → attachment loads). */
    VkSubpassDependency2 deps[3] = {0};
    /* ... fill deps[0..2] EXACTLY like vg_lite_vulkan_create_render_pass
     * lines 544-567 (srcSubpass EXTERNAL/0/0, COLOR_ATTACHMENT_OUTPUT stages,
     * COLOR_ATTACHMENT_READ|WRITE access, deps[2] BY_REGION) using the
     * VkSubpassDependency2 sType ... */

    VkRenderPassCreateInfo2 ci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
    ci.attachmentCount = 2;
    ci.pAttachments = att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 3;
    ci.pDependencies = deps;

    VkRenderPass rp;
    if (vkCreateRenderPass2(g_vk_ctx.device, &ci, NULL, &rp) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return rp;
}
```

(The `deps` fill-in is mechanical: same values as lines 544–567 with `sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2` and `viewOffset = 0`. Write it out fully in the implementation — do not leave a comment placeholder.)

- [ ] **Step 3.2: Branch in both public factories.** First line of `vg_lite_vulkan_create_render_pass` (line 498) and `vg_lite_vulkan_create_render_pass_clear` (line 587):

```c
if (g_vk_ctx.msrtss_enabled)
    return create_render_pass_msrtss(format, VK_ATTACHMENT_LOAD_OP_LOAD);   /* LOAD variant */
/* ... existing legacy body unchanged ... */
```
and for the clear factory: `VK_ATTACHMENT_LOAD_OP_CLEAR`.

- [ ] **Step 3.3: Verify pipeline compatibility.** All 12 pipeline-creation sites (`gp_ci.renderPass = rp` at 1259, 1449, 1794, 1915, 2036, 2157, 2271, 2414 + vg_lite_draw.c 120, 220) build their compatibility RP by calling the same factories, and `ms.rasterizationSamples` already reads `g_msaa_samples` at all 10 sites — under MSRTSS a 4x pipeline against a 1x-attachment RP is exactly what the spec requires (validation layers will actively confirm; treat any VUID-vkCmdBindPipeline mismatch message as a hard failure of this task).

- [ ] **Step 3.4: Build + quick validation**

Run: `cmake --build build --config Debug`, run one MSAA test.
Expected: may fail/render wrong until Tasks 4–5 land (set_render_target still binds 3 views against a 2-attachment RP). That is expected at this checkpoint — do not fix by touching the factories.

---

### Task 4: `set_render_target_ex` MSRTSS branch (2-attachment framebuffer)

**Files:**
- Modify: `vglite_vulkan_2026/src/vg_lite_vulkan.c` `vg_lite_vulkan_set_render_target_ex` (lines 808–911)

- [ ] **Step 4.1: Skip the 4x color sidecar; create 1x depth.** Change the attachment-creation section (lines 834–866) so that:

```c
if (g_vk_ctx.msrtss_enabled) {
    /* depth is single-sampled under MSRTSS */
    if (internal->msaa_depth_image == VK_NULL_HANDLE) {
        if (create_attachment(&internal->msaa_depth_image, &internal->msaa_depth_memory,
                &internal->msaa_depth_view,
                target->width, target->height, VK_FORMAT_D24_UNORM_S8_UINT,
                VK_SAMPLE_COUNT_1_BIT,                    /* was g_msaa_samples */
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) < 0)
            return VG_LITE_OUT_OF_MEMORY;
    }
    /* resolve_image is still needed — it IS the color attachment now.
       Create it identically to the legacy path below (same usage flags:
       COLOR_ATTACHMENT | TRANSFER_SRC). */
}
```
Then guard the legacy `msaa_color` block (834–843) with `if (!g_vk_ctx.msrtss_enabled && ...)`, keep `resolve_image` creation (856–866) common to both modes, and guard the legacy 4x depth creation (845–854) with `!msrtss_enabled` too.

- [ ] **Step 4.2: Framebuffer + clear values.** Replace lines 868–907:

```c
VkImageView fb_views[3];
uint32_t fb_att_count;
if (g_vk_ctx.msrtss_enabled) {
    fb_views[0] = internal->resolve_view;
    fb_views[1] = internal->msaa_depth_view;
    fb_att_count = 2;
} else {
    fb_views[0] = internal->msaa_color_view;
    fb_views[1] = internal->resolve_view;
    fb_views[2] = internal->msaa_depth_view;
    fb_att_count = 3;
}
VkFramebufferCreateInfo fb_ci = {0};
fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
fb_ci.renderPass = rp;
fb_ci.attachmentCount = fb_att_count;
fb_ci.pAttachments = fb_views;
fb_ci.width  = target->width;
fb_ci.height = target->height;
fb_ci.layers = 1;
```
…and clear values: MSRTSS uses `clear_values[0]` = color (if `clear_value`), `clear_values[1]` = depth `{0.0f, 0}` — note the depth slot moves from index 2 to 1; `rpbi.clearValueCount = fb_att_count;`.

Also set `g_vk_ctx.current_msaa_color_image = VK_NULL_HANDLE;` in MSRTSS mode (line 882 becomes conditional) — `current_resolve_image` must still be set (the deferred copy path depends on it, vg_lite_vulkan.c:975–979).

- [ ] **Step 4.3: Build + run one MSAA test (blit) with `VGLITE_MSRTSS=1`**

Expected: no validation errors; output PNG may already be visually correct for single-pass blits. Multi-pass/draw cases still need Task 5 (seed). If the GPU lacks the extension, verify `VGLITE_MSRTSS=1` logs the warning and the run is identical to baseline.

---

### Task 5: `seed_msaa` becomes a copy in MSRTSS mode

**Files:**
- Modify: `vglite_vulkan_2026/src/vg_lite_vulkan.c` `vg_lite_vulkan_seed_msaa` (lines 757–806)

- [ ] **Step 5.1: Insert MSRTSS branch at the top of the function:**

```c
if (g_vk_ctx.msrtss_enabled) {
    /* No seed draw: the RP loads the 1x resolve_image directly. But after
     * no-MSAA RP writes / CPU writes / sample switches, the target holds
     * newer content than resolve_image — sync it with a full-image copy
     * (must run OUTSIDE a render pass; callers invoke seed_msaa before
     * set_render_target_ex begins the MSAA RP — verify each call site:
     * vg_lite.c:1732, vg_lite_draw.c:519,794,1061,1280). */
    buffer_internal_t *internal = (buffer_internal_t *)target->handle;
    /* resolve_image lazily exists? If not, nothing to seed — RP will be
     * CLEAR/first-use anyway; first MSRTSS pass after invalidation uses
     * loadOp=LOAD on stale content only when needs_seed was set, and
     * invalidation destroyed resolve_image. Recreate it here if missing. */
    if (internal->resolve_image == VK_NULL_HANDLE) {
        VkFormat vkfmt0 = vg_lite_format_to_vk(target->format);
        if (create_attachment(&internal->resolve_image, &internal->resolve_memory,
                &internal->resolve_view, target->width, target->height, vkfmt0,
                VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) < 0)
            return VG_LITE_OUT_OF_MEMORY;
    }
    /* barrier target: GENERAL(=current CPU-visible layout) → TRANSFER_SRC */
    /* barrier resolve_image: COLOR_ATTACHMENT_OPTIMAL/UNDEFINED → TRANSFER_DST */
    /* vkCmdCopyImage resolve-size region (mirror resolve_msaa_to_target,
     * lines 923–949, in the reverse direction) */
    /* barrier resolve_image → COLOR_ATTACHMENT_OPTIMAL (RP initialLayout),
     * target back → GENERAL */
    return VG_LITE_SUCCESS;
}
```
Write the three barrier/copy blocks out fully (copy the barrier struct pattern from `resolve_msaa_to_target` lines 923–964; src layout for `internal->image` is `VK_IMAGE_LAYOUT_GENERAL` as in line 926).

**Call-order caveat:** confirm seed sites (vg_lite.c:1700–1732, vg_lite_draw.c:493–519, 781–795, 1048–1062, 1267–1281) call `resolve_msaa_to_target` first, then `set_render_target_ex` — the copy must land while no RP is active. If any site currently relies on seeding *inside* an already-begun RP (the old seed was a draw INSIDE the MSAA RP!), restructure: in MSRTSS mode `set_render_target_ex` must be called AFTER the seed copy. Check each site; the sites flush+resolve first (they end the RP), so placing the copy before `set_render_target_ex` is safe. Document per-site in the PR description.

- [ ] **Step 5.2: Build + run the draw tests**

Run: config 1 build, run the draw/blit/pattern/radial test exes plus `msaaSwitch`.
Expected: all pass, PNG dumps in `dump_lin_msaa_obb/` visually match legacy (compare against a baseline produced by `VGLITE_MSRTSS=0` run).

---

### Task 6: End-of-pass / resolve / invalidation audit (verify-only)

**Files:**
- Verify (modify only if a mismatch is found): `vg_lite_vulkan.c` lines 918–1035, 40–64, 92–123

- [ ] **Step 6.1: Confirm `resolve_msaa_to_target` works unchanged.** It copies `resolve_image → internal->image`. Under MSRTSS the RP finalLayout of the color attachment is `TRANSFER_SRC_OPTIMAL` (same as legacy resolve attachment) and `msaa_dirty=1` is still set in `end_render_pass` (line 975–979 keys on `current_resolve_image && current_fb_image`, both still set). Read the function and check the `if (!internal->msaa_dirty)` early-out plus the dirty-flag lifecycle (set at 979, cleared at 966) — no edits expected.

- [ ] **Step 6.2: Confirm `end_render_pass` barrier block.** The `current_msaa_color_image` barrier (lines 997–1012) is skipped automatically in MSRTSS mode (handle is NULL). The no-MSAA host barrier branch (980–995) is unaffected. No edits expected.

- [ ] **Step 6.3: Confirm invalidation.** `destroy_buffer_msaa_objects` (40–64) destroys `resolve_image`/RPs — correct for MSRTSS too; after invalidation `msaa_needs_seed=1` forces the Task-5 copy path to resync from the target on first use. Check `vg_lite_free` path still unregisters (registry). No edits expected.

- [ ] **Step 6.4: Record any actual edit in FIXES.md** (Symptom/Root Cause/Solution) — only if Steps 6.1–6.3 found a real mismatch requiring a change.

---

### Task 7: Full 8-config test matrix

- [ ] **Step 7.1: Produce MSRTSS baselines**: run each MSAA config (1, 2, 5, 6) twice — `VGLITE_MSRTSS=0` and `VGLITE_MSRTSS=1` — and pixel-compare the dump dirs. Differences beyond existing run-to-run variance (none expected — fixed test content) are bugs: fix before proceeding.

```powershell
# Example, config 1 (repeat for 2,5,6 with their build/dump dirs)
cmake -B build -DVGLITE_TARGET_OPTIMAL=OFF -DVGLITE_BLIT_MSAA=ON -DVGLITE_BLIT_OBB=ON
cmake --build build --config Debug
$env:VGLITE_MSRTSS='0'; # run all exes in build\tests\Debug\
$env:VGLITE_MSRTSS='1'; # run again; compare dump_lin_msaa_obb\ outputs
```

- [ ] **Step 7.2: Default-mode (unset env) full matrix**: all 8 configs, expect **37/38 PASS** each (only `test_sft_blit` pre-existing crash). Record the matrix summary.

- [ ] **Step 7.3: Forced-fallback regression**: `VGLITE_MSRTSS=0` on configs 1, 2, 5, 6 — must be identical to pre-change baseline (proves the legacy path is untouched).

---

### Task 8: Documentation

- [ ] **Step 8.1: FIXES.md** — append entry: performance/feature change (MSAA sidecar + seed draw elimination), root cause (explicit MSAA sidecar + CPU-side seeding vs HW tile resolve), solution (MSRTSS factories + copy-seed + fallback), test evidence.
- [ ] **Step 8.2: README.md** — document `VGLITE_MSRTSS` env var, GPU support note, and memory/bandwidth effect, per the AGENTS.md pre-push checklist.

---

## Risk register

| Risk | Mitigation |
|---|---|
| GPU lacks the extension | Everything is behind `msrtss_enabled`; fallback path byte-identical (Task 7.3 proves it) |
| Depth attachment 1x + 4x rasterizationSamples rejected by validation | This is legal ONLY under MSRTSS chaining — validation must be clean; any VUID error = stop and re-check chain wiring |
| Multi-pass accumulation semantics (blit_accum, msaaSwitch) | Hardware load replicates 1x samples exactly like the old seed blit; Task 7.1 pixel-compare on exactly those tests |
| Seed call sites assumed in-RP drawing | Audited in Task 5.1 — all sites flush/resolve before seeding; copy runs RP-external |
| `dev_exts` overflow / pNext lifetime | 2 of 8 slots used; feature structs are `static` like `enable_feat2` |
| LINEAR smear workaround lost | Deliberately kept: resolve_image remains the RP color attachment and the final `vkCmdCopyImage` to LINEAR target stays |
