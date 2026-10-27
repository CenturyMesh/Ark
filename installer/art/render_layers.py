"""Renders the MeshSphere key-art layers for ARK Setup from a COPY of the scene (nothing is saved back).
The scene is the project owner's "MeshSphere Cinematic v5" Blender file; it is not part of this repository.

    blender -b <copy.blend> --python render_layers.py -- <out_dir>
    py -3 -I make_layers.py <out_dir> [reference.jpg]     -> plate.jpg, sphere.png, layers.json (sprite rectangle)

Uses the scene's own settings (EEVEE, samples, AgX, DOF, motion blur, Fog Glow compositor) at frame 175:
  full16.png / full8.png    the whole frame (16-bit for maths; 8-bit with Blender's dither, as the original frames)
  plate16.png / plate8.png  the same frame with the sphere and wordmark unlinked
  matte16.png               coverage of the sphere alone (transparent film, no compositor)
and writes A.npy, B.npy, m.npy (float32, top-down) plus render.json with the timings and settings.
"""
import bpy, sys, os, time, json
import numpy as np

OUT = os.path.abspath(sys.argv[sys.argv.index('--') + 1])
os.makedirs(OUT, exist_ok=True)
FRAME = 175
s = bpy.context.scene
s.frame_set(FRAME)
root = bpy.data.objects['MASTER • continuous solid transformation']
words = bpy.data.objects['WORDMARK • only at the end']
sphere = list(root.children)
wordmark = list(words.children)
others = [o for o in s.objects if o not in sphere and o not in wordmark and o.type in ('MESH', 'FONT', 'CURVE')]
coll = bpy.data.collections['Collection']

def unlink(objs):
    for o in objs:
        for c in list(o.users_collection):
            c.objects.unlink(o)

def link(objs):
    for o in objs:
        if coll not in o.users_collection:
            coll.objects.link(o)

img = s.render.image_settings
img.file_format = 'PNG'; img.compression = 15
timings = {}

def render(name, depth='16', rgba=False):
    img.color_depth = depth
    img.color_mode = 'RGBA' if rgba else 'RGB'
    s.render.filepath = os.path.join(OUT, name)
    t = time.perf_counter()
    bpy.ops.render.render(write_still=True)
    timings[name] = round(time.perf_counter() - t, 2)
    print('RENDERED', name, timings[name], 's')

t0 = time.perf_counter()
render('full16.png'); render('full8.png', '8')
unlink(sphere + wordmark)
render('plate16.png'); render('plate8.png', '8')
link(sphere)
unlink(others)
s.render.film_transparent = True
s.render.use_compositing = False
render('matte16.png', rgba=True)
total = round(time.perf_counter() - t0, 2)

def load(name):
    im = bpy.data.images.load(os.path.join(OUT, name))
    im.colorspace_settings.name = 'Non-Color'
    w, h = im.size
    buf = np.empty(w * h * 4, dtype=np.float32)
    im.pixels.foreach_get(buf)
    return buf.reshape(h, w, 4)[::-1].copy()

np.save(os.path.join(OUT, 'A.npy'), load('full16.png')[..., :3])
np.save(os.path.join(OUT, 'B.npy'), load('plate16.png')[..., :3])
np.save(os.path.join(OUT, 'm.npy'), load('matte16.png')[..., 3])
gpu = ''
try:
    gpu = bpy.context.preferences.addons['cycles'].preferences.compute_device_type
except Exception:
    pass
info = {'blender': bpy.app.version_string, 'frame': FRAME, 'engine': s.render.engine, 'samples': s.eevee.taa_render_samples,
        'resolution': [s.render.resolution_x, s.render.resolution_y], 'view_transform': s.view_settings.view_transform,
        'motion_blur_shutter': round(s.render.motion_blur_shutter, 3), 'dof_fstop': s.camera.data.dof.aperture_fstop,
        'dither_intensity': s.render.dither_intensity, 'compositor': 'Fog Glow (scene default)',
        'timings_s': timings, 'total_render_s': total}
json.dump(info, open(os.path.join(OUT, 'render.json'), 'w'), indent=1)
print('INFO', json.dumps(info))
