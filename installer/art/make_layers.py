"""Builds ARK Setup's key-art layers from render_layers.py output: py -3 -I make_layers.py <dir> [reference.jpg]
(numpy and Pillow). The reference defaults to Blender's own 8-bit full frame; pass the previously shipped key
art to compare against it.

plate.jpg   clean plate: Blender's dithered 8-bit plate plus the sphere's broad soft shadow (a smooth field
            estimated from full - plate outside the sphere), so the background is lit as in the original frame.
sphere.png  RGBA sprite, straight alpha, 640 px square around the sphere. Inside the coverage matte the colour is
            solved exactly from full = a*C + (1-a)*plate with minimal a; outside it, the low-passed residual
            (near glow) is kept the same way, without per-pixel noise. The rim fades to 0.
Also writes composite.png (sprite over the JPEG plate), compare.png (key art | composite | difference x8) and
layers.json with the sprite rectangle and statistics.
"""
import sys, json, io, hashlib, numpy as np
from PIL import Image

d = sys.argv[1]
keyart = sys.argv[2] if len(sys.argv) > 2 else d + '/full8.png'
A = np.load(d + '/A.npy').astype(np.float64); B = np.load(d + '/B.npy').astype(np.float64); m = np.load(d + '/m.npy').astype(np.float64)
H, W = m.shape
ys, xs = np.nonzero(m > 0.5 / 255)
cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
R = max(xs.max() - xs.min(), ys.max() - ys.min()) / 2
rr = np.hypot(*np.meshgrid(np.arange(W) - cx, np.arange(H) - cy)) / R

def box(x, r):
    pad = ((r + 1, r), (0, 0)) + ((0, 0),) * (x.ndim - 2)
    c = np.cumsum(np.pad(x, pad, mode='edge'), axis=0); x = (c[2 * r + 1:] - c[:-2 * r - 1]) / (2 * r + 1)
    pad = ((0, 0), (r + 1, r)) + ((0, 0),) * (x.ndim - 2)
    c = np.cumsum(np.pad(x, pad, mode='edge'), axis=1); return (c[:, 2 * r + 1:] - c[:, :-2 * r - 1]) / (2 * r + 1)

def gauss(x, r):
    return box(box(box(x, r), r), r)

# 1. the sphere's soft shadow on the background: masked low-pass of full - plate, ignoring the sphere and its glow
w = (rr > 1.3).astype(np.float64)
D = A - B
w40, w160 = gauss(w, 40), gauss(w, 160)
f40 = gauss(D * w[..., None], 40) / np.maximum(w40, 1e-6)[..., None]
f160 = gauss(D * w[..., None], 160) / np.maximum(w160, 1e-6)[..., None]
k = np.clip(w40 / 0.5, 0, 1)[..., None]                  # far inside the sphere the wide estimate fills the hole
field = k * f40 + (1 - k) * f160
plate8 = np.asarray(Image.open(d + '/plate8.png').convert('RGB'), dtype=np.float64) / 255
P = np.clip(plate8 + field, 0, 1)                       # shipped plate (before JPEG)
P16 = np.clip(B + field, 0, 1)                          # same plate without dither, for the maths
plate_img = Image.fromarray(np.round(P * 255).astype(np.uint8))
best = None
for q in range(95, 60, -1):
    buf = io.BytesIO(); plate_img.save(buf, 'JPEG', quality=q, optimize=True, progressive=True, subsampling=2)
    if buf.tell() <= 120 * 1024:
        best = (q, buf.getvalue()); break
open(d + '/plate.jpg', 'wb').write(best[1])
Pj = np.asarray(Image.open(d + '/plate.jpg').convert('RGB'), dtype=np.float64) / 255
print('plate.jpg quality', best[0], len(best[1]), 'bytes; shadow field min/mean', field.mean(axis=2).min() * 255, field.mean(axis=2).mean() * 255)

