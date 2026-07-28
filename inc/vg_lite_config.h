#ifndef VG_LITE_CONFIG_H
#define VG_LITE_CONFIG_H

/*
 * vg_lite_config.h — central compile-time configuration for VGLite Vulkan backend.
 *
 * Override any macro with -D on the compile command line, e.g.:
 *   cmake -DCMAKE_C_FLAGS="-DVGLITE_BLIT_MSAA=0 -DVGLITE_BLIT_OBB=0" ...
 */

/* =========================================================================
 * Blit configuration
 * ========================================================================= */

/* Use 4x MSAA render pass for native-blend blit.
 * 1 = 4x MSAA (default), 0 = 1x no-MSAA (lower overhead). */
#ifndef VGLITE_BLIT_MSAA
#define VGLITE_BLIT_MSAA 1
#endif

/* Use OBB-driven vertex shader for blit (oriented bounding box quad instead of fullscreen triangle).
 * 1 = enable OBB optimization (default), 0 = original fullscreen triangle.
 * This controls the initial value of g_vk_ctx.use_obb_blit.
 * Runtime switching is still available via vg_lite_set_blit_obb_mode(). */
#ifndef VGLITE_BLIT_OBB
#define VGLITE_BLIT_OBB 1
#endif

/* =========================================================================
 * Performance profiling
 * ========================================================================= */

/* Wrap every vkCmdDraw in vg_lite_blit() with GPU timestamps.
 * Auto-falls back to CPU wall-clock timer if GPU timestamps are unavailable.
 * 1 = enable (default), 0 = disable (zero overhead). */
#ifndef VGLITE_BLIT_PERF
#define VGLITE_BLIT_PERF 1
#endif

/* Number of timestamp query slots in the query pool.
 * Each blit consumes 2 slots. Must be >= 2. */
#ifndef VGLITE_TIMESTAMP_QUERY_COUNT
#define VGLITE_TIMESTAMP_QUERY_COUNT 4096
#endif

/* =========================================================================
 * Internal limits
 * ========================================================================= */

#ifndef MAX_PIPELINE_CACHE
#define MAX_PIPELINE_CACHE 64
#endif

#ifndef MAX_PENDING_FB
#define MAX_PENDING_FB 32
#endif

#ifndef MAX_PENDING_DESC
#define MAX_PENDING_DESC 64
#endif

#ifndef MAX_SCISSOR_RECTS
#define MAX_SCISSOR_RECTS 16
#endif

#endif /* VG_LITE_CONFIG_H */
