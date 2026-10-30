#!/usr/bin/env python3
"""Product renders of case_v11 in Blender 4.x.

Run:  blender -b -P render_blender.py -- --out renders/

All geometry is millimetres in the physical STL frame
(x = KiCad x, y = -KiCad y, z up from the skin face). The real fix_v24
board comes from _work/board_v24_pads.glb; glTF import lands it in the
same physical frame (metres -> x1000), seated at the design seat z=3.4.
"""
import bpy, sys, os, math, argparse

ap = argparse.ArgumentParser()
ap.add_argument('--out', default='renders')
ap.add_argument('--engine', default='BLENDER_EEVEE_NEXT')
ap.add_argument('--samples', type=int, default=48)
ap.add_argument('--only', default='')
A = ap.parse_args(sys.argv[sys.argv.index('--') + 1:]
                if '--' in sys.argv else [])
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, A.out)
os.makedirs(OUT, exist_ok=True)

# ----------------------------------------------------------------------
# clean scene + units
# ----------------------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
sc.render.engine = A.engine
if A.engine == 'CYCLES':
    sc.cycles.samples = A.samples
    sc.cycles.use_denoising = True
    try:
        sc.cycles.device = 'GPU'
    except Exception:
        pass
sc.render.resolution_x, sc.render.resolution_y = 1920, 1200
sc.render.image_settings.file_format = 'PNG'
sc.render.film_transparent = False
sc.view_settings.look = 'AgX - Medium High Contrast'

# ----------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------
def import_stl(name, loc=(0, 0, 0), rotz=0.0, flip_z=False):
    before = set(bpy.data.objects)
    bpy.ops.wm.stl_import(filepath=os.path.join(HERE, name + '.stl'))
    ob = [o for o in bpy.data.objects if o not in before]
    if len(ob) != 1:
        grp = bpy.data.objects.new(name, None)
        bpy.context.collection.objects.link(grp)
        for o in ob:
            o.parent = grp
        ob = [grp]
    o = ob[0]
    o.name = name
    if flip_z:
        o.scale.z = -1
    o.location = loc
    o.rotation_euler[2] = rotz
    return o

