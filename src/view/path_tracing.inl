// Optional, genuine progressive path tracing on the GPU. OpenGL 3.3 fragment
// shaders traverse a world-space triangle BVH: this does not use RTX RT cores.
// The immutable display tessellation is input, never a replacement for B-rep.
static const std::string FS_PATH_SOURCE=std::string("#version 330 core\n")+STUDIO_LIGHTING_GLSL+R"(
uniform samplerBuffer triangles,nodes;uniform sampler2D previous;
uniform vec2 resolution,focal,clipRange;uniform vec3 camera;uniform float ortho,rayEpsilon;
uniform int sampleIndex,nodeCount;out vec4 o;
uint rng;
float randomValue(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(rng&0xffffffu)/16777216.;}
vec3 cosineDirection(vec3 n){float a=2.*PI*randomValue(),u=randomValue();vec3 t=normalize(cross(abs(n.y)<.9?vec3(0.,1.,0.):vec3(1.,0.,0.),n));return t*(sqrt(u)*cos(a))+cross(n,t)*(sqrt(u)*sin(a))+n*sqrt(1.-u);}
bool boxHit(vec3 lo,vec3 hi,vec3 ro,vec3 inv,float nearest){vec3 a=(lo-ro)*inv,b=(hi-ro)*inv;vec3 l=min(a,b),h=max(a,b);return max(max(l.x,l.y),max(l.z,0.))<=min(min(h.x,h.y),min(h.z,nearest));}
bool intersection(vec3 ro,vec3 rd,float maximum,out int face,out float distance,out vec2 bary){
 face=-1;distance=maximum;bary=vec2(0.);int stack[48],top=0;stack[top++]=0;
 vec3 inv=1./(sign(rd)*max(abs(rd),vec3(1e-20))+vec3(equal(rd,vec3(0.)))*1e-20);
 // CPU median subdivision bounds depth to 19 at the 250k triangle ceiling.
 // Traversal is bounded independently so a malformed buffer cannot hang GPU.
 for(int iteration=0;iteration<524288&&top>0;++iteration){int node=stack[--top];if(node<0||node>=nodeCount)continue;
  vec4 lo=texelFetch(nodes,node*2),hi=texelFetch(nodes,node*2+1);if(!boxHit(lo.xyz,hi.xyz,ro,inv,distance))continue;
  int count=int(hi.w);if(count>0){int first=int(lo.w);
   for(int k=0;k<4;++k){if(k>=count)break;int id=first+k;vec3 p=texelFetch(triangles,id*8).xyz,e1=texelFetch(triangles,id*8+1).xyz,e2=texelFetch(triangles,id*8+2).xyz;
    vec3 q=cross(rd,e2);float det=dot(e1,q);if(abs(det)<1e-10)continue;float inverse=1./det;vec3 delta=ro-p;float u=dot(delta,q)*inverse;if(u<0.||u>1.)continue;
    vec3 s=cross(delta,e1);float v=dot(rd,s)*inverse;if(v<0.||u+v>1.)continue;float t=dot(e2,s)*inverse;
    if(t>rayEpsilon*.25&&t<distance){face=id;distance=t;bary=vec2(u,v);}
   }
  }else if(top<46){stack[top++]=int(lo.w);stack[top++]=-int(hi.w);}
 }
 return face>=0;
}
float specularProbability(vec3 base,float metal){return clamp(.25+.5*metal+.1*max(base.r,max(base.g,base.b)),.25,.85);}
float bsdfPdf(vec3 n,vec3 v,vec3 l,float r,float probability){
 float nl=max(dot(n,l),0.);if(nl<=0.)return 0.;vec3 h=normalize(v+l);
 float spec=distributionGGX(max(dot(n,h),0.),r)*max(dot(n,h),0.)/max(4.*abs(dot(v,h)),1e-5);
 return mix(nl/PI,spec,probability);
}
vec3 sampleBRDF(vec3 n,vec3 v,float r,float probability){
 if(randomValue()>probability)return cosineDirection(n);
 float a=r*r,u=randomValue(),phi=2.*PI*randomValue(),ct=sqrt((1.-u)/max(1.+(a*a-1.)*u,1e-6));
 float st=sqrt(max(0.,1.-ct*ct));vec3 t=normalize(cross(abs(n.y)<.9?vec3(0.,1.,0.):vec3(1.,0.,0.),n));
 vec3 h=normalize(t*(st*cos(phi))+cross(n,t)*(st*sin(phi))+n*ct);return reflect(-v,h);
}
float powerWeight(float a,float b){return a*a/max(a*a+b*b,1e-10);}
void main(){
 ivec2 pixel=ivec2(gl_FragCoord.xy);rng=uint(pixel.x)*1973u+uint(pixel.y)*9277u+uint(sampleIndex+1)*26699u+911u;rng|=1u;
 vec2 uv=(gl_FragCoord.xy+vec2(randomValue()-.5,randomValue()-.5))/resolution,ndc=uv*2.-1.;
 vec3 ro=camera,rd=normalize(fromStudio(vec3(ndc/focal,-1.)));
 if(ortho>.5){ro+=fromStudio(vec3(ndc/focal,0.));rd=-studioBack;}
 float cameraForward=max(dot(rd,-studioBack),1e-6);ro+=rd*(clipRange.x/cameraForward);float primaryMaximum=(clipRange.y-clipRange.x)/cameraForward;
 vec3 radiance=vec3(0.),throughput=vec3(1.),previousNormal=vec3(0.);float previousPdf=0.,coverage=0.;
 for(int bounce=0;bounce<4;++bounce){int id;float distance;vec2 bary;
  if(!intersection(ro,rd,bounce==0?primaryMaximum:1e30,id,distance,bary)){
   if(bounce>0){float lightPdf=max(dot(previousNormal,rd),0.)/PI;float mis=powerWeight(previousPdf,lightPdf);radiance+=throughput*studioEnvironment(rd,0.)*mis;}
   break;
  }
  if(bounce==0)coverage=1.;vec3 hit=ro+rd*distance,e1=texelFetch(triangles,id*8+1).xyz,e2=texelFetch(triangles,id*8+2).xyz;
  vec3 geometric=normalize(cross(e1,e2));if(dot(geometric,rd)>0.)geometric=-geometric;
  vec3 n0=texelFetch(triangles,id*8+3).xyz,n1=texelFetch(triangles,id*8+4).xyz,n2=texelFetch(triangles,id*8+5).xyz;
  vec3 n=normalize(n0*(1.-bary.x-bary.y)+n1*bary.x+n2*bary.y);if(dot(n,geometric)<0.)n=-n;if(dot(n,rd)>0.)n=geometric;
  vec4 material=texelFetch(triangles,id*8+6),parameters=texelFetch(triangles,id*8+7);vec3 base=material.rgb,v=-rd;float metal=material.w,r=parameters.x;
  float probability=specularProbability(base,metal);vec3 light=cosineDirection(n);float nl=max(dot(n,light),0.),lightPdf=nl/PI;
  if(dot(geometric,light)>0.){int blocker;float blockerDistance;vec2 blockerBary;
   if(!intersection(hit+geometric*rayEpsilon,light,1e30,blocker,blockerDistance,blockerBary)){
    // The terminal surface has no competing continuation-ray estimator.
    float mis=bounce==3?1.:powerWeight(lightPdf,bsdfPdf(n,v,light,r,probability));
    radiance+=throughput*evaluateBRDF(n,v,light,base,metal,r)*studioEnvironment(light,0.)*nl*mis/max(lightPdf,1e-6);
   }
  }
  if(bounce==3)break;
  vec3 next=sampleBRDF(n,v,r,probability);float pdf=bsdfPdf(n,v,next,r,probability),cosine=max(dot(n,next),0.);
  if(pdf<1e-7||cosine<=0.||dot(geometric,next)<=0.)break;
  throughput*=evaluateBRDF(n,v,next,base,metal,r)*cosine/pdf;
  if(bounce>=2){float survive=clamp(max(throughput.r,max(throughput.g,throughput.b)),.10,.95);if(randomValue()>survive)break;throughput/=survive;}
  previousNormal=n;previousPdf=pdf;ro=hit+geometric*rayEpsilon;rd=next;
 }
 // Four-bounce preview, no radiance clamp or denoiser disguised as convergence.
 vec4 sampleValue=vec4(max(radiance,vec3(0.)),coverage);vec4 old=sampleIndex>0?texelFetch(previous,pixel,0):vec4(0.);
 o=mix(old,sampleValue,1./float(sampleIndex+1));
})";
static const std::string FS_PATH_PRESENT_SOURCE=std::string("#version 330 core\n")+STUDIO_LIGHTING_GLSL+R"(
uniform sampler2D image;uniform vec2 resolution,origin;out vec4 o;
void main(){vec4 sampleValue=texture(image,(gl_FragCoord.xy-origin)/resolution);o=vec4(studioTonemap(sampleValue.rgb/max(sampleValue.a,1e-6)),sampleValue.a);})";

