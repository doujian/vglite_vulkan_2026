import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np

fig, axes = plt.subplots(1, 3, figsize=(18, 6))

# Common data
cos33, sin33 = np.cos(np.radians(33)), np.sin(np.radians(33))
cx, cy = 160, 240

def transform(sx, sy):
    px, py = sx * 2.5, sy * 3.75
    px2, py2 = px - cx, py - cy
    rx = px2 * cos33 - py2 * sin33
    ry = px2 * sin33 + py2 * cos33
    return rx + cx, ry + cy

# Source corners (in source pixel space, 128x128)
src_corners = [(0,0), (128,0), (128,128), (0,128)]
rotated = [transform(sx, sy) for sx, sy in src_corners]
rot_arr = np.array(rotated)

# AABB
abb_min = rot_arr.min(axis=0)
abb_max = rot_arr.max(axis=0)

# Target rect
target = patches.Rectangle((0, 0), 320, 480, linewidth=2, edgecolor='black', facecolor='#4488ff', alpha=0.3, label='Target (blue bg)')

for ax, title in zip(axes, ['Fullscreen Triangle', 'AABB', 'OBB']):
    ax.add_patch(patches.Rectangle((0, 0), 320, 480, linewidth=2, edgecolor='black', facecolor='#4488ff', alpha=0.3))
    ax.set_xlim(-150, 450)
    ax.set_ylim(-100, 560)
    ax.set_aspect('equal')
    ax.set_title(title, fontsize=14, fontweight='bold')
    ax.set_xlabel('x')
    ax.set_ylabel('y')

# --- Fullscreen triangle ---
ax = axes[0]
tri = plt.Polygon([(-1,-1), (640+1,-1), (-1, 960+1)], alpha=0.2, facecolor='red', edgecolor='red', linestyle='--', label='Fullscreen tri coverage')
ax.add_patch(tri)
ax.add_patch(plt.Polygon(rotated, alpha=0.5, facecolor='#ffaa00', edgecolor='black', linewidth=2, label='Source (rotated)'))
# Label the wrong area
ax.text(30, 30, 'CLAMP_TO_EDGE\nsmears source edge\nover blue bg', fontsize=8, color='red', ha='center',
        bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))
ax.legend(fontsize=7, loc='upper right')

# --- AABB ---
ax = axes[1]
aabb_rect = patches.Rectangle((abb_min[0], abb_min[1]), abb_max[0]-abb_min[0], abb_max[1]-abb_min[1],
                               alpha=0.2, facecolor='orange', edgecolor='orange', linewidth=2, linestyle='--', label='AABB')
ax.add_patch(aabb_rect)
ax.add_patch(plt.Polygon(rotated, alpha=0.5, facecolor='#ffaa00', edgecolor='black', linewidth=2, label='Source (rotated)'))
# Mark corner waste areas
for corner_name, (px, py) in zip(['C0','C1','C2','C3'], [(abb_min[0]+10, abb_min[1]+10), (abb_max[0]-10, abb_min[1]+10), (abb_max[0]-10, abb_max[1]-10), (abb_min[0]+10, abb_max[1]-10)]):
    ax.plot(px, py, 'rx', markersize=8)
ax.text(30, 30, 'CLAMP_TO_EDGE\nsmears source edge\nat AABB corners', fontsize=8, color='red', ha='center',
        bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))
ax.legend(fontsize=7, loc='upper right')

# --- OBB ---
ax = axes[2]
ax.add_patch(plt.Polygon(rotated, alpha=0.5, facecolor='#ffaa00', edgecolor='black', linewidth=2, label='Source (rotated)'))
for name, (px, py) in zip(['C0','C1','C2','C3'], rotated):
    ax.plot(px, py, 'ko', markersize=5)
    offset = {'C0': (5, -12), 'C1': (-5, -12), 'C2': (-5, 12), 'C3': (5, 12)}
    ax.annotate(f'{name}\n({px:.0f},{py:.0f})', (px, py), fontsize=7,
                textcoords='offset points', xytext=offset[name])
ax.text(160, 240, 'No discard\nneeded', fontsize=10, color='green', ha='center', va='center',
        bbox=dict(boxstyle='round', facecolor='white', alpha=0.8))
ax.legend(fontsize=7, loc='upper right')

fig.suptitle('Blit Discard Analysis: rotate 33° (source 128x128 → 320x480)', fontsize=13, fontweight='bold')
plt.tight_layout()
plt.savefig('docs/blit_discard_analysis.png', dpi=150, bbox_inches='tight')
print('saved docs/blit_discard_analysis.png')
