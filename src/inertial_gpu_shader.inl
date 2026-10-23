// Original ARK DirectCompute kernels. All integration, convex SAT, impulses and
// joint iterations execute on the hardware device. Host schedules dispatch only.
static const char* inertialShader=R"HLSL(
struct Body {float4 p,q,v,w,i0,i1,i2,inv0,inv1,inv2,force,torque;uint4 shape,edges;};
struct Contact {float4 normalDepth,pointTarget;};
struct Joint {uint4 ids;float4 anchorA,anchorB,axisA,axisB;};
StructuredBuffer<Body> inputBodies:register(t0);
StructuredBuffer<float4> mesh:register(t1);
StructuredBuffer<Contact> contacts:register(t2);
StructuredBuffer<Joint> joints:register(t3);
RWStructuredBuffer<Body> outputBodies:register(u0);
RWStructuredBuffer<Contact> outputContacts:register(u1);
RWStructuredBuffer<uint> failure:register(u2);
cbuffer Config:register(b0){uint count,jointCount,doContacts,ground;float dt,restitution,friction,groundZ;float4 gravity;};
float3 rotated(float4 q,float3 v){return v+2*cross(q.xyz,cross(q.xyz,v)+q.w*v);}
float4 conjugate(float4 q){return float4(-q.xyz,q.w);}
float4 qmul(float4 a,float4 b){return float4(a.w*b.xyz+b.w*a.xyz+cross(a.xyz,b.xyz),a.w*b.w-dot(a.xyz,b.xyz));}
float3 iw(Body b,float3 v,bool invert){if(b.p.w==0)return 0;v=rotated(conjugate(b.q),v);float3 r=invert?float3(dot(b.inv0.xyz,v),dot(b.inv1.xyz,v),dot(b.inv2.xyz,v)):float3(dot(b.i0.xyz,v),dot(b.i1.xyz,v),dot(b.i2.xyz,v));return rotated(b.q,r);}
float3 worldPoint(Body b,uint index){return b.p.xyz+rotated(b.q,mesh[b.shape.x+index].xyz);}
float effective(Body b,float3 r,float3 n){return b.p.w+dot(n,cross(iw(b,cross(r,n),true),r));}
float2 project(Body b,float3 n){float2 interval=float2(3.4e38,-3.4e38);for(uint i=0;i<b.shape.y;++i){float v=dot(worldPoint(b,i),n);interval=float2(min(interval.x,v),max(interval.y,v));}return interval;}
float3 support(Body b,float3 n){float hi=project(b,n).y;float3 sum=0;uint hits=0;for(uint i=0;i<b.shape.y;++i){float3 v=worldPoint(b,i);if(hi-dot(v,n)<1e-6){sum+=v;++hits;}}return sum/max(hits,1);}
bool clipSegment(Body b,float3 p,float3 q,out float3 first,out float3 last){float lo=0,hi=1;float3 delta=q-p;first=last=0;for(uint k=0;k<b.shape.w;++k){float3 n=rotated(b.q,mesh[b.shape.z+k].xyz);float2 bounds=project(b,n);float start=dot(p,n),slope=dot(delta,n);if(abs(slope)<1e-12){if(start<bounds.x-1e-7||start>bounds.y+1e-7)return false;continue;}float2 t=(bounds-start)/slope;lo=max(lo,min(t.x,t.y));hi=min(hi,max(t.x,t.y));if(lo>hi)return false;}first=p+delta*lo;last=p+delta*hi;return true;}
bool clippedEdge(Body a,Body b,uint index,out float3 first,out float3 last){float4 indices=mesh[a.edges.z+index];return clipSegment(b,worldPoint(a,(uint)indices.x),worldPoint(a,(uint)indices.y),first,last);}
bool seenWitness(Body a,Body b,uint upto,float3 candidate){for(uint k=0;k<upto;++k){float3 first,last;if(clippedEdge(a,b,k,first,last)&&(distance(first,candidate)<1e-6||distance(last,candidate)<1e-6))return true;}return false;}
void addWitnesses(Body a,Body b,bool second,inout float3 sum,inout uint hits){for(uint k=0;k<a.edges.w;++k){float3 first,last;if(clippedEdge(a,b,k,first,last)){if(!seenWitness(a,b,k,first)&&(!second||!seenWitness(b,a,b.edges.w,first))){sum+=first;++hits;}if(distance(first,last)>=1e-6&&!seenWitness(a,b,k,last)&&(!second||!seenWitness(b,a,b.edges.w,last))){sum+=last;++hits;}}}}
float3 witness(Body a,Body b){float3 sum=0;uint hits=0;addWitnesses(a,b,false,sum,hits);addWitnesses(b,a,true,sum,hits);if(hits==0){uint prior;InterlockedOr(failure[0],1,prior);}return sum/max(hits,1);}
bool axis(Body a,Body b,float3 direction,inout float depth,inout float3 normal){float len=length(direction);if(len<1e-7)return true;float3 n=direction/len;float2 pa=project(a,n),pb=project(b,n);float d1=pb.y-pa.x,d2=pa.y-pb.x;if(d1<0||d2<0)return false;if(min(d1,d2)<depth){depth=min(d1,d2);normal=d1<d2?n:-n;}return true;}
[numthreads(64,1,1)] void integrate(uint3 id:SV_DispatchThreadID){uint k=id.x;if(k>=count)return;Body b=inputBodies[k];if(b.p.w>0){float3 a=gravity.xyz+b.force.xyz*b.p.w;b.p.xyz+=b.v.xyz*dt+a*(.5*dt*dt);b.v.xyz+=a*dt;float3 alpha=iw(b,b.torque.xyz-cross(b.w.xyz,iw(b,b.w.xyz,false)),true);Body mid=b;mid.w.xyz=b.w.xyz+alpha*(.5*dt);float halfAngle=length(b.w.xyz)*dt*.5;float4 halfQ=float4(0,0,0,1);if(halfAngle>1e-12)halfQ=float4(normalize(b.w.xyz)*sin(halfAngle*.5),cos(halfAngle*.5));mid.q=normalize(qmul(halfQ,b.q));b.w.xyz+=iw(mid,mid.torque.xyz-cross(mid.w.xyz,iw(mid,mid.w.xyz,false)),true)*dt;float angle=length(mid.w.xyz)*dt;float4 dq=float4(0,0,0,1);if(angle>1e-12)dq=float4(normalize(mid.w.xyz)*sin(angle*.5),cos(angle*.5));b.q=normalize(qmul(dq,b.q));}outputBodies[k]=b;}
[numthreads(32,1,1)] void collide(uint3 id:SV_DispatchThreadID){uint k=id.x;if(k>=count*(count+1))return;uint ia=k/(count+1),ib=k%(count+1);if(ib<count&&ia>ib)return;Body a=inputBodies[ia];Contact c;c.normalDepth=float4(0,0,0,-1);c.pointTarget=0;outputContacts[k]=c;if(ib<count&&ia<ib)outputContacts[ib*(count+1)+ia]=c;if(ia==ib||(ib==count&&(!ground||a.p.w==0))||(ib<count&&!doContacts)){outputContacts[k]=c;return;}float3 normal=0,p=0;float depth=3.4e38;float3 relative=0;
if(ib==count){p=support(a,float3(0,0,-1));depth=groundZ-p.z;if(depth<0){outputContacts[k]=c;return;}normal=float3(0,0,1);relative=a.v.xyz+cross(a.w.xyz,p-a.p.xyz);}
else {Body b=inputBodies[ib];if(a.p.w+b.p.w==0){outputContacts[k]=c;return;}
if(!axis(a,b,float3(1,0,0),depth,normal)||!axis(a,b,float3(0,1,0),depth,normal)||!axis(a,b,float3(0,0,1),depth,normal)){outputContacts[k]=c;return;}
for(uint i=0;i<a.shape.w;++i)if(!axis(a,b,rotated(a.q,mesh[a.shape.z+i].xyz),depth,normal)){outputContacts[k]=c;return;}
for(uint j=0;j<b.shape.w;++j)if(!axis(a,b,rotated(b.q,mesh[b.shape.z+j].xyz),depth,normal)){outputContacts[k]=c;return;}
for(uint e=0;e<a.edges.y;++e)for(uint f=0;f<b.edges.y;++f)if(!axis(a,b,cross(rotated(a.q,mesh[a.edges.x+e].xyz),rotated(b.q,mesh[b.edges.x+f].xyz)),depth,normal)){outputContacts[k]=c;return;}
p=witness(a,b);relative=a.v.xyz+cross(a.w.xyz,p-a.p.xyz)-b.v.xyz-cross(b.w.xyz,p-b.p.xyz);}
c.normalDepth=float4(normal,depth);c.pointTarget=float4(p,max(-restitution*min(dot(relative,normal),0),.15*max(depth-1e-5,0)/dt));outputContacts[k]=c;if(ib<count){c.normalDepth.xyz=-c.normalDepth.xyz;outputContacts[ib*(count+1)+ia]=c;}}
uint degree(uint body){uint total=0;for(uint j=0;j<=count;++j)if(contacts[body*(count+1)+j].normalDepth.w>=0)++total;for(uint k=0;k<jointCount;++k)if(joints[k].ids.x==body||joints[k].ids.y==body)++total;return max(total,1);}
[numthreads(64,1,1)] void solve(uint3 id:SV_DispatchThreadID){uint ia=id.x;if(ia>=count)return;Body a=inputBodies[ia];if(a.p.w==0){outputBodies[ia]=a;return;}float3 dv=0,dw=0;uint da=degree(ia);
for(uint ib=0;ib<=count;++ib){Contact c=contacts[ia*(count+1)+ib];if(c.normalDepth.w<0)continue;float3 n=c.normalDepth.xyz,ra=c.pointTarget.xyz-a.p.xyz,rb=0,relative=a.v.xyz+cross(a.w.xyz,ra);float denom=effective(a,ra,n);Body b=(Body)0;uint db=1;if(ib<count){b=inputBodies[ib];rb=c.pointTarget.xyz-b.p.xyz;relative-=b.v.xyz+cross(b.w.xyz,rb);denom+=effective(b,rb,n);db=degree(ib);}if(denom<=1e-18)continue;float relax=1.0/max(da,db),impulse=max(0,(c.pointTarget.w-dot(relative,n))/denom)*relax;float3 p=n*impulse,tangent=relative-n*dot(relative,n);float speed=length(tangent);if(speed>1e-9){float3 t=tangent/speed;float td=effective(a,ra,t);if(ib<count)td+=effective(b,rb,t);p-=t*min(friction*impulse,speed/td*relax);}dv+=p*a.p.w;dw+=iw(a,cross(ra,p),true);}
for(uint k=0;k<jointCount;++k){Joint j=joints[k];if(j.ids.x!=ia&&j.ids.y!=ia)continue;bool first=j.ids.x==ia;uint ib=first?j.ids.y:j.ids.x;Body b=inputBodies[ib];float3 ra=rotated(a.q,first?j.anchorA.xyz:j.anchorB.xyz),rb=rotated(b.q,first?j.anchorB.xyz:j.anchorA.xyz),error=b.p.xyz+rb-a.p.xyz-ra,relative=a.v.xyz+cross(a.w.xyz,ra)-b.v.xyz-cross(b.w.xyz,rb);float relax=.6/max(da,degree(ib));
for(uint dim=0;dim<3;++dim){float3 n=dim==0?float3(1,0,0):dim==1?float3(0,1,0):float3(0,0,1);float denom=effective(a,ra,n)+effective(b,rb,n);if(denom>1e-18){float3 p=n*((.15*dot(error,n)/dt-dot(relative,n))/denom*relax);dv+=p*a.p.w;dw+=iw(a,cross(ra,p),true);}}
if(j.ids.z!=0){float3 aa=rotated(a.q,first?j.axisA.xyz:j.axisB.xyz),ab=rotated(b.q,first?j.axisB.xyz:j.axisA.xyz),target=cross(aa,ab)*(.15/dt),relativeW=a.w.xyz-b.w.xyz,t1=normalize(cross(aa,abs(aa.x)<.8?float3(1,0,0):float3(0,1,0))),t2=cross(aa,t1);for(uint dim=0;dim<2;++dim){float3 n=dim==0?t1:t2;float denom=dot(n,iw(a,n,true)+iw(b,n,true));if(denom>1e-18)dw+=iw(a,n*(dot(target-relativeW,n)/denom*relax),true);}}}
a.v.xyz+=dv;a.w.xyz+=dw;outputBodies[ia]=a;}
)HLSL";
