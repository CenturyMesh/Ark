"""Export an ARK ideal notch plan using OCCT; never certify a production process.

Requires cadquery-ocp (OCCT C++ Python bindings). All notch decisions are supplied
by the C++ planner; this bridge constructs and independently checks the B-rep.
"""
import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path

from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Common
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakePrism
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_AsIs, STEPControl_Reader, STEPControl_Writer
from OCP.TopAbs import TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.gp import gp_Pnt, gp_Vec


def volume(shape):
    properties = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, properties)
    return properties.Mass()


def solid_count(shape):
    explorer = TopExp_Explorer(shape, TopAbs_SOLID)
    count = 0
    while explorer.More():
        count += 1
        explorer.Next()
    return count


def cut(shape, tool):
    operation = BRepAlgoAPI_Cut(shape, tool)
    operation.Build()
    if not operation.IsDone() or operation.Shape().IsNull():
        raise ValueError('OCCT boolean failed')
    return operation.Shape()


def export(plan, destination):
    if (plan.get('format') != 'ark-cut-plan' or plan.get('version') != 1 or
            plan.get('model') != 'ideal_zero_radius_hinge_preview' or
            plan.get('manufacturing_authoritative') is not False):
        raise ValueError('Expected an explicitly non-authoritative ARK preview plan')
    if not isinstance(plan.get('notches'), list) or not 1 <= len(plan['notches']) <= 64:
        raise ValueError('Expected 1..64 notches')
    stock = plan['stock']
    width, height, wall, length, hinge = [float(stock[key]) for key in (
        'width_mm', 'height_mm', 'wall_mm', 'length_mm', 'hinge_offset_mm')]
    if not (all(math.isfinite(x) for x in (width, height, wall, length, hinge)) and
            10000 >= width > 2 * wall > 0 and 10000 >= height > 2 * wall and 0 < length <= 1e6 and 0 <= hinge <= wall):
        raise ValueError('Invalid stock dimensions')
    shape = cut(BRepPrimAPI_MakeBox(length, height, width).Shape(),
                BRepPrimAPI_MakeBox(gp_Pnt(-1, wall, wall), length + 2,
                                    height - 2 * wall, width - 2 * wall).Shape())
    stock_volume = volume(shape)
    removed = 0.
    envelopes = []
    for notch in plan['notches']:
        face = notch['hinge_face']
        if face not in ('bottom', 'top', 'left', 'right'):
            raise ValueError('Unknown hinge face')
        depth, breadth = ((height, width) if face in ('bottom', 'top') else (width, height))
        angle, station = float(notch['angle_deg']), float(notch['station_mm'])
        if not (math.isfinite(angle) and 0 < angle < 170 and math.isfinite(station)):
            raise ValueError('Invalid notch angle/station')
        slope = math.tan(math.radians(angle) / 2)
        opening = (depth - hinge) * slope
        lo, hi = station - opening, station + opening
        if lo <= 0 or hi >= length or any(lo <= b and hi >= a for a, b in envelopes):
            raise ValueError('Notch envelopes overlap or reach an end')
        envelopes.append((lo, hi))
        def place(point):
            x, y, z = point
            return {'bottom': (x, y, z), 'top': (x, height-y, width-z),
                    'left': (x, height-z, y), 'right': (x, z, width-y)}[face]
        lower, upper = (wall-hinge)*slope, (depth+1-hinge)*slope
        expected_polygon = [place(p) for p in ((station-lower, wall, -1),
            (station-upper, depth+1, -1), (station+upper, depth+1, -1), (station+lower, wall, -1))]
        if len(notch['cutter_polygon']) != 4 or any(len(p) != 3 for p in notch['cutter_polygon']):
            raise ValueError('Invalid cutter polygon')
        if not all(math.isfinite(x) for p in notch['cutter_polygon'] for x in p):
            raise ValueError('Non-finite cutter coordinate')
        if max(abs(x-y) for p, q in zip(expected_polygon, notch['cutter_polygon']) for x, y in zip(p, q)) > 1e-8:
            raise ValueError('Cutter polygon disagrees with the independent notch equations')
        origin, end = place((0, 0, 0)), place((0, 0, breadth+2))
        expected_direction = [b-a for a,b in zip(origin,end)]
        if not all(math.isfinite(x) for x in notch['cutter_extrusion']):
            raise ValueError('Non-finite extrusion')
        if len(notch['cutter_extrusion']) != 3 or max(abs(x-y) for x,y in zip(expected_direction,notch['cutter_extrusion'])) > 1e-8:
            raise ValueError('Cutter extrusion disagrees with the selected hinge face')
        wire = BRepBuilderAPI_MakePolygon()
        for point in notch['cutter_polygon']:
            wire.Add(gp_Pnt(*point))
        wire.Close()
        cutter = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),
                                       gp_Vec(*notch['cutter_extrusion'])).Shape()
        shape = cut(shape, cutter)
        # Independent analytic integral: side wall strips, then opposite wall.
        depth, breadth = ((height, width) if notch['hinge_face'] in ('bottom', 'top')
                          else (width, height))
        slope = math.tan(math.radians(notch['angle_deg']) / 2)
        def integral(lo, hi, breadth_at_depth):
            return slope * breadth_at_depth * ((hi - hinge) ** 2 - (lo - hinge) ** 2)
        removed += integral(wall, depth - wall, 2 * wall) + integral(depth - wall, depth, breadth)
    hinge_errors = []
    for notch, (lo,hi) in zip(plan['notches'], envelopes):
        face = notch['hinge_face']
        ymin, ymax = (0, wall) if face == 'bottom' else ((height-wall, height) if face == 'top' else (0,height))
        zmin, zmax = (0, wall) if face == 'left' else ((width-wall, width) if face == 'right' else (0,width))
        region = BRepPrimAPI_MakeBox(gp_Pnt(lo,ymin,zmin),hi-lo,ymax-ymin,zmax-zmin).Shape()
        common = BRepAlgoAPI_Common(shape,region)
        if not common.IsDone():
            raise ValueError('Retained-wall verification failed')
        hinge_errors.append(abs(volume(common.Shape())-(hi-lo)*(ymax-ymin)*(zmax-zmin)))
    expected = stock_volume - removed
    actual = volume(shape)
    tolerance = max(1e-6, expected * 1e-9)
    if not BRepCheck_Analyzer(shape).IsValid() or solid_count(shape) != 1:
        raise ValueError('The cut blank must be one valid connected solid')
    if max(hinge_errors, default=0) > tolerance:
        raise ValueError("A retained hinge wall lost material")
    if abs(expected - actual) > tolerance:
        raise ValueError('B-rep volume does not agree with the analytical cut integral')
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix('.pending.step')
    writer = STEPControl_Writer()
    if writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone or writer.Write(str(temporary)) != IFSelect_RetDone:
        raise ValueError('STEP export failed')
    reader = STEPControl_Reader()
    if reader.ReadFile(str(temporary)) != IFSelect_RetDone or not reader.TransferRoots():
        raise ValueError('STEP readback failed')
    readback = reader.OneShape()
    difference_volume = abs(volume(cut(shape, readback))) + abs(volume(cut(readback, shape)))
    if (not BRepCheck_Analyzer(readback).IsValid() or solid_count(readback) != 1 or
            abs(volume(readback) - actual) > tolerance or difference_volume > tolerance):
        raise ValueError('STEP roundtrip changed the solid')
    report = dict(format='ark-step-validation', version=1, brep_valid=True, solids=1,
                  stock_volume_mm3=stock_volume, expected_removed_volume_mm3=removed,
                  retained_wall_volume_max_error_mm3=max(hinge_errors, default=0),
                  measured_remaining_volume_mm3=actual, volume_error_mm3=abs(expected-actual),
                  roundtrip_symmetric_difference_volume_mm3=difference_volume,
                  volume_tolerance_mm3=tolerance, manufacturing_authoritative=False,
                  source_reconstruction_verified=False, finite_wall_deformation_verified=False,
                  model='ideal_zero_radius_hinge_preview')
    temporary.replace(destination)
    report['step_sha256'] = hashlib.sha256(destination.read_bytes()).hexdigest()
    destination.with_suffix('.validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('plan', type=Path)
    parser.add_argument('step', type=Path)
    parser.add_argument('--engine', type=Path, help='Treat input as request and generate plan through this ARK MCP executable')
    args = parser.parse_args()
    plan = json.loads(args.plan.read_text(encoding='utf-8-sig'))
    if args.engine:
        request = dict(jsonrpc='2.0', id=1, method='tools/call', params=dict(name='notch_plan', arguments=plan))
        process = subprocess.run([str(args.engine.resolve())], input=json.dumps(request)+'\n', text=True, capture_output=True, check=True, timeout=30)
        result = json.loads(process.stdout)['result']
        if result['isError']:
            raise ValueError(result['structuredContent'])
        plan = result['structuredContent']
        args.step.parent.mkdir(parents=True, exist_ok=True)
        args.step.with_suffix('.arkcut').write_text(json.dumps(plan, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(export(plan, args.step), indent=2))
