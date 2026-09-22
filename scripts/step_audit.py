"""Read-only STEP inventory using OCCT; keeps every solid, including fragments."""
import argparse
import collections
import hashlib
import json
from pathlib import Path

import OCP
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepGProp import BRepGProp
from OCP.Bnd import Bnd_Box
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_Reader
from OCP.TColStd import TColStd_SequenceOfAsciiString
from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.TopoDS import TopoDS


def mass(shape):
    properties = GProp_GProps()
    BRepGProp.VolumeProperties_s(shape, properties)
    return properties.Mass()


def audit(path):
    path = Path(path)
    reader = STEPControl_Reader()
    if reader.ReadFile(str(path)) != IFSelect_RetDone or not reader.TransferRoots():
        raise ValueError('STEP import failed')
    shape = reader.OneShape()
    lengths, angles, solids_units = [TColStd_SequenceOfAsciiString() for _ in range(3)]
    reader.FileUnits(lengths, angles, solids_units)
    box = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape, box)
    report = dict(format='ark-source-audit', version=1, source_name=path.name,
                  source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  ocp_version=OCP.__version__, source_length_units=[lengths.Value(i).ToCString() for i in range(1,lengths.Length()+1)],
                  brep_valid=BRepCheck_Analyzer(shape).IsValid(),
                  volume_mm3=mass(shape), bounds_mm=list(box.Get()), solids=[], face_types={},
                  source_geometry_modified=False, reconstruction_verified=False,
                  manufacturing_authoritative=False)
    explorer = TopExp_Explorer(shape, TopAbs_SOLID)
    while explorer.More():
        solid = explorer.Current()
        faces = TopExp_Explorer(solid, TopAbs_FACE)
        counts = collections.Counter()
        while faces.More():
            kind = str(BRepAdaptor_Surface(TopoDS.Face_s(faces.Current())).GetType()).split('.')[-1]
            counts[kind] += 1
            faces.Next()
        bounds = Bnd_Box()
        BRepBndLib.AddOptimal_s(solid, bounds)
        report['solids'].append(dict(index=len(report['solids'])+1, volume_mm3=mass(solid),
                                     brep_valid=BRepCheck_Analyzer(solid).IsValid(),
                                     bounds_mm=list(bounds.Get()), face_types=dict(counts)))
        explorer.Next()
    faces = TopExp_Explorer(shape, TopAbs_FACE)
    counts = collections.Counter()
    while faces.More():
        counts[str(BRepAdaptor_Surface(TopoDS.Face_s(faces.Current())).GetType()).split('.')[-1]] += 1
        faces.Next()
    report['face_types'] = dict(counts)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('report', type=Path)
    args = parser.parse_args()
    result = audit(args.source)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
