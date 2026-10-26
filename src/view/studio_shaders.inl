// Display-only studio lighting. These shaders never select or modify CAD geometry.
// Shared by the raster BRDF and the progressive ray integrator so their materials
// and exposure have the same meaning. The environment is analytic, not an HDRI.
static const std::string STUDIO_LIGHTING_GLSL=R"(
const float PI=3.141592653589793;
uniform vec3 studioRight,studioUp,studioBack;
vec3 toStudio(vec3 d){return vec3(dot(d,studioRight),dot(d,studioUp),dot(d,studioBack));}
vec3 fromStudio(vec3 d){return studioRight*d.x+studioUp*d.y+studioBack*d.z;}
vec3 srgbToLinear(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
vec3 linearToSrgb(vec3 c){return mix(c*12.92,1.055*pow(max(c,vec3(0.)),vec3(1./2.4))-.055,step(vec3(.0031308),c));}
vec3 studioEnvironment(vec3 worldDirection,float blur){
 vec3 d=toStudio(normalize(worldDirection));
 vec3 c=mix(vec3(.045,.055,.072),vec3(.24,.28,.34),smoothstep(-.35,.9,d.y));
 // Finite luminous studio panels. Angular width broadens the raster reflection
 // only; the path integrator evaluates the actual sharp environment (blur=0).
 vec3 a=normalize(vec3(-.55,.72,.42)),b=normalize(vec3(.78,.30,-.32));
 vec3 q=normalize(vec3(-.25,.28,-.93)),t=normalize(vec3(.05,.96,-.18));
 float width=max(.016,blur*blur*.36);
 float key=exp((dot(d,a)-1.)/(.065+width));
 float fill=exp((dot(d,b)-1.)/(.032+width));
 float rim=exp((dot(d,q)-1.)/(.019+width));
 float roof=exp((dot(d,t)-1.)/(.09+width));
 // Peak radiance falls with convolution width, keeping a rough metal from
 // receiving the peak energy of a sharp mirror reflection over a wide lobe.
 c+=vec3(5.8,5.35,4.8)*key*.065/(.065+width);
 c+=vec3(2.2,2.7,3.6)*fill*.032/(.032+width);
 c+=vec3(4.2,4.8,5.6)*rim*.019/(.019+width);
 c+=vec3(1.6,1.7,1.9)*roof*.09/(.09+width);
 return c;
}
vec3 fresnelSchlick(float vh,vec3 f0){return f0+(1.-f0)*pow(clamp(1.-vh,0.,1.),5.);}
float distributionGGX(float nh,float r){float a=r*r,a2=a*a,q=nh*nh*(a2-1.)+1.;return a2/max(PI*q*q,1e-12);}
float smithGGX(float nv,float nl,float r){float a2=pow(r,4.);float gv=nl*sqrt(nv*nv*(1.-a2)+a2);float gl=nv*sqrt(nl*nl*(1.-a2)+a2);return .5/max(gv+gl,1e-5);}
vec3 evaluateBRDF(vec3 n,vec3 v,vec3 l,vec3 base,float metal,float r){
 float nv=max(dot(n,v),1e-5),nl=max(dot(n,l),0.);if(nl<=0.)return vec3(0.);
 vec3 h=normalize(v+l),f=fresnelSchlick(max(dot(v,h),0.),mix(vec3(.04),base,metal));
 return (1.-f)*(1.-metal)*base/PI+f*distributionGGX(max(dot(n,h),0.),r)*smithGGX(nv,nl,r);
}
vec2 environmentBRDF(float nv,float r){
 // Analytic fit to the split-sum GGX BRDF integral (Epic's EnvBRDFApprox).
 vec4 c=r*vec4(-1.,-.0275,-.572,.022)+vec4(1.,.0425,1.04,-.04);
 float a=min(c.x*c.x,exp2(-9.28*nv))*c.x+c.y;return vec2(-1.04,1.04)*a+c.zw;
}
vec3 studioTonemap(vec3 c){
 c=max(c,vec3(0.));float y=dot(c,vec3(.2126,.7152,.0722));
 float mapped=(y*(2.51*y+.03))/(y*(2.43*y+.59)+.14);
 // Luminance mapping preserves material hue. Gamut compression handles HDR
 // chroma without hard clipping bright coloured surfaces to white.
 c*=mapped/max(y,1e-6);float peak=max(c.r,max(c.g,c.b));
 if(peak>1.)c=mix(vec3(mapped),c,clamp((1.-mapped)/max(peak-mapped,1e-5),0.,1.));
 return linearToSrgb(clamp(c,0.,1.));
}
)";
static const std::string FS_STUDIO_SOLID_SOURCE=std::string("#version 330 core\n")+STUDIO_LIGHTING_GLSL+R"(
in vec3 nrm,wpos,lpos,lnrm;out vec4 o;
uniform vec3 emissionColor;uniform float emissionIntensity,emissionBloom,emissionGlow,emissionOnly;
uniform vec3 color,eye;uniform float sel,hardSurface,satin,metallic,roughness,opacity,hasTexture,selectionOverlay,cameraOrtho;
uniform float sectionCap;
uniform sampler2DShadow keyShadowMap;uniform mat4 keyShadowMatrix;uniform float keyShadowStrength,keyShadowTexelWorld;
float keyVisibility(vec3 n,vec3 light){
 if(keyShadowStrength<=0.)return 1.;
 // Normal offset scales in world-space texels, independently of camera zoom.
 float slope=1.-max(dot(n,light),0.);
 vec4 p=keyShadowMatrix*vec4(wpos+n*keyShadowTexelWorld*(.45+.9*slope),1.);
 vec3 q=p.xyz/p.w*.5+.5;
 if(any(lessThan(q,vec3(0.)))||any(greaterThan(q,vec3(1.))))return 1.;
 vec2 texel=1./vec2(textureSize(keyShadowMap,0));float visibility=0.;
 for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)visibility+=texture(keyShadowMap,vec3(q.xy+vec2(x,y)*texel,q.z-.00008));
 return mix(1.,visibility/25.,keyShadowStrength);
}
uniform vec2 textureScale;uniform sampler2D surfaceTexture;
uniform float stampMode,stampDepth,stampFrontOnly;uniform vec3 stampOrigin,stampU,stampV,stampNormal;uniform vec2 stampSize;
vec4 triplanar(){
 vec3 a=pow(abs(normalize(dot(lnrm,lnrm)>1e-8?lnrm:vec3(0.,1.,0.))),vec3(4.));a/=max(a.x+a.y+a.z,1e-5);
 return texture(surfaceTexture,lpos.xy*textureScale)*a.z+texture(surfaceTexture,lpos.xz*textureScale)*a.y+texture(surfaceTexture,lpos.yz*textureScale)*a.x;
}
// 044B procedural patterns: the same scalar fields as appearancePatternField044B.
uniform float patternKind,patternScale,patternAngle,patternContrast,patternColorSet;uniform vec3 patternColor;
float patternHash(float x,float y){return fract(sin(x*127.1+y*311.7)*43758.5453);}
float patternNoise(float x){float i=floor(x),f=x-i,u=f*f*(3.-2.*f);return mix(patternHash(i,0.),patternHash(i+1.,0.),u);}
float patternField(float k,vec2 q){
 if(k>.5&&k<3.5){bool plain=abs(k-2.)<.5;float t=plain?2.:4.;vec2 g=q*t,c=floor(g),f=g-c;bool warp=plain?mod(c.x+c.y,2.)<1.:mod(c.x+c.y,4.)<2.;float p=warp?sin(3.14159265*f.y):sin(3.14159265*f.x);float lit=abs(k-3.)<.5?(warp?.75+.25*p:.25+.25*p):(warp?.55+.45*p:.1+.45*p);return 1.-lit;}
 if(abs(k-4.)<.5){vec2 g=q*3.,i0=floor(g);float d1=9.,d2=9.;for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i){vec2 c=i0+vec2(float(i),float(j));vec2 p=c+vec2(patternHash(c.x,c.y),patternHash(c.y+17.3,c.x-9.1));float d=length(p-g);if(d<d1){d2=d1;d1=d;}else if(d<d2)d2=d;}return clamp(.85*(1.-smoothstep(.02,.22,d2-d1))+.15*patternHash(floor(q.x*12.),floor(q.y*12.)),0.,1.);}
 if(abs(k-5.)<.5)return clamp(.65*patternNoise(q.y*60.)+.35*patternNoise(q.y*170.+3.7),0.,1.);
 if(abs(k-6.)<.5){float a=abs(fract(q.x+q.y)-.5),b=abs(fract(q.x-q.y)-.5);return 1.-2.*min(.5-a,.5-b);}
 if(abs(k-7.)<.5){float x=q.x+.5*mod(floor(q.y),2.);return 1.-smoothstep(.26,.30,length(vec2(fract(x)-.5,fract(q.y)-.5)));}
 if(abs(k-8.)<.5){float x=q.x+.5*mod(floor(q.y),2.),r=length(vec2(fract(x)-.5,(fract(q.y)-.5)*1.1547))/.43;return 1.-sqrt(max(0.,1.-r*r))*(1.-smoothstep(.85,1.,r));}
 if(abs(k-9.)<.5){float x=q.x+.5*mod(floor(q.y),2.),d=max(abs(fract(x)-.5)/.39,abs(fract(q.y)-.5)/.30);return smoothstep(.83,1.,d);}
 if(abs(k-10.)<.5){float d=abs(fract(q.x+abs(fract(q.y)-.5)*1.5)-.5);return smoothstep(.20,.29,d);}
 return 0.;
}
float patternTriplanar(){
 vec3 a=pow(abs(normalize(dot(lnrm,lnrm)>1e-8?lnrm:vec3(0.,1.,0.))),vec3(4.));a/=max(a.x+a.y+a.z,1e-5);float c=cos(radians(patternAngle)),s=sin(radians(patternAngle)),sc=max(patternScale,1e-6);mat2 rot=mat2(c,-s,s,c);
 return a.z*patternField(patternKind,rot*lpos.xy/sc)+a.y*patternField(patternKind,rot*lpos.xz/sc)+a.x*patternField(patternKind,rot*lpos.yz/sc);
}
// Screen-space bump from the pattern height (mm); display relief only.
vec3 patternBump(vec3 n,float h){vec3 dpx=dFdx(wpos),dpy=dFdy(wpos);float dhx=dFdx(h),dhy=dFdy(h);vec3 r1=cross(dpy,n),r2=cross(n,dpx);float det=dot(dpx,r1);if(abs(det)<1e-12)return n;return normalize(abs(det)*n-sign(det)*(dhx*r1+dhy*r2));}
void main(){
 if(emissionOnly>.5){o=vec4(srgbToLinear(emissionColor)*emissionIntensity*emissionBloom*opacity,emissionIntensity>0.?emissionGlow/64.:0.);return;}
 if(selectionOverlay>.5){o=vec4(.97,.63,.21,sel*.16);return;}
 vec3 n=hardSurface>0.?normalize(cross(dFdx(wpos),dFdy(wpos))):normalize(nrm),v=cameraOrtho>.5?studioBack:normalize(eye-wpos);
 if(dot(n,v)<0.)n=-n;vec4 texel=hasTexture>.5?triplanar():vec4(1.);
 if(stampMode>.5){vec3 delta=lpos-stampOrigin;vec2 uv=vec2(dot(delta,stampU),dot(delta,stampV))/stampSize+vec2(.5);if(any(lessThan(uv,vec2(0.)))||any(greaterThan(uv,vec2(1.)))||abs(dot(delta,stampNormal))>stampDepth||(stampFrontOnly>.5&&dot(lnrm,stampNormal)<=0.))discard;texel=texture(surfaceTexture,uv);if(texel.a<.001)discard;}
 vec3 tint=clamp(color*texel.rgb,0.,1.);float m=clamp(metallic,0.,1.),r=clamp(roughness,.055,1.);
 if(sectionCap>.5){float hatch=smoothstep(.12,.20,abs(fract((gl_FragCoord.x+gl_FragCoord.y)/12.)-.5));tint*=mix(.58,1.,hatch);}
 if(patternKind>.5&&stampMode<.5){float pf=patternTriplanar();vec3 second=patternColorSet>.5?patternColor:tint*(abs(patternKind-7.)<.5?.08:.45);tint=mix(tint,second,clamp(pf*patternContrast,0.,1.));
  if(abs(patternKind-5.)>.5)r=clamp(r+(abs(patternKind-3.)<.5?-(1.-pf)*.25:patternKind>5.5?pf*.2:(pf-.5)*.35)*patternContrast,.04,1.);n=patternBump(n,(1.-pf)*patternContrast*patternScale*.06);}
 vec3 base=srgbToLinear(tint);
 // Normal variance broadens subpixel highlights instead of letting a sharp
 // highlight sparkle as a curved surface or small part moves across pixels.
 float variance=max(dot(dFdx(n),dFdx(n)),dot(dFdy(n),dFdy(n)));r=sqrt(clamp(r*r+min(variance,.18),.003,1.));
 float nv=max(dot(n,v),.001);vec3 f0=mix(vec3(.04),base,m),f=fresnelSchlick(nv,f0);
 vec3 c=base*(1.-m)*(1.-f)*studioEnvironment(n,.85)*.85;
 vec2 ab=environmentBRDF(nv,r);c+=studioEnvironment(reflect(-v,n),r)*(f0*ab.x+ab.y)*mix(.72,1.,satin);
 vec3 l=fromStudio(normalize(vec3(-.55,.72,.42)));c+=evaluateBRDF(n,v,l,base,m,max(r,.24))*vec3(2.4,2.25,2.05)*max(dot(n,l),0.)*keyVisibility(n,l);
 l=fromStudio(normalize(vec3(.78,.30,-.32)));c+=evaluateBRDF(n,v,l,base,m,max(r,.28))*vec3(.55,.66,.85)*max(dot(n,l),0.);
 l=fromStudio(normalize(vec3(-.25,.28,-.93)));c+=evaluateBRDF(n,v,l,base,m,max(r,.20))*vec3(.72,.82,1.)*max(dot(n,l),0.);
 c=studioTonemap(c+srgbToLinear(emissionColor)*emissionIntensity);c=mix(c,min(c+vec3(.105,.061,.012),vec3(1.)),sel);
 o=vec4(c,opacity*texel.a);
})";
static const char* FS_SOLID=FS_STUDIO_SOLID_SOURCE.c_str();

