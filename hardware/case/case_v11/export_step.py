#!/usr/bin/env python3
"""Export the case_v11 parts (assembled pose) as a STEP file.

No CAD kernel is available in this environment, so this writes a valid
ISO-10303-21 (STEP) file using FACETED_BREP solids — one per part — inside a
single product's ADVANCED_BREP_SHAPE_REPRESENTATION. FACETED_BREP imports in
FreeCAD / SolidWorks / KiCad as a valid (faceted) solid, preserving names.

v11: case STLs are exported in the PHYSICAL frame (x=kicad x, y=-kicad y,
z up from skin). The board solid is the real fix_v24 GLB slab (same frame);
the small parts' hardcoded seat positions are mirrored y -> -y.
"""
import os
import numpy as np
import trimesh
from case_lib import ZLID, board_solid, phys

SLAB_GLB = '_work/board_v24_slab.glb'

def real_board():
    """Real fix_v24 board slab in the physical frame at the seat z=3.4.
    GLB frame: (x=kicad x, y=height above B.Cu, z=kicad y) metres."""
    try:
        g = trimesh.load(SLAB_GLB).to_geometry()
        m = trimesh.Trimesh(np.c_[g.vertices[:, 0], -g.vertices[:, 2],
                                  g.vertices[:, 1]] * 1000.0, g.faces)
        m.apply_translation([0, 0, 3.4])
        return m
    except Exception:
        return phys(board_solid())

def T(m, dx=0, dy=0, dz=0):
    out = m.copy(); out.apply_translation([dx, dy, dz]); return out

def load_parts():
    parts = [('case_base', trimesh.load('case_base.stl')),
             ('case_top', trimesh.load('case_top.stl')),
             ('board_pcb', real_board()),
             ('bay_cup', T(trimesh.load('bay_cup.stl'), dz=ZLID - 0.3)),
             ('pin_plate', trimesh.load('pin_plate.stl')),
             ('battery_door', T(trimesh.load('battery_door.stl'),
                                dz=ZLID - 0.3)),
             ('gasket_tpu', trimesh.load('gasket.stl')),
             ('usb_dust_flap', trimesh.load('usb_flap.stl')),
             ('button_cap_SW1', T(trimesh.load('button_cap.stl'),
                                  45.30024, -34.61124, 7.05)),
             ('button_cap_SW2', T(trimesh.load('button_cap.stl'),
                                  2.02538, -57.16984, 7.05)),
             ('light_pipe_D25', T(trimesh.load('light_pipe.stl'),
                                  4.0, -42.9, 6.1)),
             ('j8_window_plug', T(trimesh.load('plug.stl'),
                                  26.45, -53.4, ZLID-0.55)),
             ('contacts_assy', trimesh.load('contacts_assy.stl'))]
    # pin plate assembled: flipped, pins down through the cup floor + lid
    plate = dict(parts)['pin_plate']
    plate.apply_transform(
        trimesh.transformations.reflection_matrix([0, 0, 0], [0, 0, 1]))
    plate.apply_translation([0, 0, ZLID+1.1])
    return parts

HDR = """ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('VitalQ case_v11 assembly - faceted solids'),'2;1');
FILE_NAME('case_v11.step','2026-10-14T00:00:00',('Devin'),('Cognition'),
'build_case.py + export_step.py','trimesh STL->FACETED_BREP','');
FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));
ENDSEC;
DATA;
"""

def write_step(parts, fname):
    ids = {}
    n = [100]                                    # next entity id
    def nid():
        n[0] += 1
        return n[0]
    body = []
    brep_ids = []
    for name, mesh in parts:
        if mesh is None:
            continue
        tris = mesh.triangles.reshape(-1, 9)
        pt_ids = {}
        face_ids = []
        for tri in tris:
            loop_pts = []
            for v in tri.reshape(3, 3):
                key = tuple(np.round(v, 5))
                if key not in pt_ids:
                    pid = nid()
                    pt_ids[key] = pid
                    body.append("#%d = CARTESIAN_POINT('',(%.5f,%.5f,%.5f));"
                                % (pid, key[0], key[1], key[2]))
                loop_pts.append(pt_ids[key])
            pid = nid()
            body.append("#%d = POLY_LOOP('',(#%d,#%d,#%d));"
                        % (pid, loop_pts[0], loop_pts[1], loop_pts[2]))
            bid = nid()
            body.append("#%d = FACE_OUTER_BOUND('',#%d,.T.);" % (bid, pid))
            fid = nid()
            body.append("#%d = FACE('',(#%d));" % (fid, bid))
            face_ids.append(fid)
        sid = nid()
        body.append("#%d = CLOSED_SHELL('',(%s));"
                    % (sid, ','.join('#%d' % f for f in face_ids)))
        brid = nid()
        body.append("#%d = FACETED_BREP('%s',#%d);" % (brid, name, sid))
        brep_ids.append(brid)
    ctx = """#10 = APPLICATION_PROTOCOL_DEFINITION('international standard',
  'automotive_design',2000,'');
#11 = APPLICATION_CONTEXT('core data for automotive mechanical design processes');
#12 = PRODUCT_CONTEXT('',#11,'mechanical');
#13 = PRODUCT('case_v11','VitalQ case_v11 assembly','',(#12));
#14 = PRODUCT_DEFINITION_FORMATION('','assembled case_v11 product',#13);
#15 = PRODUCT_RELATED_PRODUCT_CATEGORY('part','',(#13));
#16 = PRODUCT_DEFINITION_CONTEXT('part definition',#11,'design');
#17 = PRODUCT_DEFINITION('design','',#14,#16);
#18 = PRODUCT_DEFINITION_SHAPE('','geometry for case_v11',#17);
#19 = ( GEOMETRIC_REPRESENTATION_CONTEXT(3)
  GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#20))
  GLOBAL_UNIT_ASSIGNED_CONTEXT((#23,#26,#27))
  REPRESENTATION_CONTEXT('case_v11','3D') );
#20 = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-05),#23,
  'distance_accuracy_value','confusion accuracy');
#23 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );
#26 = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );
#27 = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );
#30 = SHAPE_DEFINITION_REPRESENTATION(#18,#31);
#31 = ADVANCED_BREP_SHAPE_REPRESENTATION('case_v11',(#32,%s),#19);
#32 = AXIS2_PLACEMENT_3D('',#33,#34,#35);
#33 = CARTESIAN_POINT('',(0.,0.,0.));
#34 = DIRECTION('',(0.,0.,1.));
#35 = DIRECTION('',(1.,0.,0.));
""" % (','.join('#%d' % b for b in brep_ids))
    with open(fname, 'w') as f:
        f.write(HDR + ctx + '\n'.join(body) + "\nENDSEC;\nEND-ISO-10303-21;\n")
    print('wrote', fname, 'solids:', len(brep_ids),
          'entities:', n[0])

if __name__ == '__main__':
    parts = load_parts()
    for name, m in parts:
        if m is not None:
            print(f'{name:16s} watertight={m.is_watertight} faces={len(m.faces)}')
    write_step(parts, 'case_v11.step')
