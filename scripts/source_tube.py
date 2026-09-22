"""Source-preserving planar tube unfolding through finite cylindrical hinges.

Supported domain: a chain of rigid tube spans joined by complete rectangular
annular hinge sectors. Hole contours stay on their owning B-rep span. Unknown
hinge trims, ambiguous connectivity and lost source material are rejected.
"""
import argparse
import collections
import csv
import hashlib
import base64
import json
import math
import OCP
from pathlib import Path

from OCP.BRep import BRep_Builder, BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface, BRepAdaptor_Curve
from OCP.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Common, BRepAlgoAPI_Fuse
from OCP.BRepBndLib import BRepBndLib
from OCP.BRepBuilderAPI import BRepBuilderAPI_MakeEdge, BRepBuilderAPI_MakeWire, BRepBuilderAPI_MakeFace, BRepBuilderAPI_MakePolygon, BRepBuilderAPI_Transform, BRepBuilderAPI_MakeVertex
from OCP.BRepCheck import BRepCheck_Analyzer
from OCP.BRepExtrema import BRepExtrema_DistShapeShape
from OCP.BRepGProp import BRepGProp
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.TopLoc import TopLoc_Location
from OCP.BRepTools import BRepTools
from OCP.BRepPrimAPI import BRepPrimAPI_MakePrism
from OCP.Bnd import Bnd_Box
from OCP.GC import GC_MakeArcOfCircle
from OCP.GProp import GProp_GProps
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPControl import STEPControl_Reader, STEPControl_Writer, STEPControl_AsIs
from OCP.TopAbs import TopAbs_FACE, TopAbs_SOLID, TopAbs_WIRE, TopAbs_EDGE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopoDS import TopoDS, TopoDS_Compound
from OCP.gp import gp_Pnt, gp_Vec, gp_Dir, gp_Ax1, gp_Trsf

LINEAR_TOL = 1e-6


def xyz(p): return (p.X(), p.Y(), p.Z())
def add(a,b): return tuple(x+y for x,y in zip(a,b))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def mul(a,s): return tuple(x*s for x in a)
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def cross(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return math.sqrt(dot(a,a))
def unit(a): return mul(a,1/norm(a))


def shapes(shape,kind):
    result=[];e=TopExp_Explorer(shape,kind)
    while e.More(): result.append(e.Current());e.Next()
    return result


def properties(shape,kind='volume'):
    g=GProp_GProps()
    getattr(BRepGProp, {'volume':'VolumeProperties_s','area':'SurfaceProperties_s','length':'LinearProperties_s'}[kind])(shape,g)
    return g.Mass()


def bounds(shape):
    b=Bnd_Box();BRepBndLib.AddOptimal_s(shape,b,False,False);return b.Get()


def boolean(cls,a,b):
    operation=cls(a,b);operation.Build()
    if not operation.IsDone(): raise ValueError('OCCT Boolean operation failed')
    return operation.Shape()


def compound(parts):
    builder=BRep_Builder();out=TopoDS_Compound();builder.MakeCompound(out)
    for part in parts: builder.Add(out,part)
    return out


def transform(shape,m): return BRepBuilderAPI_Transform(shape,m,True).Shape()
def point(m,p): return xyz(gp_Pnt(*p).Transformed(m))
def direction(m,p): return xyz(gp_Vec(*p).Transformed(m))
def translation(p):
    m=gp_Trsf();m.SetTranslation(gp_Vec(*p));return m
def rotation(axis,angle):
    m=gp_Trsf();m.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(*axis)),angle);return m
def matrix(m): return [m.Value(r+1,c+1) if r<3 else (1. if c==3 else 0.) for c in range(4) for r in range(4)]


