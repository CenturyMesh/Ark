"""Compose a 50-second edited guide from verified native captures.

Install Pillow 10.4.0 and imageio-ffmpeg 0.5.1 under out/launch-demo/tools.
Requires plate-before.png, plate-after.png, plate-bom.png and evidence.json.
The owner's original docs/showcase-drone.png is read without modification.
"""
import json, pathlib, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'out/launch-demo'
sys.path.insert(0,str(OUT/'tools'))
from PIL import Image, ImageDraw, ImageFont
import imageio_ffmpeg
FFMPEG=imageio_ffmpeg.get_ffmpeg_exe()
W,H=1920,1080
BG='#0b1018'; CARD='#14202d'; INK='#eff6ff'; MUTED='#a4b9ce'; CYAN='#56cee7'
fontroot=pathlib.Path('C:/Windows/Fonts')
def font(size,bold=False): return ImageFont.truetype(str(fontroot/('segoeuib.ttf' if bold else 'segoeui.ttf')),size)
def text(draw,xy,value,size=30,color=INK,bold=False): draw.text(xy,value,font=font(size,bold),fill=color)
def base(number,title,sub):
    image=Image.new('RGB',(W,H),BG);d=ImageDraw.Draw(image)
    text(d,(70,35),'ARK',46,CYAN,True);text(d,(208,42),'CAD for people and agents',29,MUTED)
    text(d,(70,95),title,43,INK,True);text(d,(70,150),sub,25,MUTED)
    text(d,(70,1022),'Edited guided demo  /  native ARK captures  /  synthetic editing example  /  no AI timing claims',23,MUTED)
    for i in range(6):d.rounded_rectangle((1630+i*40,49,1658+i*40,55),radius=3,fill=CYAN if i<=number else '#283749')
    return image
def put(image,path,box):
    src=Image.open(path).convert('RGB');src.thumbnail((box[2]-box[0],box[3]-box[1]),Image.Resampling.LANCZOS)
    image.paste(src,(box[0]+(box[2]-box[0]-src.width)//2,box[1]+(box[3]-box[1]-src.height)//2))
def panel(image,title,lines):
    d=ImageDraw.Draw(image);d.rounded_rectangle((1400,210,1850,974),radius=20,fill=CARD)
    text(d,(1430,246),title,32,CYAN,True)
    y=310
    for big,small in lines:
        text(d,(1430,y),big,31,INK,True);text(d,(1430,y+48),small,23,MUTED);y+=145
scenes=[]
a=base(0,'From a complete design to an editable part','An existing drone workspace, followed by a separate synthetic CAD example.')
put(a,ROOT/'docs/showcase-drone.png',(70,210,1850,929))
text(ImageDraw.Draw(a),(70,949),'Owner-selected drone capture · original image preserved · ARK 014D workspace',23,MUTED)
scenes.append((8,a))
a=base(1,'Start with explicit design intent','A synthetic sensor mounting plate: one native solid, four mounting holes.')
put(a,OUT/'plate-before.png',(70,200,1350,1000))
panel(a,'01  /  BEFORE',[('100 mm wide','65 mm deep · 6 mm thick'),('Four holes','Diameter 8 mm'),('Editable history','Parameters and native features')]);scenes.append((8,a))
a=base(2,'Edit the parameter; recompute the geometry','The scripted MCP edit uses the current revision and the existing definition.')
put(a,OUT/'plate-after.png',(70,200,1350,1000))
panel(a,'02  /  EDIT',[('plate_width','100 → 140 mm'),('Atomic edit','expected_revision: 1 → r2'),('Same part identity','DEMO-PLATE-001')]);scenes.append((8,a))
a=base(3,'Inspect what actually changed','Measured native bounds confirm the width change. Both states are valid OCCT solids.')
put(a,OUT/'plate-before.png',(70,238,938,810));put(a,OUT/'plate-after.png',(982,238,1850,810))
d=ImageDraw.Draw(a);text(d,(70,827),'BEFORE  /  100 × 65 × 6 mm',31,INK,True);text(d,(982,827),'AFTER  /  140 × 65 × 6 mm',31,INK,True)
text(d,(70,887),'Four holes remain linked to the width parameter.',27,MUTED);text(d,(70,936),'Native inspection: B-rep valid · 1 solid before · 1 solid after',27,CYAN)
scenes.append((10,a))
a=base(4,'Review the bill of materials in Studio','The table is derived from this same edited part, at the current document revision.')
put(a,OUT/'plate-bom.png',(70,200,1350,1000))
panel(a,'03  /  BOM',[('DEMO-PLATE-001','Quantity 1 · make'),('Nominal material','Aluminium · declared metadata'),('Needs review','Manufacturing route pending')]);scenes.append((12,a))
a=base(5,'Try ARK. Inspect the result. Share reproducible feedback.','Windows Studio · Linux headless CLI/MCP · Apache-2.0 source')
put(a,OUT/'plate-after.png',(70,200,1350,1000))
panel(a,'OPEN SOURCE',[('Get the preview','github.com/CenturyMesh/Ark'),('Keep it editable','Inspect parameters and BOM'),('Concept scope','No fabrication qualification')]);scenes.append((4,a))
clips=[]
for i,(duration,im) in enumerate(scenes):
    frame=OUT/('scene-%02d.png'%i);im.save(frame)
    clip=OUT/('scene-%02d.mp4'%i);clips.append(clip)
    subprocess.run([FFMPEG,'-y','-loglevel','error','-loop','1','-i',str(frame),'-t',str(duration),'-r','30','-vf','fade=t=in:st=0:d=0.3,fade=t=out:st=%s:d=0.3'%(duration-.3),'-c:v','libx264','-preset','medium','-crf','19','-pix_fmt','yuv420p','-an',str(clip)],check=True)
concat=OUT/'concat.txt';concat.write_text(''.join("file '%s'\n"%p.as_posix() for p in clips),encoding='utf-8')
video=OUT/'ark-guided-demo-016F.mp4'
subprocess.run([FFMPEG,'-y','-loglevel','error','-f','concat','-safe','0','-i',str(concat),'-c','copy','-movflags','+faststart',str(video)],check=True)
thumb=Image.new('RGB',(1440,540),BG)
for i,(_,im) in enumerate(scenes):thumb.paste(im.resize((480,270),Image.Resampling.LANCZOS),((i%3)*480,(i//3)*270))
thumb.save(OUT/'storyboard.png')
print(json.dumps({'video':str(video),'duration_seconds':sum(s[0] for s in scenes),'dimensions':[W,H],'bytes':video.stat().st_size}))
