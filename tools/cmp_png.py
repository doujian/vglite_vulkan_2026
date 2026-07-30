"""Compare two PNG images pixel-by-pixel with rich diagnostics.

Usage:
    python cmp_png.py <golden.png> <ours.png> [threshold] [--sample x,y ...]
    python cmp_png.py <golden.png> <ours.png> --heatmap <out.png>

threshold: max per-pixel channel delta allowed (default 0).
--sample:  print golden vs ours RGBA at given coordinates.
--heatmap: write a difference heatmap PNG (red = big diff).
"""
import sys
import numpy as np
from PIL import Image


def load_rgba(path):
    img = Image.open(path).convert("RGBA")
    return np.asarray(img, dtype=np.int32), img.size  # (h,w,4)


def histogram(delta):
    """delta: 1D int array of abs differences. Returns bucket counts."""
    buckets = [0, 0, 0, 0, 0]
    buckets[0] = int(np.sum(delta == 0))
    buckets[1] = int(np.sum((delta >= 1) & (delta <= 4)))
    buckets[2] = int(np.sum((delta >= 5) & (delta <= 16)))
    buckets[3] = int(np.sum((delta >= 17) & (delta <= 64)))
    buckets[4] = int(np.sum(delta >= 65))
    return buckets


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = [a for a in sys.argv[1:] if a.startswith("--")]
    if len(args) < 2:
        print(__doc__)
        return 2

    golden_path, ours_path = args[0], args[1]
    threshold = int(args[2]) if len(args) >= 3 else 0

    g, gsize = load_rgba(golden_path)
    o, osize = load_rgba(ours_path)
    print(f"golden {golden_path}: {gsize[0]}x{gsize[1]} (WxH)")
    print(f"ours   {ours_path}: {osize[0]}x{osize[1]} (WxH)")
    gh, gw = g.shape[:2]
    oh, ow = o.shape[:2]
    if (gh, gw) != (oh, ow):
        print(f"DIMENSION MISMATCH golden={gw}x{gh} ours={ow}x{oh}")
        return 4

    # sample mode
    if "--sample" in opts:
        idx = opts.index("--sample")
        # sample coords come after --sample in opts order isn't reliable; parse from args after threshold
        samples = [a for a in args[3:] if "," in a]
        print(f"\n{'coord':>14}    {'golden':>20}    {'ours':>20}    {'dRGB':>14}")
        for s in samples:
            try:
                x, y = [int(v) for v in s.split(",")]
            except ValueError:
                continue
            if not (0 <= y < gh and 0 <= x < gw):
                print(f"({x:4d},{y:4d})    out of range")
                continue
            gp = g[y, x]
            op = o[y, x]
            d = tuple(abs(int(op[i]) - int(gp[i])) for i in range(4))
            print(f"({x:4d},{y:4d})    ({gp[0]:3d},{gp[1]:3d},{gp[2]:3d},{gp[3]:3d})    "
                  f"({op[0]:3d},{op[1]:3d},{op[2]:3d},{op[3]:3d})    ({d[0]:3d},{d[1]:3d},{d[2]:3d},{d[3]:3d})")
        return 0

    # full diff
    dr = np.abs(o[:, :, 0] - g[:, :, 0])
    dg = np.abs(o[:, :, 1] - g[:, :, 1])
    db = np.abs(o[:, :, 2] - g[:, :, 2])
    da = np.abs(o[:, :, 3] - g[:, :, 3])
    maxd = np.maximum(np.maximum(dr, dg), np.maximum(db, da))
    total = gw * gh
    mismatch_mask = maxd > threshold
    mismatch = int(np.sum(mismatch_mask))
    match = total - mismatch

    # sample mismatches
    print(f"\n--- 10 sample mismatches (threshold={threshold}) ---")
    ys, xs = np.nonzero(mismatch_mask)
    if len(ys) > 0:
        # pick spread-out samples
        step = max(1, len(ys) // 10)
        for k in range(0, min(10, len(ys)), 1):
            i = (k * step) if k < 9 else len(ys) - 1
            y, x = int(ys[i]), int(xs[i])
            gp = g[y, x]
            op = o[y, x]
            print(f"  ({x:4d},{y:4d}) golden=({gp[0]:3d},{gp[1]:3d},{gp[2]:3d},{gp[3]:3d}) "
                  f"ours=({op[0]:3d},{op[1]:3d},{op[2]:3d},{op[3]:3d}) "
                  f"d=({abs(int(op[0])-int(gp[0])):3d},{abs(int(op[1])-int(gp[1])):3d},{abs(int(op[2])-int(gp[2])):3d})")

    print(f"\n=== RESULT (threshold={threshold}) ===")
    print(f"total={total}  match={match} ({100.0*match/total:.2f}%)  "
          f"mismatch={mismatch} ({100.0*mismatch/total:.2f}%)")
    if mismatch > 0:
        mm = mismatch_mask
        print(f"avg delta on mismatched: "
              f"dR={dr[mm].mean():.2f} dG={dg[mm].mean():.2f} "
              f"dB={db[mm].mean():.2f} dA={da[mm].mean():.2f}")
        print(f"max delta: dR={int(dr.max())} dG={int(dg.max())} "
              f"dB={int(db.max())} dA={int(da.max())}")

    print(f"\nPer-channel delta histogram (all {total} px):")
    print(f"  bucket       R        G        B        A")
    hr, hg, hb, ha = histogram(dr), histogram(dg), histogram(db), histogram(da)
    labels = ["=0   ", "1-4  ", "5-16 ", "17-64", "65+  "]
    for i, lbl in enumerate(labels):
        print(f"  {lbl}    {hr[i]:8d} {hg[i]:8d} {hb[i]:8d} {ha[i]:8d}")

    # spatial: 8x8 grid of mismatch counts
    print(f"\nMismatch density 8x8 grid (rows=top..bottom, cols=left..right):")
    bands = 8
    for by in range(bands):
        y0 = by * gh // bands
        y1 = (by + 1) * gh // bands
        row = []
        for bx in range(bands):
            x0 = bx * gw // bands
            x1 = (bx + 1) * gw // bands
            cnt = int(np.sum(mismatch_mask[y0:y1, x0:x1]))
            row.append(f"{cnt:6d}")
        print(f"  rows[{y0:4d}-{y1-1:4d}]: " + " ".join(row))

    # row-band and col-band totals
    print(f"\nMismatches per row-band (8):")
    for b in range(bands):
        y0 = b * gh // bands
        y1 = (b + 1) * gh // bands
        print(f"  rows [{y0:4d}-{y1-1:4d}]: {int(np.sum(mismatch_mask[y0:y1, :]))}")
    print(f"Mismatches per col-band (8):")
    for b in range(bands):
        x0 = b * gw // bands
        x1 = (b + 1) * gw // bands
        print(f"  cols [{x0:4d}-{x1-1:4d}]: {int(np.sum(mismatch_mask[:, x0:x1]))}")

    # heatmap
    if "--heatmap" in opts:
        hi = opts.index("--heatmap")
        out_path = opts[hi + 1] if hi + 1 < len(opts) else "diff_heatmap.png"
        heat = np.zeros((gh, gw, 3), dtype=np.uint8)
        # green-ish where match, red where mismatch (scaled by maxd)
        norm = np.clip(maxd * 4, 0, 255).astype(np.uint8)
        heat[:, :, 0] = norm            # R = diff
        heat[:, :, 1] = (255 - norm) // 2  # G
        # mark golden path region outline roughly by where mismatch is
        Image.fromarray(heat, "RGB").save(out_path)
        print(f"\nHeatmap written: {out_path}")

    return 0 if mismatch == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