def sector(center,axis,radial,tangent,angle,inner,outer,width):
    def at(radius,a): return gp_Pnt(*add(center,mul(add(mul(radial,math.cos(a)),mul(tangent,math.sin(a))),radius)))
    edges=[BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(at(outer,0),at(outer,angle/2),at(outer,angle)).Value()).Edge(),
           BRepBuilderAPI_MakeEdge(at(outer,angle),at(inner,angle)).Edge(),
           BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(at(inner,angle),at(inner,angle/2),at(inner,0)).Value()).Edge(),
           BRepBuilderAPI_MakeEdge(at(inner,0),at(outer,0)).Edge()]
    wire=BRepBuilderAPI_MakeWire()
    for e in edges:wire.Add(e)
    return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(),gp_Vec(*mul(axis,width))).Shape()


def rectangle_prism(origin,a,b,c):
    p=BRepBuilderAPI_MakePolygon()
    for x in (origin,add(origin,a),add(add(origin,a),b),add(origin,b)):p.Add(gp_Pnt(*x))
    p.Close();return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(p.Wire()).Face(),gp_Vec(*c)).Shape()


def check_hinge_boundary(cylinder):
    if len(shapes(cylinder['face'],TopAbs_WIRE))!=1:
        raise ValueError('Holed hinge patch requires a richer deformation map')
    for edge in shapes(cylinder['face'],TopAbs_EDGE):
        curve=BRepAdaptor_Curve(TopoDS.Edge_s(edge));kind=str(curve.GetType())
        if 'Circle' in kind:
            circle=curve.Circle();offset=sub(xyz(circle.Location()),cylinder['center']);v=dot(offset,cylinder['axis'])
            if (abs(circle.Radius()-cylinder['r'])>LINEAR_TOL or norm(cross(offset,cylinder['axis']))>LINEAR_TOL or
                min(abs(v),abs(v-cylinder['width']))>LINEAR_TOL or abs(dot(xyz(circle.Axis().Direction()),cylinder['axis']))<1-1e-10):
                raise ValueError('Non-rectangular hinge boundary is unsupported')
        elif 'Line' in kind:
            line=curve.Line();offset=sub(xyz(line.Location()),cylinder['center']);radial=sub(offset,mul(cylinder['axis'],dot(offset,cylinder['axis'])))
            if (abs(dot(xyz(line.Direction()),cylinder['axis']))<1-1e-10 or
                min(norm(sub(radial,mul(cylinder['radial'],cylinder['r']))),norm(sub(radial,mul(cylinder['end'],cylinder['r']))))>LINEAR_TOL):
                raise ValueError('Trimmed hinge boundary is unsupported')
        else:raise ValueError('Unsupported hinge boundary curve type')


