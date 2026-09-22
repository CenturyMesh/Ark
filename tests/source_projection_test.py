"""Independent synthetic B-rep + rigid metamorphic source-projection tests.

Requires optional OCCT environment. No customer fixture or reference blank is
used to select production geometry.
"""
import math
from pathlib import Path
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from source_tube import unfold
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakePolygon, BRepBuilderAPI_MakeFace, BRepBuilderAPI_Transform
from OCP.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder, BRepPrimAPI_MakePrism
from OCP.STEPControl import STEPControl_Writer, STEPControl_AsIs
from OCP.gp import gp_Pnt,gp_Vec,gp_Dir,gp_Ax1,gp_Ax2,gp_Trsf


def cut(a,b):return BRepAlgoAPI_Cut(a,b).Shape()
def fuse(a,b):return BRepAlgoAPI_Fuse(a,b).Shape()
def moved(shape,m):return BRepBuilderAPI_Transform(shape,m,True).Shape()
def shift(vector):
    m=gp_Trsf();m.SetTranslation(gp_Vec(*vector));return m


def fixture():
    width,height,wall=29.,38.,1.8
    inner=3.25;neutral=inner+wall/2;outer=inner+wall;angle=math.radians(70)
    before,after,relief=480.,420.,45.;arc=neutral*angle;h=wall/2;end=before+arc
    def tube(length):return cut(BRepPrimAPI_MakeBox(length,height,width).Shape(),BRepPrimAPI_MakeBox(gp_Pnt(-1,wall,wall),length+2,height-2*wall,width-2*wall).Shape())
    left=cut(tube(before),BRepPrimAPI_MakeBox(gp_Pnt(before-relief,wall,-1),relief+1,height,width+2).Shape())
    right=cut(tube(after),BRepPrimAPI_MakeBox(gp_Pnt(-1,wall,-1),relief+1,height,width+2).Shape())
    # Exact circular hole and polygonal hole on both broad tube walls.
    left=cut(left,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100,height/2,-1),gp_Dir(0,0,1)),3,width+2).Shape())
    polygon=BRepBuilderAPI_MakePolygon()
    for k in range(6):polygon.Add(gp_Pnt(210+4*math.cos(k*math.pi/3),height/2+4*math.sin(k*math.pi/3),-1))
    polygon.Close();left=cut(left,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(0,0,width+2)).Shape())
    right=cut(right,BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(170,height/2,-1),gp_Dir(0,0,1)),2.4,width+2).Shape())
    # Independent partial-cylinder Boolean construction of the curved wall.
    position=gp_Ax2(gp_Pnt(before,h+neutral,0),gp_Dir(0,0,1),gp_Dir(0,-1,0))
    sector=BRepPrimAPI_MakeCylinder(position,outer,width,angle).Shape()
    hole=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(before,h+neutral,-1),gp_Dir(0,0,1)),inner,width+2).Shape()
    hinge=cut(sector,hole)
    rotation=gp_Trsf();rotation.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(0,0,1)),angle)
    endpoint=(before+neutral*math.sin(angle),h+neutral*(1-math.cos(angle)),0)
    placement=shift(endpoint).Multiplied(rotation).Multiplied(shift((0,-h,0)))
    source=fuse(fuse(left,hinge),moved(right,placement))
    # A hole on the curved ligament must fail closed, not disappear in unfolding.
    radial=(math.sin(angle/2),-math.cos(angle/2),0)
    drill_origin=gp_Pnt(before+(inner-1)*radial[0],h+neutral+(inner-1)*radial[1],width/2)
    damaged=cut(source,BRepPrimAPI_MakeCylinder(gp_Ax2(drill_origin,gp_Dir(*radial)),1.1,wall+2).Shape())
    return source,damaged,before+after+arc


def write(shape,path):
    writer=STEPControl_Writer();writer.Transfer(shape,STEPControl_AsIs);writer.Write(str(path))


with tempfile.TemporaryDirectory(prefix='ark-source-test-') as folder:
    directory=Path(folder);source,damaged,length=fixture()
    base=directory/'source.step';write(source,base);first=unfold(base,directory/'base')
    report=first['report']
    assert report['opening_count']==6 and report['opening_kinds']=={'circular':4,'polygonal':2},report
    assert abs(report['flat_bounds_mm'][3]-report['flat_bounds_mm'][0]-length)<1e-5
    assert report['source_projection_verified'] and report['refold_symmetric_difference_volume_mm3']<report['volume_tolerance_mm3']
    rigid=gp_Trsf();rigid.SetRotation(gp_Ax1(gp_Pnt(),gp_Dir(1,2,3)),.47);rigid.SetTranslationPart(gp_Vec(123,-75,210))
    path=directory/'rigid-transform.step';write(moved(source,rigid),path);second=unfold(path,directory/'transformed')
    assert second['report']['opening_count']==6
    assert abs(second['report']['flat_volume_mm3']-report['flat_volume_mm3'])<report['volume_tolerance_mm3']
    one=sorted((h['kind'],tuple(round(v,5) for v in h['flat_center'])) for h in first['holes'])
    two=sorted((h['kind'],tuple(round(v,5) for v in h['flat_center'])) for h in second['holes'])
    assert one==two,(one,two)
    path=directory/'unsupported-hinge-hole.step';write(damaged,path)
    try:unfold(path,directory/'unsupported')
    except ValueError as error:assert 'hinge' in str(error).lower() or 'chain' in str(error).lower(),error
    else:raise AssertionError('A cut through the deforming hinge was silently flattened')
print('Synthetic circular/polygon holes, rigid metamorphic projection and unsupported hinge-hole rejection passed')