# 2. sprite
size = 640
x0 = int(round(cx - size / 2)); y0 = int(round(cy - size / 2))
sl = (slice(y0, y0 + size), slice(x0, x0 + size))
Ac, Pc, mc = A[sl], P16[sl], m[sl]
noise = 1.5 / 255

def need_alpha(Rd, Pb):
    a = np.zeros(Rd.shape[:2])
    bright = Rd > noise; dark = Rd < -noise
    t = np.zeros_like(Rd)
    t[bright] = Rd[bright] / np.maximum(1 - Pb[bright], 1e-6)
    t[dark] = -Rd[dark] / np.maximum(Pb[dark], 1e-6)
    return np.clip(t.max(axis=2), 0, 1)

exact = need_alpha(Ac - Pc, Pc)
Rl = gauss(Ac - Pc, 2)                                   # low-passed residual: glow without noise
soft = need_alpha(Rl, Pc)
inside = gauss(mc, 2) > 0.001                            # the matte plus a few pixels: the sharp rim glow is solved exactly
a = np.where(inside, np.maximum(mc, exact), soft)
yy, xx = np.mgrid[0:size, 0:size]
rc = np.hypot(xx - (cx - x0), yy - (cy - y0)) / (size / 2)
a *= np.clip((1.0 - rc) / 0.08, 0, 1)                    # fade to nothing at the crop rim
target = np.where(inside[..., None], Ac, Pc + Rl)
C = np.clip((target - (1 - a)[..., None] * Pc) / np.maximum(a, 1e-6)[..., None], 0, 1)
a8 = np.round(a * 255).astype(np.uint8)
C8 = np.round(C * 255).astype(np.uint8); C8[a8 == 0] = 0
Image.fromarray(np.dstack([C8, a8]), 'RGBA').save(d + '/sphere.png', optimize=True)

# 3. composite back over the JPEG plate and compare with the original key art and the full render
comp = Pj.copy()
af = a8[..., None] / 255.0
comp[sl] = C8 / 255.0 * af + (1 - af) * Pj[sl]
full8 = np.asarray(Image.open(d + '/full8.png').convert('RGB'), dtype=np.float64) / 255
K = np.asarray(Image.open(keyart).convert('RGB'), dtype=np.float64) / 255
Image.fromarray(np.round(comp * 255).astype(np.uint8)).save(d + '/composite.png')
def stats(x, y):
    e = np.abs(x - y) * 255
    return {'mean': round(float(e.mean()), 3), 'p99': round(float(np.percentile(e, 99)), 2), 'p999': round(float(np.percentile(e, 99.9)), 2), 'max': round(float(e.max()), 1)}
st = {'composite_vs_keyart': stats(comp, K), 'full8_vs_keyart': stats(full8, K), 'composite_vs_full8': stats(comp, full8),
      'composite_vs_keyart_sprite_area': stats(comp[sl], K[sl]), 'full8_vs_keyart_sprite_area': stats(full8[sl], K[sl])}
print(json.dumps(st, indent=1))
pad = 40
cs = (slice(y0 - pad if y0 >= pad else 0, y0 + size + pad), slice(x0 - pad, x0 + size + pad))
diff = np.clip(np.abs(comp[cs] - K[cs]) * 8, 0, 1)
strip = np.concatenate([K[cs], comp[cs], diff], axis=1)
Image.fromarray(np.round(strip * 255).astype(np.uint8)).save(d + '/compare.png')
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()
info = {'source': [W, H], 'sprite': {'x': x0, 'y': y0, 'size': size}, 'sphere_center': [cx, cy], 'sphere_radius': R,
        'plate_jpeg_quality': best[0], 'plate_bytes': len(best[1]), 'sprite_bytes': len(open(d + '/sphere.png', 'rb').read()),
        'sha256': {'plate.jpg': sha(d + '/plate.jpg'), 'sphere.png': sha(d + '/sphere.png')}, 'stats': st}
json.dump(info, open(d + '/layers.json', 'w'), indent=1)
print(json.dumps({k: v for k, v in info.items() if k != 'stats'}, indent=1))