def find_hinges(source):
    faces=[TopoDS.Face_s(f) for f in shapes(source,TopAbs_FACE)]
    groups=[]
    for face in faces:
        s=BRepAdaptor_Surface(face)
        if 'Plane' not in str(s.GetType()):continue
        n=xyz(s.Plane().Axis().Direction());area=properties(face,'area')
        match=next((g for g in groups if abs(dot(g['axis'],n))>1-1e-10),None)
        if match:match['area']+=area
        else:groups.append(dict(axis=n,area=area))
    groups.sort(key=lambda g:-g['area'])
    if len(groups)<2 or groups[0]['area']<1.5*groups[1]['area']:
        raise ValueError('No unambiguous dominant planar-wall normal; planar tube domain required')
    normal=groups[0]['axis'];cylinders=[]
    for i,face in enumerate(faces):
        s=BRepAdaptor_Surface(face)
        if 'Cylinder' not in str(s.GetType()):continue
        cy=s.Cylinder();axis=xyz(cy.Axis().Direction())
        if abs(dot(axis,normal))<1-1e-10:continue
        u0,u1,v0,v1=BRepTools.UVBounds_s(face);angle=u1-u0
        if not 1e-5<angle<math.pi-1e-5:continue
        center=add(xyz(cy.Location()),mul(axis,v0));r=cy.Radius()
        radial=unit(sub(xyz(s.Value(u0,v0)),center));end=unit(sub(xyz(s.Value(u1,v0)),center))
        # Normalize the physical axis/end ordering. Inner and outer surfaces can
        # legitimately carry opposite cylinder parameterizations in another CAD.
        if axis[max(range(3),key=lambda k:abs(axis[k]))]<0:
            center=add(center,mul(axis,v1-v0));axis=mul(axis,-1);radial,end=end,radial
        cylinders.append(dict(face=face,id=i+1,r=r,axis=axis,center=center,radial=radial,end=end,angle=angle,width=v1-v0,area=properties(face,'area')))
    hinges=[];used=set()
    for a in sorted(cylinders,key=lambda x:x['r']):
        if a['id'] in used:continue
        candidates=[]
        for b in cylinders:
            thickness=b['r']-a['r']
            if b['id'] in used or thickness<=LINEAR_TOL or a['width']<4*thickness:continue
            if (norm(sub(a['center'],b['center']))<LINEAR_TOL and dot(a['axis'],b['axis'])>1-1e-10 and
                norm(sub(a['radial'],b['radial']))<LINEAR_TOL and abs(a['width']-b['width'])<LINEAR_TOL and abs(a['angle']-b['angle'])<1e-8):candidates.append(b)
        if not candidates:continue
        if len(candidates)!=1:raise ValueError('Ambiguous cylindrical hinge radius pair')
        b=candidates[0]
        for c in (a,b):
            check_hinge_boundary(c)
            expected=c['r']*c['width']*c['angle']
            if abs(c['area']-expected)>max(1e-6,expected*1e-8):raise ValueError('Trimmed/holed hinge patch requires a richer deformation map')
        h=dict(center=a['center'],axis=a['axis'],radial=a['radial'],angle=a['angle'],width=a['width'],inner=a['r'],outer=b['r'],face_ids=[a['id'],b['id']])
        h['tangent']=cross(h['axis'],h['radial']);h['neutral']=(h['inner']+h['outer'])/2
        h['sector']=sector(h['center'],h['axis'],h['radial'],h['tangent'],h['angle'],h['inner'],h['outer'],h['width'])
        h['expected_volume']=h['width']*(h['outer']**2-h['inner']**2)*h['angle']/2
        hinges.append(h);used.update((a['id'],b['id']))
    if not 1<=len(hinges)<=32:raise ValueError('No supported cylindrical hinge chain detected')
    return hinges,faces


def write_openings(path,holes):
    with Path(path).open('w',newline='',encoding='utf-8') as f:
        writer=csv.writer(f);writer.writerow(['opening_id','kind','source_face','rigid_span','source_x_mm','source_y_mm','source_z_mm','flat_x_mm','flat_y_mm','flat_z_mm','radius_mm','perimeter_mm','contour_edges'])
        for hole in holes:writer.writerow([hole['id'],hole['kind'],hole['source_face'],hole['segment']+1,*hole['source_center'],*hole['flat_center'],hole.get('radius',''),hole['perimeter_mm'],hole['contour_edges']])


def mesh_data(shape):
    BRepMesh_IncrementalMesh(shape,.025,False,.12,False).Perform()
    positions=[];indices=[]
    for item in shapes(shape,TopAbs_FACE):
        face=TopoDS.Face_s(item);location=TopLoc_Location();triangulation=BRep_Tool.Triangulation_s(face,location)
        if triangulation is None:raise ValueError('Source face failed display triangulation')
        base=len(positions)//3
        for i in range(1,triangulation.NbNodes()+1):
            p=triangulation.Node(i).Transformed(location.Transformation());positions.extend(round(v,8) for v in xyz(p))
        for i in range(1,triangulation.NbTriangles()+1):
            tri=list(triangulation.Triangle(i).Get())
            if face.Orientation()==TopAbs_REVERSED:tri[1],tri[2]=tri[2],tri[1]
            indices.extend(base+k-1 for k in tri)
    return dict(pos=positions,idx=indices)


