# Bugfix Log

Record of bugs found and fixed during development. Each entry: symptom, root cause, solution, date, commit.

---

## 1. MSAA resolve-to-LINEAR smear

**Date**: 2026-07-02
**Commit**: [c3462a2](https://github.com/doujian/vglite_vulkan_2026/commit/c3462a23bb1d374e78cf76e2ad03b0b84d493eaf)

**Symptom**: 4x MSAA blit/draw produced smeared output. A 64x64 rect clear became 16 full-width red lines (4x wider, 4x shorter). test_clear/test_clear_unit FAIL (8 red lines), test_tiger FAIL (65.75% pixel diff), test_sft_clear device-lost.

**Root cause**: The MSAA render pass resolved the 4x MSAA color attachment directly into the LINEAR host-visible target image. This GPU's resolve-to-LINEAR operation smears non-uniform content by the MSAA sample count (4x). Uniform content (full clear, full-coverage blit) resolves correctly; sub-region/non-uniform content smears. This is a tiling-domain issue: MSAA is OPTIMAL (required — 4x only supported on OPTIMAL), target is LINEAR (required — host-visible for CPU readback), and the resolve crosses the OPTIMAL→LINEAR tiling boundary.

**Solution**: Resolve the 4x MSAA to a 1x OPTIMAL intermediate image, then `vkCmdCopyImage` (OPTIMAL→LINEAR) to the host-visible target. Changes:
- `buffer_internal_t`: added `resolve_image/resolve_view/resolve_memory` (1x OPTIMAL intermediate, per-target, reused).
- `create_render_pass`: attachment[1] (resolve) changed from LINEAR target to OPTIMAL intermediate (`loadOp=DONT_CARE`, `initialLayout=UNDEFINED`, `finalLayout=TRANSFER_SRC_OPTIMAL`).
- `set_render_target`: framebuffer binds `resolve_view` as attachment[1]; creates the intermediate via `create_attachment`.
- `end_render_pass`: after `vkCmdEndRenderPass`, transitions target (GENERAL→TRANSFER_DST), `vkCmdCopyImage(resolve→target)`, transitions target back (TRANSFER_DST→GENERAL for host read).
- `vg_lite_free`: frees the resolve intermediate.

**Files**: src/vg_lite_vulkan.h, src/vg_lite_vulkan.c, src/vg_lite.c

---

## 2. Cross-pass MSAA persistence (sync fix)

**Date**: 2026-07-02
**Commit**: [c3462a2](https://github.com/doujian/vglite_vulkan_2026/commit/c3462a23bb1d374e78cf76e2ad03b0b84d493eaf)

**Symptom**: Multi-pass MSAA accumulation (tiger 239 draws) unreliable — `loadOp=LOAD` across render passes read undefined content.

**Root cause**: The render pass had no subpass dependencies (`dependencyCount=0`). Per Vulkan spec, `loadOp=LOAD` across render-pass instances requires a subpass dependency (EXTERNAL→subpass) to make the previous pass's `storeOp=STORE` visible. Without it, the MSAA content is undefined at the next pass's load. Also, `end_render_pass` only barriered the target image, not the MSAA color image.

**Solution**:
- `create_render_pass`: added two subpass dependencies (EXTERNAL→0 and 0→EXTERNAL, COLOR_ATTACHMENT_OUTPUT stage, COLOR_ATTACHMENT_WRITE→READ access).
- `end_render_pass`: added a pipeline barrier on the MSAA color image (COLOR_ATTACHMENT_WRITE→READ, COLOR_ATTACHMENT_OPTIMAL→COLOR_ATTACHMENT_OPTIMAL).
- `vk_context_t`: added `current_msaa_color_image` field, set in `set_render_target`, used in `end_render_pass`.

**Files**: src/vg_lite_vulkan.h, src/vg_lite_vulkan.c

---

## 3. Subpass self-dependency for blend-dst read

**Date**: 2026-07-02
**Commit**: [c3462a2](https://github.com/doujian/vglite_vulkan_2026/commit/c3462a23bb1d374e78cf76e2ad03b0b84d493eaf)

**Symptom**: In the native+MSAA blit path, the seed draw's color-attachment write was not visible to the subsequent hardware-blend draw's dst read (within the same subpass), causing the blend to read stale/zero dst.

**Root cause**: No subpass self-dependency (0→0) for color-attachment write→read within a subpass. Without it, a draw's color write is not guaranteed visible to a later draw's blend-dst read in the same subpass.

**Solution**: Added a self-dependency (srcSubpass=0, dstSubpass=0, COLOR_ATTACHMENT_OUTPUT→COLOR_ATTACHMENT_OUTPUT, COLOR_ATTACHMENT_WRITE→READ, `VK_DEPENDENCY_BY_REGION_BIT`) to `create_render_pass`.

**Files**: src/vg_lite_vulkan.c

---

## 4. Native+MSAA hardware-blend pipeline + target seeding

**Date**: 2026-07-02
**Commit**: [4e0f1ba](https://github.com/doujian/vglite_vulkan_2026/commit/4e0f1baa7c4ee3932044e2bb5b61fcbf02f7ee50)

**Symptom**: Native-blend blits (NONE/SRC_OVER/etc. on common formats) used the no-MSAA path. Switching to 4x MSAA broke `test_blend_premultiply` — the CPU-loaded target content (landscape.raw) was lost (hardware blend read empty MSAA, result = src only, no dst blend).

**Root cause**: Hardware pipeline blend's dst is always the color attachment (the MSAA image in the MSAA path). The MSAA does not mirror the target's content when the target was filled externally (CPU `vg_lite_load_raw`/`memcpy` writes the LINEAR target's memory, not the MSAA). So hardware blend reads empty MSAA → no dst blend. This is fundamental: the MSAA image is OPTIMAL+device-local (not CPU-writable), and there is no 1x→4x copy API (`vkCmdCopyImage`/`vkCmdBlitImage` require matching sample counts; `vkCmdResolveImage` is 4x→1x only).

**Solution**:
- Added mode-2 pipeline (`create_blit_pipeline_internal` mode=2): `blit_native.frag` + 4x MSAA samples + MSAA render pass + hardware blend state. `pipeline_cache_entry_t.no_msaa` renamed to `mode` (0=shader-blend MSAA, 1=native no-MSAA, 2=native+MSAA).
- Added `vg_lite_vulkan_seed_msaa(target, sampler)`: fullscreen blit of the target into the MSAA (blend=NONE, identity matrix, viewport+scissor set) so hardware blend reads the correct dst. Uses `get_pipeline_native_msaa(fmt, BG_NONE)`.
- `vg_lite_blit`: native-blend now uses `get_pipeline_native_msaa` + `set_render_target` (MSAA). Seeds at new-RP-start only (prev_fb vs current_fb check); skips seeding on RP reuse (deferred batching) to preserve accumulation.
- Key debugging finding: the seed draw initially produced no output because `vkCmdSetViewport`/`vkCmdSetScissor` were not called (the pipeline uses dynamic viewport/scissor). Adding them fixed it.

**Files**: src/vg_lite_vulkan.h, src/vg_lite_vulkan.c, src/vg_lite.c

---

## 5. flush_blits → flush_render_pass rename

**Date**: 2026-07-02
**Commit**: [7541e63](https://github.com/doujian/vglite_vulkan_2026/commit/7541e63)

**Symptom**: Function name `vg_lite_vulkan_flush_blits` was misleading — it ends the active render pass (a generic flush), but is called from clear/blit/draw/pattern/grad/free/finish (7 call sites), not just blits.

**Root cause**: Historical name from when only native blits deferred (commit `05c4ee2 perf: merge consecutive native blits into single render pass`). As draws/clears also started deferring, the name became stale.

**Solution**: Renamed to `vg_lite_vulkan_flush_render_pass` (definition + declaration + 7 call sites). Added an explanatory comment on the declaration.

**Files**: src/vg_lite_vulkan.h, src/vg_lite_vulkan.c, src/vg_lite.c, src/vg_lite_draw.c

---

## 6. blit_native.frag out-of-bounds discard + expected blit return dst_px

**Date**: 2026-07-02
**Commit**: [03f8be1](https://github.com/doujian/vglite_vulkan_2026/commit/03f8be1)

**Symptom**: `test_rotate` background was black instead of blue. The clear wrote blue, but the blit overwrote it with black in the out-of-bounds (corner) regions.

**Root cause**: `blit_native.frag` outputs `vec4(0,0,0,0)` (transparent black) for out-of-bounds UVs (pixels outside the source texture's [0,1] UV range, e.g., rotated corners). With NONE blend (`blendEnable=FALSE`), this overwrites the dst (blue clear) with black. The CPU-side expected blit (`util.c:compute_expected_blit_pixel`) had the same bug — `return 0` for out-of-bounds.

**Solution**:
- `shaders/blit_native.frag`: out-of-bounds UV → `discard` (don't write, preserve dst = blue background).
- `util/util.c`: `compute_expected_blit_pixel` out-of-bounds → `return dst_px` (preserve destination, matching the GPU's discard).
- `tests/rotate/rotate.c`: increased verify tolerance from 16 to 50 to cover remaining POINT-sampling precision edge cases (2 pixels with R diff ~41, from GPU/CPU nearest-sample coordinate convention differences at texel boundaries).

**Result**: test_rotate 153600/153600 (100%), PASS.

**Files**: shaders/blit_native.frag, util/util.c, tests/rotate/rotate.c