// CPU supplies two endpoint positions and six quad corners per segment.
// Expansion and coverage are in physical viewport pixels, independent of GL
// line-width support, perspective, zoom and the underlying triangulation.
static const char* VS_FEATURE_EDGE=R"(#version 330 core
layout(location=0)in vec3 A;layout(location=1)in vec3 B;layout(location=2)in vec2 corner;
uniform mat4 vp,model;uniform vec2 viewport;uniform float width;
noperspective out vec2 local;flat out float segmentLength;
void main(){
 vec4 a=vp*model*vec4(A,1.),b=vp*model*vec4(B,1.);
 // Clip against the near plane before expansion; never divide behind the eye.
 float da=a.z+a.w,db=b.z+b.w;if(da<0.&&db<0.){gl_Position=vec4(2.,2.,2.,1.);local=vec2(0.);segmentLength=0.;return;}
 if(da<0.)a=mix(a,b,clamp(da/(da-db),0.,1.));else if(db<0.)b=mix(b,a,clamp(db/(db-da),0.,1.));
 vec2 pa=a.xy/max(a.w,1e-6)*viewport*.5,pb=b.xy/max(b.w,1e-6)*viewport*.5;
 vec2 delta=pb-pa;float len=max(length(delta),1e-4);vec2 axis=delta/len,normal=vec2(-axis.y,axis.x);
 float extent=width*.5+1.25;vec4 p=corner.x<.5?a:b;
 vec2 offset=normal*corner.y*extent+axis*(corner.x<.5?-extent:extent);
 p.xy+=offset*2./viewport*p.w;
 // Two millionths of NDC, rather than the old .0008, cannot pull an edge
 // through an unrelated wall. The filled depth pass supplies slope offset.
 p.z-=.000002*p.w;gl_Position=p;
 local=vec2(corner.x*len+(corner.x<.5?-extent:extent),corner.y*extent);segmentLength=len;
})";
static const char* FS_FEATURE_EDGE=R"(#version 330 core
noperspective in vec2 local;flat in float segmentLength;uniform float width;out vec4 o;
void main(){vec2 p=vec2(local.x-clamp(local.x,0.,segmentLength),local.y);float d=length(p)-width*.5;
 float coverage=1.-smoothstep(-.65,.65,d);if(coverage<.005)discard;o=vec4(.008,.010,.012,coverage*.94);})";

static const char* FS_CONTOUR=R"(#version 330 core
uniform sampler2D gbuf;uniform vec2 res,origin;uniform float depthFar;out vec4 o;
void main(){
 vec2 uv=(gl_FragCoord.xy-origin)/res;vec4 g=texture(gbuf,uv);float edge=0.;
 for(int k=0;k<8;++k){float a=float(k)*.785398163;vec4 q=texture(gbuf,uv+vec2(cos(a),sin(a))/res);
  bool bg=g.a>.985,qbg=q.a>.985;if(bg!=qbg){edge=max(edge,.68);continue;}if(bg)continue;
  // Only visible normal/depth discontinuities. Smooth curved faces produce
  // no tessellation wireframe; depth bounds scale with local view depth.
  float dz=abs(q.a-g.a)*depthFar,depth=max(g.a*depthFar,.001);
  float angle=1.-dot(normalize(g.rgb*2.-1.),normalize(q.rgb*2.-1.));
  float jump=smoothstep(depth*.003+depthFar*.000008,depth*.009+depthFar*.000025,dz);
  edge=max(edge,max(jump*.75,smoothstep(.14,.32,angle)*.50));
 }
 o=vec4(.008,.010,.012,edge);
})";