def opening_inventory(source_faces,parts,order,transforms,flat):
    flat_box=bounds(flat)
    part_faces=[[TopoDS.Face_s(f) for f in shapes(parts[i],TopAbs_FACE)] for i in order]
    openings=[];max_inverse=0.;max_boundary=0.;max_length_error=0.
    for face_id,face in enumerate(source_faces,1):
        surface=BRepAdaptor_Surface(face)
        if 'Plane' not in str(surface.GetType()):continue
        plane=surface.Plane();outer=BRepTools.OuterWire_s(face)
        wires=[w for w in shapes(face,TopAbs_WIRE) if not w.IsSame(outer)]
        if not wires:continue
        face_owners=[k for k,faces in enumerate(part_faces) if any(face.IsSame(f) for f in faces)]
        for wire in wires:
            owners=face_owners
            if len(owners)!=1:
                # A planar face can span multiple rigid regions. Assign the
                # complete contour by exact common edge length, never centroid.
                perimeter=properties(wire,'length');owners=[]
                for k,i in enumerate(order):
                    common=boolean(BRepAlgoAPI_Common,wire,parts[i])
                    if abs(properties(common,'length')-perimeter)<=LINEAR_TOL:owners.append(k)
            if len(owners)!=1:raise ValueError('Opening contour does not have exactly one rigid source owner')
            owner=owners[0];m=transforms[owner];flat_normal=direction(m,xyz(plane.Axis().Direction()));flat_origin=point(m,xyz(plane.Location()))
            # Count outer wall openings only. Inner mouths and tube end caps
            # remain in source geometry but are not extra drilled openings.
            axis=next((k for k in (1,2) if abs(flat_normal[k])>1-1e-10),None)
            if axis is None or min(abs(flat_origin[axis]-flat_box[axis]),abs(flat_origin[axis]-flat_box[axis+3]))>LINEAR_TOL:continue
            edges=[TopoDS.Edge_s(e) for e in shapes(wire,TopAbs_EDGE)]
            curves=[BRepAdaptor_Curve(e) for e in edges];kinds=[str(c.GetType()) for c in curves]
            perimeter=properties(wire,'length');projected=transform(wire,m)
            length_error=abs(properties(projected,'length')-perimeter);max_length_error=max(max_length_error,length_error)
            sample=[]
            for c in curves:
                for i in range(9):sample.append(xyz(c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*i/8)))
            for v in sample:max_inverse=max(max_inverse,norm(sub(point(m.Inverted(),point(m,v)),v)))
            # Every projected edge must remain on the exported solid boundary.
            for edge in shapes(projected,TopAbs_EDGE):
                c=BRepAdaptor_Curve(TopoDS.Edge_s(edge))
                for i in range(5):
                    q=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*i/4)
                    vertex=BRepBuilderAPI_MakeVertex(q).Vertex();distance=BRepExtrema_DistShapeShape(vertex,flat);distance.Perform();max_boundary=max(max_boundary,distance.Value())
            kind='circular' if all('Circle' in k for k in kinds) else 'polygonal' if all('Line' in k for k in kinds) else 'mixed'
            if kind=='circular':
                circles=[c.Circle() for c in curves];center=xyz(circles[0].Location());radius=circles[0].Radius()
                if any(norm(sub(xyz(c.Location()),center))>LINEAR_TOL or abs(c.Radius()-radius)>LINEAR_TOL for c in circles):kind='mixed'
            if kind!='circular':
                props=GProp_GProps();BRepGProp.LinearProperties_s(wire,props);center=xyz(props.CentreOfMass())
            face_normal=xyz(plane.Axis().Direction())
            if face.Orientation()==TopAbs_REVERSED:face_normal=mul(face_normal,-1)
            record=dict(id=len(openings)+1,kind=kind,source_face=face_id,segment=owner,source_center=center,flat_center=point(m,center),source_normal=face_normal,flat_normal=direction(m,face_normal),perimeter_mm=perimeter,perimeter_error_mm=length_error,contour_edges=len(edges))
            if kind=='circular':record['radius']=radius
            openings.append(record)
    if max_inverse>LINEAR_TOL or max_boundary>LINEAR_TOL or max_length_error>LINEAR_TOL:raise ValueError('Opening contour projection failed')
    return openings,dict(opening_inverse_max_mm=max_inverse,opening_boundary_sample_max_mm=max_boundary,opening_perimeter_max_error_mm=max_length_error,boundary_samples_per_edge=5,inverse_samples_per_edge=9,linear_tolerance_mm=LINEAR_TOL)