def mat(name, color, rough=0.55, metallic=0.0, alpha=1.0,
        transmission=0.0, coat=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get('Principled BSDF')
    b.inputs['Base Color'].default_value = (*color, 1)
    b.inputs['Roughness'].default_value = rough
    b.inputs['Metallic'].default_value = metallic
    if 'Coat Weight' in b.inputs:
        b.inputs['Coat Weight'].default_value = coat
    if transmission:
        b.inputs['Transmission Weight'].default_value = transmission
        b.inputs['IOR'].default_value = 1.45
    if alpha < 1.0:
        b.inputs['Alpha'].default_value = alpha
        m.surface_render_method = 'DITHERED'
    return m

def set_mat(ob, m, recursive=True):
    if ob.type == 'MESH':
        ob.data.materials.clear()
        ob.data.materials.append(m)
    if recursive:
        for c in ob.children:
            set_mat(c, m, True)

def box(name, loc, dims, bevel=0.0, material=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.name = name
    o.dimensions = dims
    bpy.ops.object.transform_apply(scale=True)
    if bevel:
        mod = o.modifiers.new('bev', 'BEVEL')
        mod.width, mod.segments = bevel, 3
    if material:
        set_mat(o, material)
    return o

# ----------------------------------------------------------------------
# materials (muted product palette)
# ----------------------------------------------------------------------
M_BASE   = mat('base',  (0.74, 0.72, 0.68), 0.55)
M_TOP    = mat('top',   (0.80, 0.76, 0.69), 0.5)
M_CUP    = mat('cup',   (0.66, 0.59, 0.50), 0.55)
M_PLATE  = mat('plate', (0.55, 0.50, 0.45), 0.55)
M_DOOR   = mat('door',  (0.62, 0.56, 0.48), 0.55)
M_BOARD  = mat('board', (0.07, 0.22, 0.13), 0.5)
M_GASKET = mat('gasket',(0.15, 0.55, 0.50), 0.7, alpha=0.85)
M_GOLD   = mat('pogos', (0.75, 0.58, 0.18), 0.35, metallic=0.85)
M_SLUG   = mat('slug',  (0.80, 0.78, 0.74), 0.3, metallic=0.9)
M_CORAL  = mat('tpu',   (0.78, 0.30, 0.28), 0.65)
M_PIPE   = mat('pipe',  (0.9, 0.92, 0.95), 0.15, transmission=0.85)
M_PLUG   = mat('plug',  (0.45, 0.45, 0.47), 0.6)
M_CELL   = mat('cell',  (0.20, 0.20, 0.24), 0.5)
M_STRAP  = mat('strap', (0.04, 0.04, 0.06), 0.9)
M_TORSO  = mat('torso', (0.30, 0.19, 0.12), 0.85)

# ----------------------------------------------------------------------
# import parts (STL already in physical frame, mm)
# ----------------------------------------------------------------------
ZLID = 10.15
base   = import_stl('case_base');     set_mat(base, M_BASE)
top    = import_stl('case_top');      set_mat(top, M_TOP)
cup    = import_stl('bay_cup',  (0, 0, ZLID - 0.3));  set_mat(cup, M_CUP)
door   = import_stl('battery_door', (0, 0, ZLID - 0.3)); set_mat(door, M_DOOR)
plate  = import_stl('pin_plate', (0, 0, ZLID + 1.1), flip_z=True); set_mat(plate, M_PLATE)
gasket = import_stl('gasket');        set_mat(gasket, M_GASKET)
flap   = import_stl('usb_flap');      set_mat(flap, M_CORAL)
cap1   = import_stl('button_cap', (45.30024, -34.61124, 7.05)); set_mat(cap1, M_CORAL)
cap2   = import_stl('button_cap', (2.02538, -57.16984, 7.05));  set_mat(cap2, M_CORAL)
pipe   = import_stl('light_pipe', (4.0, -42.9, 6.1));   set_mat(pipe, M_PIPE)
plug   = import_stl('plug', (26.45, -53.4, ZLID - 0.55)); set_mat(plug, M_PLUG)
cont   = import_stl('contacts_assy'); set_mat(cont, M_GOLD)

# real fix_v24 board (glTF metres -> mm; import already lands in the
# physical frame: blender x=glb x, y=-glb z, z=glb height)
before = set(bpy.data.objects)
bpy.ops.import_scene.gltf(filepath=os.path.join(HERE, '_work', 'board_v24_pads.glb'))
board_objs = [o for o in bpy.data.objects if o not in before]
grp = bpy.data.objects.new('board', None)
bpy.context.collection.objects.link(grp)
for o in board_objs:
    o.parent = grp
grp.scale = (1000, 1000, 1000)
grp.location.z = 3.4
bpy.context.view_layer.update()
# keep the GLB's own materials (KiCad export has real component
# colours); recolour only the full-outline board body pieces to green
import mathutils
def world_xy(o):
    wb = [o.matrix_world @ mathutils.Vector(c) for c in o.bound_box]
    return min(v.x for v in wb), max(v.x for v in wb), \
           min(v.y for v in wb), max(v.y for v in wb)
ext = [world_xy(o) for o in board_objs if o.type == 'MESH']
gx0 = min(e[0] for e in ext); gx1 = max(e[1] for e in ext)
gy0 = min(e[2] for e in ext); gy1 = max(e[3] for e in ext)
for o, e in zip([o for o in board_objs if o.type == 'MESH'], ext):
    if (e[1] - e[0]) > 0.8 * (gx1 - gx0) and \
       (e[3] - e[2]) > 0.8 * (gy1 - gy0):
        set_mat(o, M_BOARD)
board = grp

cell = box('cell', (17.3, -22.5, ZLID + 1.1 + 2.0), (24.3, 35.0, 4.0),
           bevel=1.5, material=M_CELL)

# ----------------------------------------------------------------------
# mm -> m: parent every scene object to a 0.001 root so lights/camera use
# normal metre-scale energies
# ----------------------------------------------------------------------
root = bpy.data.objects.new('root', None)
bpy.context.collection.objects.link(root)
root.scale = (0.001, 0.001, 0.001)
for o in (base, top, cup, door, plate, gasket, flap, cap1, cap2,
          pipe, plug, cont, board, cell):
    o.parent = root

# ----------------------------------------------------------------------
# lighting + world + ground
# ----------------------------------------------------------------------
def add_area(name, loc, energy, size, color):
    d = bpy.data.lights.new(name, 'AREA')
    d.energy, d.shape, d.size = energy, 'DISK', size
    d.color = color
    o = bpy.data.objects.new(name, d)
    bpy.context.collection.objects.link(o)
    o.location = loc
    return o

def point_at(ob, target):
    ob.rotation_euler = (math.radians(90), 0, 0)
    d = ob.data if ob.type != 'EMPTY' else None
    if ob.type == 'LIGHT' or ob.type == 'CAMERA' or ob.data is not None:
        pass
    # generic: aim local -Z at target (lights + camera convention)
    import mathutils
    q = (mathutils.Vector(target) - ob.location).to_track_quat('-Z', 'Y')
    ob.rotation_euler = q.to_euler()

world = bpy.data.worlds.new('w')
sc.world = world
world.use_nodes = True
bg = world.node_tree.nodes['Background']
bg.inputs[0].default_value = (0.24, 0.25, 0.27, 1)
bg.inputs[1].default_value = 0.03

key = add_area('key', (0.06, 0.06, 0.16), 0.5, 0.09, (1.0, 0.96, 0.90))
fill = add_area('fill', (-0.08, 0.02, 0.09), 0.25, 0.07, (0.85, 0.92, 1.0))
rim = add_area('rim', (0.02, -0.14, 0.12), 0.6, 0.06, (1.0, 0.9, 0.8))
uplight = add_area('uplight', (0.02, -0.02, -0.16), 0.8, 0.10,
                   (0.9, 0.95, 1.0))
uplight.hide_render = True
for L in (key, fill, rim):
    point_at(L, (0.023, -0.025, 0))
point_at(uplight, (0.023, -0.025, 0.004))

# ground cyclorama in metre scale
M_GROUND = mat('ground', (0.62, 0.63, 0.65), 0.9)
ground = box('ground', (0.025, -0.025, -0.00055), (0.4, 0.4, 0.001),
             material=M_GROUND)
ground.visible_shadow = True

cam = bpy.data.cameras.new('cam')
cam_obj = bpy.data.objects.new('cam', cam)
bpy.context.collection.objects.link(cam_obj)
sc.camera = cam_obj
cam.lens = 58

def render(fname, cam_loc, target, lens=58):
    cam_obj.location = cam_loc
    cam.lens = lens
    point_at(cam_obj, target)
    sc.render.filepath = os.path.join(OUT, fname)
    bpy.ops.render.render(write_still=True)
    print('wrote', sc.render.filepath)

# ----------------------------------------------------------------------
# views — positions in metres, physical frame centre ~(0.023,-0.025)
# ----------------------------------------------------------------------
C = (0.023, -0.025, 0.004)
views = {v.strip() for v in A.only.split(',') if v.strip()}
def want(v): return not views or v in views

# -- closed: three-quarter from above, fully assembled
if want('closed'):
    render('closed.png', (0.16, 0.09, 0.18), C, lens=60)

# -- hero: lid lifted, board + pogos visible
if want('hero'):
    saved = {}
    for o, dz in [(top, 28), (cup, 42), (door, 42), (plate, 36),
                  (cell, 50), (cap1, 30), (cap2, 30), (pipe, 30),
                  (plug, 30), (flap, 24)]:
        saved[o] = o.location.z
        o.location.z += dz
    render('hero.png', (0.19, 0.09, 0.19), (0.023, -0.025, 0.012), lens=58)
    for o, z in saved.items():
        o.location.z = z

# -- exploded: full vertical stack
if want('exploded'):
    saved = {}
    for o, dz in [(top, 24), (cup, 44), (door, 54), (plate, 32),
                  (cell, 66), (cap1, 34), (cap2, 34), (pipe, 34),
                  (plug, 34), (flap, 26), (board, 11), (cont, 4),
                  (gasket, 2)]:
        saved[o] = o.location.z
        o.location.z += dz
    render('exploded.png', (0.22, 0.10, 0.20), (0.023, -0.025, 0.026), lens=55)
    for o, z in saved.items():
        o.location.z = z

# -- skin side: from below (pogos, gasket, slug, apertures)
if want('skin_side'):
    ground.hide_render = True
    uplight.hide_render = False
    render('skin_side.png', (0.13, 0.06, -0.13), (0.023, -0.025, 0), lens=58)
    ground.hide_render = False
    uplight.hide_render = True

# -- worn: to-scale torso block + strap tails (built in mm under root)
if want('worn'):
    ground.hide_render = True
    torso = box('torso', (23, -25, -46), (240, 170, 90), bevel=35,
                material=M_TORSO)
    sL = box('strapL', (-44, -42, 10), (72, 36, 2.0), bevel=0.6,
             material=M_STRAP)
    sL.rotation_euler[1] = math.radians(-24)
    sR = box('strapR', (90, -42, 10), (72, 36, 2.0), bevel=0.6,
             material=M_STRAP)
    sR.rotation_euler[1] = math.radians(24)
    for o in (torso, sL, sR):
        o.parent = root
    render('worn.png', (0.16, -0.15, 0.09), (0.023, -0.025, -0.012), lens=52)
    for o in (torso, sL, sR):
        bpy.data.objects.remove(o)
    ground.hide_render = False

print('done')