struct TraceTriangle {Vec3 p[3],n[3],base;float metallic=0,roughness=.55f;};
struct TraceNode {AABB box;int first=0,count=0,left=0,right=0;};
struct PathTraceState {
 GLuint program=0,present=0,fbo=0,accum[2]{},triangleBuffer=0,triangleTexture=0,nodeBuffer=0,nodeTexture=0;
 int width=0,height=0,samples=0,triangleCount=0,nodeCount=0;bool attempted=false,ready=false,active=false;
 std::uint64_t sceneKey=0,cameraKey=0;Vec3 origin;std::string reason;
 double buildMs=0,lastSubmitMs=0;std::vector<TraceTriangle> triangles;std::vector<TraceNode> nodes;
};
static PathTraceState g_trace;
static void traceHash(std::uint64_t& key,const void* data,size_t size){auto* bytes=(const unsigned char*)data;for(size_t i=0;i<size;++i){key^=bytes[i];key*=1099511628211ull;}}
template<class T>static void traceHashValue(std::uint64_t& key,const T& value){traceHash(key,&value,sizeof value);}
static void studioBasis(GLuint program,const CameraFrame& frame){
 auto set=[&](const char* name,Vec3 v){glUniform3f(glGetUniformLocation(program,name),(float)v.x,(float)v.y,(float)v.z);};
 set("studioRight",frame.right);set("studioUp",frame.up);set("studioBack",frame.outward);
}
static bool initializePathTracer(){
 if(g_trace.attempted)return g_trace.ready;g_trace.attempted=true;
 if(!glTexBuffer){g_trace.reason="OpenGL texture buffers unavailable";return false;}
 g_trace.program=mkProg(VS_BG,FS_PATH_SOURCE.c_str());g_trace.present=mkProg(VS_BG,FS_PATH_PRESENT_SOURCE.c_str());
 GLint linked=0;glGetProgramiv(g_trace.program,GL_LINK_STATUS,&linked);if(!linked){g_trace.reason="GPU path shader failed to link";return false;}
 glGetProgramiv(g_trace.present,GL_LINK_STATUS,&linked);if(!linked){g_trace.reason="Path presentation shader failed to link";return false;}
 glGenFramebuffers(1,&g_trace.fbo);glGenTextures(2,g_trace.accum);glGenBuffers(1,&g_trace.triangleBuffer);glGenTextures(1,&g_trace.triangleTexture);glGenBuffers(1,&g_trace.nodeBuffer);glGenTextures(1,&g_trace.nodeTexture);
 g_trace.ready=true;return true;
}
static int buildTraceNode(int first,int count){
 int index=(int)g_trace.nodes.size();g_trace.nodes.emplace_back();AABB bounds,centers;
 for(int i=first;i<first+count;++i){auto& triangle=g_trace.triangles[(size_t)i];
  // Bounds enclose the encoded GPU triangle, including p0+float(edge), rather
  // than a differently rounded original double endpoint. Internal boxes then
  // enclose those same leaf boxes. No epsilon/tolerance changes CAD geometry.
  auto packed=[](Vec3 p){return Vec3{(float)p.x,(float)p.y,(float)p.z};};Vec3 p=packed(triangle.p[0]);bounds.grow(p);bounds.grow(p+packed(triangle.p[1]-triangle.p[0]));bounds.grow(p+packed(triangle.p[2]-triangle.p[0]));centers.grow((triangle.p[0]+triangle.p[1]+triangle.p[2])/3.);
 }
 g_trace.nodes[(size_t)index].box=bounds;
 if(count<=4){g_trace.nodes[(size_t)index].first=first;g_trace.nodes[(size_t)index].count=count;return index;}
 auto extent=centers.extent();int axis=extent.x>=extent.y&&extent.x>=extent.z?0:extent.y>=extent.z?1:2;
 auto coord=[&](const TraceTriangle& t){Vec3 p=t.p[0]+t.p[1]+t.p[2];return axis==0?p.x:axis==1?p.y:p.z;};
 int half=count/2;std::nth_element(g_trace.triangles.begin()+first,g_trace.triangles.begin()+first+half,g_trace.triangles.begin()+first+count,[&](const TraceTriangle& a,const TraceTriangle& b){return coord(a)<coord(b);});
 int left=buildTraceNode(first,half),right=buildTraceNode(first+half,count-half);g_trace.nodes[(size_t)index].left=left;g_trace.nodes[(size_t)index].right=right;return index;
}
static bool rebuildTraceScene(std::uint64_t sceneKey){
 auto start=std::chrono::steady_clock::now();g_trace.triangles.clear();g_trace.nodes.clear();g_trace.samples=0;g_trace.sceneKey=sceneKey;g_trace.reason.clear();
 g_trace.origin=g_sceneBounds.valid()?g_sceneBounds.center():Vec3{};
 auto srgb=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
 auto append=[&](const Mesh& mesh,const Mat4& world,const Appearance& look,bool flat){
  if(g_trace.triangles.size()+mesh.triCount()>250000){g_trace.reason="250000 display-triangle limit exceeded";return false;}
  Mat4 inverse=world.inverted();auto normal=[&](Vec3 n){return Vec3{inverse.m[0]*n.x+inverse.m[1]*n.y+inverse.m[2]*n.z,inverse.m[4]*n.x+inverse.m[5]*n.y+inverse.m[6]*n.z,inverse.m[8]*n.x+inverse.m[9]*n.y+inverse.m[10]*n.z}.normalized();};
  for(size_t i=0;i+2<mesh.idx.size();i+=3){TraceTriangle triangle;for(int k=0;k<3;++k){size_t vertex=(size_t)mesh.idx[i+k]*3;triangle.p[k]=world.transformPoint({mesh.pos[vertex],mesh.pos[vertex+1],mesh.pos[vertex+2]})-g_trace.origin;
    triangle.n[k]=mesh.nrm.size()>vertex+2?normal({mesh.nrm[vertex],mesh.nrm[vertex+1],mesh.nrm[vertex+2]}):Vec3{};}
   Vec3 face=(triangle.p[1]-triangle.p[0]).cross(triangle.p[2]-triangle.p[0]);if(face.lensq()<1e-20)continue;face=face.normalized();
   for(auto& n:triangle.n)if(flat||n.lensq()<1e-12)n=face;
   triangle.base={srgb(look.baseColor.x),srgb(look.baseColor.y),srgb(look.baseColor.z)};triangle.metallic=(float)look.metallic;triangle.roughness=(float)clamp(look.roughness,.055,1.);g_trace.triangles.push_back(triangle);
  }return true;
 };
 if(!g_doc.stamps.empty()||std::any_of(g_doc.features.begin(),g_doc.features.end(),[](const auto& f){return !f.faceAppearances.empty();})){g_trace.reason="Stamps and face appearances require raster display";return false;}
 if(!g_doc.canvases.empty()){g_trace.reason="Image canvases require raster display";return false;}
 for(size_t i=0;i<g_bodyIds.size();++i){if(!displayInstance(i))continue;const auto* feature=g_doc.find(g_bodyIds[i]);Appearance look=feature?resolvedAppearance(g_doc,*feature):Appearance{};
  if(look.emissionIntensity>0){g_trace.reason="Light up uses raster emission and bloom";return false;}
  if(look.opacity<.999){g_trace.reason="Transparent bodies require raster display";return false;}
  if(!look.texturePath.empty()||!look.textureAsset.empty()){g_trace.reason="Image-textured materials require raster display";return false;}
  if(!look.pattern.empty()){g_trace.reason="Procedural appearance patterns require raster display";return false;}
  if(feature&&feature->type==FeatureType::NotchedTube){auto& cache=g_notchViews.at(feature->id);auto transforms=displayedNotchTransforms(i);for(size_t k=0;k<cache.preview.segments.size();++k)if(!append(cache.preview.segments[k],g_instanceWorld[i]*transforms[k],look,true))return false;}
  else if(feature&&feature->type==FeatureType::SourceTube){auto& cache=g_sourceViews.at(feature->id);auto transforms=displayedSourceTransforms(i);double progress=(int)i==selBody?g_foldProgress:0.;updateSourceHinges(cache,progress);for(size_t k=0;k<cache.preview.rigid.size();++k)if(!append(cache.preview.rigid[k],g_instanceWorld[i]*transforms[k],look,false))return false;for(size_t k=0;k<cache.preview.bends.size();++k)if(!append(progress>0?cache.hinges[k]:cache.flatHinges[k],g_instanceWorld[i]*transforms[k],look,false))return false;}
  else if(feature&&feature->type==FeatureType::Suspension){auto& cache=g_suspViews.at(feature->id);for(auto& part:cache.preview.parts){Appearance partLook=look;partLook.baseColor=part.color;partLook.metallic=part.name.find("tyre")!=std::string::npos?0.:.24;partLook.roughness=part.name.find("tyre")!=std::string::npos?.86:.43;if(!append(part.mesh,g_instanceWorld[i]*part.pose,partLook,false))return false;}}
  else {size_t part=g_instancePart[i];if(part<g_cpuMeshes.size()&&!append(g_cpuMeshes[part],g_instanceWorld[i],look,feature&&(feature->type==FeatureType::Extrude||feature->type==FeatureType::SketchCut)))return false;}
 }
 if(g_trace.triangles.empty()){g_trace.reason="No visible display triangles";return false;}
 buildTraceNode(0,(int)g_trace.triangles.size());g_trace.triangleCount=(int)g_trace.triangles.size();g_trace.nodeCount=(int)g_trace.nodes.size();
 GLint maxTexels=0;glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&maxTexels);if((size_t)maxTexels<g_trace.triangles.size()*8||maxTexels<g_trace.nodeCount*2){g_trace.reason="GPU texture-buffer capacity exceeded";return false;}
 std::vector<float> data;data.reserve(g_trace.triangles.size()*32);auto put=[&](Vec3 v,float w=0){data.insert(data.end(),{(float)v.x,(float)v.y,(float)v.z,w});};
 for(auto& t:g_trace.triangles){put(t.p[0]);put(t.p[1]-t.p[0]);put(t.p[2]-t.p[0]);for(auto n:t.n)put(n);put(t.base,t.metallic);put({t.roughness,0,0});}
 glBindBuffer(GL_TEXTURE_BUFFER,g_trace.triangleBuffer);glBufferData(GL_TEXTURE_BUFFER,(GLsizeiptr)(data.size()*sizeof(float)),data.data(),GL_STATIC_DRAW);glBindTexture(GL_TEXTURE_BUFFER,g_trace.triangleTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,g_trace.triangleBuffer);
 data.clear();data.reserve(g_trace.nodes.size()*8);for(auto& node:g_trace.nodes){
  auto outward=[](Vec3 p,float direction){return Vec3{std::nextafter((float)p.x,direction),std::nextafter((float)p.y,direction),std::nextafter((float)p.z,direction)};};
  put(outward(node.box.mn,-std::numeric_limits<float>::infinity()),(float)(node.count?node.first:node.left));put(outward(node.box.mx,std::numeric_limits<float>::infinity()),(float)(node.count?node.count:node.right));if(!node.count)data.back()=-(float)node.right;
 }
 // Internal nodes use negative right child; leaf .w is the positive count.
 glBindBuffer(GL_TEXTURE_BUFFER,g_trace.nodeBuffer);glBufferData(GL_TEXTURE_BUFFER,(GLsizeiptr)(data.size()*sizeof(float)),data.data(),GL_STATIC_DRAW);glBindTexture(GL_TEXTURE_BUFFER,g_trace.nodeTexture);glTexBuffer(GL_TEXTURE_BUFFER,GL_RGBA32F,g_trace.nodeBuffer);glBindTexture(GL_TEXTURE_BUFFER,0);glBindBuffer(GL_TEXTURE_BUFFER,0);
 g_trace.buildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 if(GLenum error=glGetError()){char detail[96];std::snprintf(detail,sizeof detail,"GPU scene upload failed (OpenGL 0x%X)",(unsigned)error);g_trace.reason=detail;return false;}
 return true;
}
static std::uint64_t traceSceneKey(){
 std::uint64_t key=1469598103934665603ull;traceHashValue(key,g_session.revision);traceHashValue(key,g_displayMeshSerial);traceHashValue(key,g_allSheetsFlat);traceHashValue(key,g_stockMode);traceHashValue(key,g_foldProgress);traceHashValue(key,selBody);
 for(size_t i=0;i<g_bodyIds.size();++i){bool shown=displayInstance(i);traceHashValue(key,shown);if(shown){traceHashValue(key,g_bodyIds[i]);traceHash(key,g_instanceWorld[i].m,sizeof g_instanceWorld[i].m);}}
 return key;
}
static bool drawPathTracing(const Mat4& projection,const Vec3& eye){
 g_trace.active=false;if(!g_pathTracing)return false;if(wireframe){g_trace.reason="Wireframe uses raster display";g_trace.sceneKey=0;g_trace.samples=0;return false;}
 // The raster scene already retains per-part meshes and applies instance
 // matrices. Do not flatten every triangle and rebuild the ray BVH at pointer
 // frequency. Resume progressive lighting once the rigid pose is committed.
 if(g_componentMove.active||g_busy||g_preparingEdit){g_trace.samples=0;return false;}
 if(g_trace.reason=="Wireframe uses raster display")g_trace.reason.clear();if(!initializePathTracer())return false;
 const auto sceneKey=traceSceneKey();if(sceneKey!=g_trace.sceneKey){if(!rebuildTraceScene(sceneKey))return false;}else if(!g_trace.reason.empty())return false;
 int width=std::max(1,viewW),height=std::max(1,viewH);double scale=std::min({1.,1600./width,std::sqrt(1200000./((double)width*height))});width=std::max(1,(int)std::lround(width*scale));height=std::max(1,(int)std::lround(height*scale));
 if(width!=g_trace.width||height!=g_trace.height){g_trace.width=width;g_trace.height=height;g_trace.samples=0;for(GLuint texture:g_trace.accum){glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}}
 CameraFrame frame=cameraFrame(yaw,pitch,cameraRoll);Vec3 relativeEye=eye-g_trace.origin;
 std::uint64_t cameraKey=1469598103934665603ull;for(double v:{relativeEye.x,relativeEye.y,relativeEye.z,yaw,pitch,cameraRoll}){float stable=(float)v;traceHashValue(cameraKey,stable);}for(double v:projection.m){float stable=(float)v;traceHashValue(cameraKey,stable);}traceHashValue(cameraKey,g_cameraOrtho);
 if(cameraKey!=g_trace.cameraKey){g_trace.cameraKey=cameraKey;g_trace.samples=0;}
 if(g_trace.samples<4096){auto start=std::chrono::steady_clock::now();int output=g_trace.samples&1,input=output^1;
  glBindFramebuffer(GL_FRAMEBUFFER,g_trace.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,g_trace.accum[output],0);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){bindViewportOutput059();g_trace.reason="Floating-point accumulation framebuffer unavailable";return false;}
  glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(g_trace.program);studioBasis(g_trace.program,frame);
  glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,g_trace.triangleTexture);glUniform1i(glGetUniformLocation(g_trace.program,"triangles"),0);
  glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_BUFFER,g_trace.nodeTexture);glUniform1i(glGetUniformLocation(g_trace.program,"nodes"),1);
  glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,g_trace.accum[input]);glUniform1i(glGetUniformLocation(g_trace.program,"previous"),2);
  glUniform1i(glGetUniformLocation(g_trace.program,"sampleIndex"),g_trace.samples);glUniform1i(glGetUniformLocation(g_trace.program,"nodeCount"),g_trace.nodeCount);
  glUniform2f(glGetUniformLocation(g_trace.program,"resolution"),(float)width,(float)height);glUniform2f(glGetUniformLocation(g_trace.program,"focal"),(float)projection.m[0],(float)projection.m[5]);
  const double nearPlane=g_cameraOrtho?(projection.m[14]+1.)/projection.m[10]:projection.m[14]/(projection.m[10]-1.);glUniform2f(glGetUniformLocation(g_trace.program,"clipRange"),(float)std::max(0.,nearPlane),(float)g_projectionFar);
  glUniform3f(glGetUniformLocation(g_trace.program,"camera"),(float)relativeEye.x,(float)relativeEye.y,(float)relativeEye.z);glUniform1f(glGetUniformLocation(g_trace.program,"ortho"),g_cameraOrtho?1.f:0.f);
  glUniform1f(glGetUniformLocation(g_trace.program,"rayEpsilon"),(float)std::max(1e-5,g_sceneBounds.diagonal()*2e-6));glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);
  if(GLenum error=glGetError()){char detail[96];std::snprintf(detail,sizeof detail,"GPU path sample failed (OpenGL 0x%X)",(unsigned)error);g_trace.reason=detail;bindViewportOutput059();glViewport(viewX,winH-viewY-viewH,viewW,viewH);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glActiveTexture(GL_TEXTURE0);return false;}
  ++g_trace.samples;
  g_trace.lastSubmitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();bindViewportOutput059();
 }
 glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(g_trace.present);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,g_trace.accum[(g_trace.samples-1)&1]);glUniform1i(glGetUniformLocation(g_trace.present,"image"),0);
 glUniform2f(glGetUniformLocation(g_trace.present,"resolution"),(float)viewW,(float)viewH);glUniform2f(glGetUniformLocation(g_trace.present,"origin"),(float)viewX,(float)(winH-viewY-viewH));glBindVertexArray(quadVao);glDrawArrays(GL_TRIANGLES,0,3);
 glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_BUFFER,0);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_BUFFER,0);
 if(GLenum error=glGetError()){char detail[96];std::snprintf(detail,sizeof detail,"GPU path presentation failed (OpenGL 0x%X)",(unsigned)error);g_trace.reason=detail;return false;}
 g_trace.active=true;return true;
}