def finite_transforms(bends,progress):
    result=[gp_Trsf()]
    for i,b in enumerate(bends):
        phase=max(0.,min(1.,progress*len(bends)-i));angle=b['angle']*phase
        if angle<1e-12:result.append(result[-1]);continue
        radius=b['length']/angle;inward=mul(b['outward'],-1)
        end=add(b['start'],add((radius*math.sin(angle),0,0),mul(inward,radius*(1-math.cos(angle)))))
        flat_end=add(b['start'],(b['length'],0,0))
        joint=translation(end).Multiplied(rotation(b['axis'],angle)).Multiplied(translation(mul(flat_end,-1)))
        result.append(result[-1].Multiplied(joint))
    return result


def unfold(source_path,output_directory):
    source_path=Path(source_path);out=Path(output_directory);out.mkdir(parents=True,exist_ok=True)
    source_bytes=source_path.read_bytes()
    if len(source_bytes)>64*1024*1024:raise ValueError('Source STEP exceeds 64 MiB import limit')
    source_digest=hashlib.sha256(source_bytes).hexdigest();snapshot=out/'source-input.step';snapshot.write_bytes(source_bytes)
    reader=STEPControl_Reader()
    if reader.ReadFile(str(snapshot))!=IFSelect_RetDone:raise ValueError('STEP read failed')
    reader.SetSystemLengthUnit(1.0)  # OCCT system lengths in millimetres
    if not reader.TransferRoots():raise ValueError('STEP transfer failed')
    source=reader.OneShape()
    if not BRepCheck_Analyzer(source).IsValid():raise ValueError('Invalid source B-rep')
    source_solids=sorted(shapes(source,TopAbs_SOLID),key=lambda s:-properties(s));principal=source_solids[0]
    hinges,source_faces=find_hinges(principal)
    print('Hinges:',len(hinges),flush=True)
    remainder=principal
    for h in hinges:
        extracted=boolean(BRepAlgoAPI_Common,principal,h['sector']);measured=properties(extracted)
        if abs(measured-h['expected_volume'])>max(1e-6,h['expected_volume']*1e-8):raise ValueError('Hinge sector does not fully cover the measured material')
        remainder=boolean(BRepAlgoAPI_Cut,remainder,h['sector'])
    parts=shapes(remainder,TopAbs_SOLID)
    print('Rigid source parts:',len(parts),flush=True)
    if len(parts)!=len(hinges)+1:raise ValueError('Hinge subtraction did not produce one chain of rigid spans')
    graph=collections.defaultdict(list)
    for index,h in enumerate(hinges):
        h['ends']=[];h['radials']=[];h['tangents']=[]
        for angle in (0,h['angle']):
            radial=add(mul(h['radial'],math.cos(angle)),mul(h['tangent'],math.sin(angle)))
            tangent=add(mul(h['radial'],-math.sin(angle)),mul(h['tangent'],math.cos(angle)))
            p=add(h['center'],mul(radial,h['inner']))
            # Use the actual radial interface face, not a nearest-centroid guess.
            polygon=BRepBuilderAPI_MakePolygon()
            for q in (p,add(p,mul(radial,h['outer']-h['inner'])),add(add(p,mul(radial,h['outer']-h['inner'])),mul(h['axis'],h['width'])),add(p,mul(h['axis'],h['width']))):polygon.Add(gp_Pnt(*q))
            polygon.Close();interface=BRepBuilderAPI_MakeFace(polygon.Wire()).Face()
            matches=[]
            for k,part in enumerate(parts):
                common=boolean(BRepAlgoAPI_Common,part,interface)
                area=properties(common,'area')
                if area>h['width']*(h['outer']-h['inner'])*.999999:matches.append(k)
            if len(matches)!=1:raise ValueError('Hinge endpoint adjacency ambiguous: '+str(matches))
            h['ends'].append(matches[0]);h['radials'].append(radial);h['tangents'].append(tangent)
        a,b=h['ends'];graph[a].append((b,index,0));graph[b].append((a,index,1))
    endpoints=[i for i in range(len(parts)) if len(graph[i])==1]
    if len(endpoints)!=2 or any(len(graph[i]) not in (1,2) for i in range(len(parts))):raise ValueError('Not a simple hinge chain')
    def center(shape):
        b=bounds(shape);return tuple((b[i]+b[i+3])/2 for i in range(3))
    root=min(endpoints,key=lambda i:norm(center(parts[i])))
    order=[root];ordered=[];previous=-1;current=root
    while True:
        nexts=[x for x in graph[current] if x[0]!=previous]
        if not nexts:break
        following,hi,side=nexts[0];ordered.append((hi,side));order.append(following);previous,current=current,following
    if len(order)!=len(parts):raise ValueError('Disconnected hinge graph')
    first,side=ordered[0];h=hinges[first];sign=1 if side==0 else -1
    tangent=mul(h['tangents'][side],sign);depth=mul(h['radials'][side],-1);transverse=cross(tangent,depth)
    base=gp_Trsf();base.SetValues(*[x for row in (tangent,depth,transverse) for x in (*row,0)])
    root_bounds=bounds(transform(parts[root],base));base=translation(tuple(-root_bounds[i] for i in range(3))).Multiplied(base)
    transforms=[base];bands=[];bend_data=[]
    for k,(hi,side) in enumerate(ordered):
        h=hinges[hi];sign=1 if side==0 else -1;radial=h['radials'][side];tangent=mul(h['tangents'][side],sign)
        center=add(h['center'],mul(h['axis'],h['width']/2))
        a=add(center,mul(radial,h['neutral']));b=add(center,mul(h['radials'][1-side],h['neutral']));length=h['angle']*h['neutral']
        local=translation(add(a,mul(tangent,length))).Multiplied(rotation(h['axis'],-sign*h['angle'])).Multiplied(translation(mul(b,-1)))
        parent=transforms[-1];transforms.append(parent.Multiplied(local))
        start=point(parent,a);axis=direction(parent,mul(h['axis'],sign));normal=direction(parent,radial)
        axial=direction(parent,h['axis']);origin=add(add(start,mul(axial,-h['width']/2)),mul(normal,h['inner']-h['neutral']))
        band=rectangle_prism(origin,(length,0,0),mul(normal,h['outer']-h['inner']),mul(axial,h['width']));bands.append(band)
        bend_data.append(dict(start=start,axis=axis,outward=normal,width_axis=axial,width=h['width'],wall=h['outer']-h['inner'],inner_radius=h['inner'],neutral_radius=h['neutral'],angle=h['angle'],length=length,source_face_ids=h['face_ids']))
    flat_parts=[transform(parts[i],m) for i,m in zip(order,transforms)]
    reference_box=bounds(flat_parts[0]);cross_error=0.
    for part in flat_parts[1:]:
        b=bounds(part)
        cross_error=max(cross_error,max(abs(b[k]-reference_box[k]) for k in (1,2,4,5)))
    if cross_error>LINEAR_TOL:raise ValueError('Unfolded rigid spans do not share a straight stock cross-section envelope')
    extras=[];extra_records=[]
    for solid in source_solids[1:]:
        distances=[]
        for k,i in enumerate(order):
            distance=BRepExtrema_DistShapeShape(solid,parts[i]);distance.Perform();distances.append((distance.Value(),k))
        distances.sort()
        if distances[0][0]>LINEAR_TOL or distances[1][0]-distances[0][0]<LINEAR_TOL:raise ValueError('Additional solid has no unique source attachment')
        owner=distances[0][1];extras.append(transform(solid,transforms[owner]));extra_records.append(dict(segment=owner,volume_mm3=properties(solid),distance_mm=distances[0][0]))
    flat=flat_parts[0]
    for k,band in enumerate(bands):flat=boolean(BRepAlgoAPI_Fuse,flat,band);flat=boolean(BRepAlgoAPI_Fuse,flat,flat_parts[k+1])
    if extras:flat=compound([flat]+extras)
    flat_box=bounds(flat);cross_error=max(cross_error,max(abs(flat_box[k]-reference_box[k]) for k in (1,2,4,5)))
    if cross_error>LINEAR_TOL:raise ValueError('Additional source material extends outside the straight stock envelope')
    print('Projecting opening contours...',flush=True)
    holes,hole_proof=opening_inventory(source_faces,parts,order,transforms,flat)
    print('Openings:',len(holes),collections.Counter(h['kind'] for h in holes),flush=True)
    poses=finite_transforms(bend_data,1);matrix_error=0.
    for i,m in enumerate(transforms):
        product=poses[i].Multiplied(m)
        for test in ((0,0,0),(1000,0,0),(0,1000,0),(0,0,1000)):
            matrix_error=max(matrix_error,norm(sub(point(product,test),point(base,test))))
    if matrix_error>LINEAR_TOL:raise ValueError('Finite-radius folding transforms disagree with source placement')
    folded_parts=[transform(part,pose) for part,pose in zip(flat_parts,poses)]
    folded=folded_parts[0]
    for i,b in enumerate(bend_data):
        center=add(add(b['start'],mul(b['outward'],-b['neutral_radius'])),mul(b['width_axis'],-b['width']/2))
        forward=sector(center,b['width_axis'],b['outward'],(1,0,0),b['length']/b['neutral_radius'],b['neutral_radius']-b['wall']/2,b['neutral_radius']+b['wall']/2,b['width'])
        folded=boolean(BRepAlgoAPI_Fuse,folded,transform(forward,poses[i]));folded=boolean(BRepAlgoAPI_Fuse,folded,folded_parts[i+1])
    folded_extras=[transform(extra,poses[r['segment']]) for extra,r in zip(extras,extra_records)]
    reconstructed=transform(compound([folded]+folded_extras),base.Inverted())
    print('Verifying whole source boundary...',flush=True)
    difference=abs(properties(boolean(BRepAlgoAPI_Cut,source,reconstructed)))+abs(properties(boolean(BRepAlgoAPI_Cut,reconstructed,source)))
    source_volume=properties(source);volume_tolerance=max(1e-5,source_volume*1e-8)
    if difference>volume_tolerance or abs(properties(flat)-source_volume)>volume_tolerance or not BRepCheck_Analyzer(flat).IsValid() or len(shapes(flat,TopAbs_SOLID))!=len(source_solids):raise ValueError('Whole-source reconstruction/material conservation failed')
    report=dict(units="mm",display_deflection_mm=.025,display_angular_tolerance_rad=.12,display_mesh_authoritative=False,importer_version="0.6.0",importer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),ocp_version=OCP.__version__,source_sha256=source_digest,source_solids=len(source_solids),rigid_spans=len(parts),hinges=len(hinges),source_volume_mm3=properties(source),flat_volume_mm3=properties(flat),flat_bounds_mm=bounds(flat),flat_valid=BRepCheck_Analyzer(flat).IsValid(),flat_solids=len(shapes(flat,TopAbs_SOLID)),extra_solids=extra_records,bends=bend_data,root_from_source=matrix(base),source_to_flat=[matrix(m) for m in transforms],cross_section_envelope_error_mm=cross_error,holes=holes,opening_count=len(holes),opening_inventory_scope="closed inner wires on planar outer walls; end caps and inner mouths excluded",opening_kinds=dict(collections.Counter(h['kind'] for h in holes)),projection=hole_proof,fold_transform_error_mm=matrix_error,refold_symmetric_difference_volume_mm3=difference,volume_tolerance_mm3=volume_tolerance,source_projection_verified=True,manufacturing_authoritative=False,neutral_axis_model="mid-wall volume-preserving geometric baseline")
    print(json.dumps({k:v for k,v in report.items() if k not in ('holes','bends','source_to_flat','root_from_source')},indent=2),flush=True)
    flat_path=(out/'straight.step').resolve();writer=STEPControl_Writer()
    if writer.Transfer(flat,STEPControl_AsIs)!=IFSelect_RetDone or writer.Write(str(flat_path))!=IFSelect_RetDone:raise ValueError('Straight STEP export failed')
    check=STEPControl_Reader()
    if check.ReadFile(str(flat_path))!=IFSelect_RetDone or not check.TransferRoots():raise ValueError('Straight STEP readback failed')
    readback=check.OneShape();step_difference=abs(properties(boolean(BRepAlgoAPI_Cut,flat,readback)))+abs(properties(boolean(BRepAlgoAPI_Cut,readback,flat)))
    if not BRepCheck_Analyzer(readback).IsValid() or step_difference>volume_tolerance:raise ValueError('Straight STEP roundtrip failed')
    report['step_roundtrip_symmetric_difference_volume_mm3']=step_difference
    if hashlib.sha256(source_path.read_bytes()).hexdigest()!=source_digest:raise ValueError('Source changed during import; reimport a stable file')
    report['brep_valid']=True;report['solids']=report['flat_solids']
    (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    write_openings(out/'openings.csv',holes)
    print('Preparing source meshes...',flush=True)
    rigid_meshes=[]
    for i,part in enumerate(flat_parts):
        attached=[extra for extra,r in zip(extras,extra_records) if r['segment']==i]
        rigid_meshes.append(mesh_data(compound([part]+attached)))
    package=dict(format='ark-source-tube',version=1,source_name=source_path.stem,source_path=str(source_path.resolve()),source_sha256=report['source_sha256'],flat_step_path=str(flat_path),flat_step_sha256=hashlib.sha256(flat_path.read_bytes()).hexdigest(),flat_step_base64=base64.b64encode(flat_path.read_bytes()).decode('ascii'),report=report,bends=bend_data,holes=holes,rigid=rigid_meshes,straight=mesh_data(flat))
    (out/'package.json').write_text(json.dumps(package,separators=(',',':')),encoding='utf-8')
    print('Package bytes:',(out/'package.json').stat().st_size,flush=True)
    return package


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source');parser.add_argument('output');args=parser.parse_args()
    if Path(args.source).suffix.lower()=='.json':
        request=json.loads(Path(args.source).read_text(encoding='utf-8-sig'))
        if request.get('operation')=='export':
            package=request['package'];cached=Path(package['flat_step_path']);original=Path(package['source_path'])
            if original.exists() and hashlib.sha256(original.read_bytes()).hexdigest()!=package['source_sha256']:raise ValueError('Source STEP changed since verified import')
            payload=base64.b64decode(package['flat_step_base64'],validate=True) if 'flat_step_base64' in package else cached.read_bytes()
            if hashlib.sha256(payload).hexdigest()!=package['flat_step_sha256']:raise ValueError('Stored source projection changed')
            target=Path(args.output);target.parent.mkdir(parents=True,exist_ok=True);temp=target.with_suffix('.pending.step');temp.write_bytes(payload);temp.replace(target)
            receipt=dict(package['report'],export_pose='straight',source_sha256=package['source_sha256'],step_sha256=package['flat_step_sha256'],source_projection_verified=True,manufacturing_authoritative=False)
            target.with_suffix('.validation.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
            write_openings(target.with_suffix('.openings.csv'),package['holes'])
        else:unfold(request['source_path'],args.output)
    else:unfold(args.source,args.output)
