// Included inside ark after the portable XPBD implementation. D3D11 hardware
// only: integration, coloured constraints, contacts and velocity updates run on
// the GPU. CPU work schedules dispatches and reads requested display frames.
#ifdef _WIN32
namespace rubber059c {
template<class T> struct Com {
    T* p=nullptr;
    ~Com(){if(p)p->Release();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    Com(Com&& other)noexcept:p(other.p){other.p=nullptr;}
    Com& operator=(Com&& other)noexcept {if(this!=&other){if(p)p->Release();p=other.p;other.p=nullptr;}return *this;}
    T** put(){if(p){p->Release();p=nullptr;}return &p;}
    T* operator->()const{return p;}
};
void checked(HRESULT hr,const char* action) {
    require(SUCCEEDED(hr),std::string("GPU ")+action+" failed ("+std::to_string((unsigned long)hr)+")");
}
struct F4 {float x=0,y=0,z=0,w=0;};
F4 f4(Vec3 p,double w=0){return {(float)p.x,(float)p.y,(float)p.z,(float)w};}
Vec3 v3(F4 p){return {p.x,p.y,p.z};}
struct GNode {F4 position,velocity,predicted,force;};
struct GLink {unsigned a,b;float rest,compliance;F4 direction;};
struct GPulley {F4 centerRadius,axisSpeed;};
struct GConfig {
    unsigned nodeCount=0,linkCount=0,colorOffset=0,colorCount=0;
    unsigned pulleyCount=0,ground=0,contactStride=0,padding=0;
    float dt=0,decay=0,friction=0,groundZ=0;
    F4 gravityRadius;
};
static_assert(sizeof(GNode)==64&&sizeof(GLink)==32&&sizeof(GPulley)==32&&sizeof(GConfig)==64);
static const char* rubberShader059C=R"HLSL(
struct Node {float4 position,velocity,predicted,force;};
struct Link {uint a,b;float rest,compliance;float4 direction;};
struct Pulley {float4 centerRadius,axisSpeed;};
cbuffer Config:register(b0) {
    uint nodeCount,linkCount,colorOffset,colorCount;
    uint pulleyCount,ground,contactStride,padding;
    float dt,decay,friction,groundZ;
    float4 gravityRadius;
};
StructuredBuffer<Link> links:register(t0);
StructuredBuffer<Pulley> pulleys:register(t1);
RWStructuredBuffer<Node> nodes:register(u0);
RWStructuredBuffer<float> lambda:register(u1);
RWStructuredBuffer<float> contactCorrection:register(u2);
RWStructuredBuffer<uint> failure:register(u3);
void checkFinite(float3 p) {if(any(isnan(p))||any(isinf(p))){uint ignored;InterlockedOr(failure[0],1,ignored);}}
[numthreads(64,1,1)] void integrate(uint3 tid:SV_DispatchThreadID) {
    uint i=tid.x;
    if(i<linkCount)lambda[i]=0;
    if(i>=nodeCount)return;
    for(uint j=0;j<contactStride;++j)contactCorrection[i*contactStride+j]=0;
    Node n=nodes[i];
    if(n.position.w>0) {
        n.velocity.xyz=n.velocity.xyz*decay+(gravityRadius.xyz+n.force.xyz*n.position.w)*dt;
        n.position.xyz+=n.velocity.xyz*dt;checkFinite(n.position.xyz);checkFinite(n.velocity.xyz);
    }
    n.predicted=n.position;nodes[i]=n;
}
[numthreads(64,1,1)] void projectLinks(uint3 tid:SV_DispatchThreadID) {
    if(tid.x>=colorCount)return;
    uint i=colorOffset+tid.x;Link l=links[i];Node a=nodes[l.a],b=nodes[l.b];
    float wa=a.position.w,wb=b.position.w;if(wa+wb==0)return;
    float3 d=a.position.xyz-b.position.xyz;float distance=length(d);
    float3 normal=distance>1e-12?d/distance:l.direction.xyz;
    float alpha=l.compliance/(dt*dt),dl=(-(distance-l.rest)-alpha*lambda[i])/(wa+wb+alpha);
    lambda[i]+=dl;
    if(wa>0){a.position.xyz+=normal*(wa*dl);nodes[l.a]=a;}
    if(wb>0){b.position.xyz-=normal*(wb*dl);nodes[l.b]=b;}
}
float3 pulleyNormal(float3 position,Pulley p,out float radius) {
    float3 delta=position-p.centerRadius.xyz;delta-=p.axisSpeed.xyz*dot(delta,p.axisSpeed.xyz);radius=length(delta);
    return radius>1e-12?delta/radius:normalize(cross(p.axisSpeed.xyz,abs(p.axisSpeed.x)<.8?float3(1,0,0):float3(0,1,0)));
}
[numthreads(64,1,1)] void projectContacts(uint3 tid:SV_DispatchThreadID) {
    uint i=tid.x;if(i>=nodeCount)return;Node n=nodes[i];if(n.position.w==0)return;
    uint base=i*contactStride;
    if(ground) {
        float depth=groundZ+gravityRadius.w-n.position.z;
        if(depth>0){n.position.z+=depth;contactCorrection[base]+=depth;}
    }
    for(uint j=0;j<pulleyCount;++j) {
        Pulley p=pulleys[j];float radius;float3 normal=pulleyNormal(n.position.xyz,p,radius);
        float depth=p.centerRadius.w+gravityRadius.w-radius;
        if(depth>0){n.position.xyz+=normal*depth;contactCorrection[base+j+1]+=depth;}
    }
    nodes[i]=n;
}
float3 contactVelocity(float3 velocity,float3 normal,float3 surface,float correction) {
    float3 relative=velocity-surface;float vn=dot(relative,normal),normalDelta=correction/dt+max(0,-vn);
    if(vn<0)relative-=normal*vn;
    float3 tangent=relative-normal*dot(relative,normal);float speed=length(tangent);
    if(speed>1e-12)relative-=tangent*(min(speed,friction*normalDelta)/speed);
    return relative+surface;
}
[numthreads(64,1,1)] void finish(uint3 tid:SV_DispatchThreadID) {
    uint i=tid.x;if(i>=nodeCount)return;Node n=nodes[i];if(n.position.w==0)return;
    float3 velocity=n.velocity.xyz+(n.position.xyz-n.predicted.xyz)/dt;uint base=i*contactStride;
    if(ground&&n.position.z<=groundZ+gravityRadius.w+1e-7)
        velocity=contactVelocity(velocity,float3(0,0,1),float3(0,0,0),contactCorrection[base]);
    for(uint j=0;j<pulleyCount;++j) {
        Pulley p=pulleys[j];float radius;float3 normal=pulleyNormal(n.position.xyz,p,radius);
        if(radius<=p.centerRadius.w+gravityRadius.w+1e-7)
            velocity=contactVelocity(velocity,normal,cross(p.axisSpeed.xyz,normal)*(p.centerRadius.w*p.axisSpeed.w),contactCorrection[base+j+1]);
    }
    n.velocity.xyz=velocity;checkFinite(n.position.xyz);checkFinite(velocity);nodes[i]=n;
}
)HLSL";
struct GBuffer {Com<ID3D11Buffer> buffer;Com<ID3D11ShaderResourceView> srv;Com<ID3D11UnorderedAccessView> uav;};
struct Gpu {
    std::mutex mutex;
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;
    Com<ID3D11ComputeShader> integrateShader,linksShader,contactsShader,finishShader;
    Com<ID3D11Buffer> constants;
    GBuffer links;std::string linkBytes,linkKey,adapter,reason;bool tried=false;
    void init() {
        if(device.p&&context.p&&integrateShader.p&&linksShader.p&&contactsShader.p&&finishShader.p&&constants.p)return;
        if(tried)throw std::runtime_error(reason);tried=true;
        try {
            D3D_FEATURE_LEVEL required=D3D_FEATURE_LEVEL_11_0,actual;
            checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&required,1,D3D11_SDK_VERSION,device.put(),&actual,context.put()),"hardware device creation");
            Com<IDXGIDevice> dxgi;checked(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.put())),"adapter query");
            Com<IDXGIAdapter> a;checked(dxgi->GetAdapter(a.put()),"adapter");DXGI_ADAPTER_DESC desc{};checked(a->GetDesc(&desc),"adapter description");
            for(auto ch:desc.Description){if(!ch)break;adapter.push_back(ch<128?(char)ch:'?');}
            auto shader=[&](const char* entry,Com<ID3D11ComputeShader>& result) {
                Com<ID3DBlob> code,errors;
                HRESULT hr=D3DCompile(rubberShader059C,std::strlen(rubberShader059C),"ARK rubber XPBD 059C",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put());
                if(FAILED(hr))throw std::runtime_error("RUBBER_SIMULATION: GPU shader "+std::string(entry)+": "+(errors.p?std::string((char*)errors->GetBufferPointer(),errors->GetBufferSize()):"compile failed"));
                checked(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,result.put()),"shader creation");
            };
            shader("integrate",integrateShader);shader("projectLinks",linksShader);shader("projectContacts",contactsShader);shader("finish",finishShader);
            D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(GConfig);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            checked(device->CreateBuffer(&cb,nullptr,constants.put()),"constant buffer");reason.clear();
        } catch(const std::exception& e) {reason=e.what();throw;}
    }
    GBuffer buffer(size_t size,size_t stride,const void* data,bool writable) {
        require(size>0&&size<=64*1024*1024&&size%stride==0,"GPU buffer capacity");GBuffer result;
        D3D11_BUFFER_DESC d{};d.ByteWidth=(UINT)size;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|(writable?D3D11_BIND_UNORDERED_ACCESS:0);
        d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;d.StructureByteStride=(UINT)stride;
        D3D11_SUBRESOURCE_DATA init{};init.pSysMem=data;checked(device->CreateBuffer(&d,data?&init:nullptr,result.buffer.put()),"structured buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;sv.Format=DXGI_FORMAT_UNKNOWN;sv.Buffer.NumElements=(UINT)(size/stride);
        checked(device->CreateShaderResourceView(result.buffer.p,&sv,result.srv.put()),"SRV");
        if(writable) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};uv.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uv.Format=DXGI_FORMAT_UNKNOWN;uv.Buffer.NumElements=sv.Buffer.NumElements;
            checked(device->CreateUnorderedAccessView(result.buffer.p,&uv,result.uav.put()),"UAV");
        }
        return result;
    }
    void unbind() {
        ID3D11ShaderResourceView* srvs[2]{};ID3D11UnorderedAccessView* uavs[4]{};
        context->CSSetShaderResources(0,2,srvs);context->CSSetUnorderedAccessViews(0,4,uavs,nullptr);
    }
    void dispatch(ID3D11ComputeShader* shader,GBuffer& states,GBuffer& lambda,GBuffer& contacts,GBuffer& pulleys,GBuffer& failure,const GConfig& config,unsigned count) {
        unbind();context->UpdateSubresource(constants.p,0,nullptr,&config,0,0);
        ID3D11ShaderResourceView* srvs[]{links.srv.p,pulleys.srv.p};
        ID3D11UnorderedAccessView* uavs[]{states.uav.p,lambda.uav.p,contacts.uav.p,failure.uav.p};
        context->CSSetShaderResources(0,2,srvs);context->CSSetUnorderedAccessViews(0,4,uavs,nullptr);
        context->CSSetConstantBuffers(0,1,&constants.p);context->CSSetShader(shader,nullptr,0);context->Dispatch((count+63)/64,1,1);unbind();
    }
};
Gpu& gpu() {static Gpu instance;return instance;}
RubberDynamicsResult runGpu(const std::vector<RubberNode>& nodes,const Topology& graph,const RubberDynamicsOptions& o) {
    const auto started=std::chrono::steady_clock::now();auto control=captureNativeRequestControl();auto& g=gpu();
    std::unique_lock lock(g.mutex,std::defer_lock);
    while(!lock.try_lock()){cancel(control);std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    cancel(control);g.init();
    std::vector<GLink> packedLinks;
    for(const auto& link:graph.links) {
        const auto& l=link.value;
        require(l.compliance==0||(float)l.compliance>0,"link compliance underflows GPU float32; select cpu explicitly");
        double exactLength=(nodes[l.a].position-nodes[l.b].position).len();
        double packedLength=(v3(f4(nodes[l.a].position))-v3(f4(nodes[l.b].position))).len();
        require(std::abs(packedLength-exactLength)<=l.restLength*.005,"GPU coordinate precision is insufficient for a link; move analysis near its origin or select cpu");
        packedLinks.push_back({l.a,l.b,(float)l.restLength,(float)l.compliance,f4(link.initialDirection)});
    }
    if(packedLinks.empty())packedLinks.push_back({0,0,0,0,{}});
    const std::string bytes(reinterpret_cast<const char*>(packedLinks.data()),packedLinks.size()*sizeof(GLink));
    const std::string key=iterationSha256(bytes);bool hit=g.links.buffer.p&&g.linkBytes==bytes;
    size_t uploaded=hit?0:bytes.size();
    if(!hit){g.links=g.buffer(bytes.size(),sizeof(GLink),bytes.data(),false);g.linkBytes=bytes;g.linkKey=key;}
    std::vector<GNode> packed;for(const auto& n:nodes)packed.push_back({f4(n.position,invMass(n)),f4(n.velocity),f4(n.position),f4(n.force)});
    std::vector<GPulley> packedPulleys;for(const auto& p:o.pulleys)packedPulleys.push_back({f4(p.center,p.radius),f4(p.axis,p.angularVelocity)});
    if(packedPulleys.empty())packedPulleys.emplace_back();
    auto states=g.buffer(packed.size()*sizeof(GNode),sizeof(GNode),packed.data(),true);
    auto multipliers=g.buffer(packedLinks.size()*sizeof(float),sizeof(float),nullptr,true);
    auto contacts=g.buffer(nodes.size()*(o.pulleys.size()+1)*sizeof(float),sizeof(float),nullptr,true);
    auto pulleyBuffer=g.buffer(packedPulleys.size()*sizeof(GPulley),sizeof(GPulley),packedPulleys.data(),false);
    unsigned zero=0;auto failure=g.buffer(sizeof zero,sizeof zero,&zero,true);
    D3D11_BUFFER_DESC sd{};sd.ByteWidth=(UINT)(packed.size()*sizeof(GNode));sd.Usage=D3D11_USAGE_STAGING;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Com<ID3D11Buffer> readback;checked(g.device->CreateBuffer(&sd,nullptr,readback.put()),"readback");
    D3D11_BUFFER_DESC fd=sd;fd.ByteWidth=sizeof zero;Com<ID3D11Buffer> failureReadback;checked(g.device->CreateBuffer(&fd,nullptr,failureReadback.put()),"failure readback");
    D3D11_BUFFER_DESC ld=sd;ld.ByteWidth=(UINT)(packedLinks.size()*sizeof(float));Com<ID3D11Buffer> lambdaReadback;
    checked(g.device->CreateBuffer(&ld,nullptr,lambdaReadback.put()),"link force readback");
    RubberDynamicsResult out;std::vector<Vec3> initial;for(const auto& n:nodes)initial.push_back(n.position);out.frames.push_back(initial);out.times.push_back(0);
    GConfig config;config.nodeCount=(unsigned)nodes.size();config.linkCount=(unsigned)graph.links.size();config.pulleyCount=(unsigned)o.pulleys.size();
    config.ground=o.ground;config.contactStride=config.pulleyCount+1;config.friction=(float)o.friction;config.groundZ=(float)o.groundZ;config.gravityRadius=f4(o.gravity,o.contactRadius);
    unsigned dispatches=0,steps=0;double time=0,lastDt=0;size_t samples=(size_t)std::ceil(o.duration*o.outputFps);
    auto dispatch=[&](ID3D11ComputeShader* shader,unsigned count){g.dispatch(shader,states,multipliers,contacts,pulleyBuffer,failure,config,count);++dispatches;};
    for(size_t sample=1;sample<=samples;++sample) {
        const double target=std::min(o.duration,double(sample)/o.outputFps);
        while(time<target-1e-12) {
            cancel(control);double dt=std::min(o.step,target-time);config.dt=(float)dt;config.decay=(float)std::exp(-o.damping*dt);
            dispatch(g.integrateShader.p,std::max(config.nodeCount,config.linkCount));
            for(unsigned iteration=0;iteration<o.iterations;++iteration) {
                for(size_t color=0;color+1<graph.offsets.size();++color) {
                    config.colorOffset=graph.offsets[color];config.colorCount=graph.offsets[color+1]-graph.offsets[color];dispatch(g.linksShader.p,config.colorCount);
                }
                if(o.ground||!o.pulleys.empty())dispatch(g.contactsShader.p,config.nodeCount);
            }
            dispatch(g.finishShader.p,config.nodeCount);time+=dt;lastDt=dt;++steps;
        }
        cancel(control);g.context->CopyResource(readback.p,states.buffer.p);D3D11_MAPPED_SUBRESOURCE mapped{};
        checked(g.context->Map(readback.p,0,D3D11_MAP_READ,0,&mapped),"synchronized frame readback");
        std::memcpy(packed.data(),mapped.pData,sd.ByteWidth);g.context->Unmap(readback.p,0);
        std::vector<Vec3> frame,velocities;
        for(size_t i=0;i<nodes.size();++i) {
            Vec3 p=nodes[i].fixed?nodes[i].position:v3(packed[i].position),v=nodes[i].fixed?Vec3{}:v3(packed[i].velocity);
            require(finite(p)&&finite(v),"GPU solver became nonfinite; reduce step/check material and loads");frame.push_back(p);velocities.push_back(v);
        }
        out.frames.push_back(std::move(frame));out.finalVelocities=std::move(velocities);out.times.push_back(time);
    }
    g.context->CopyResource(failureReadback.p,failure.buffer.p);D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(g.context->Map(failureReadback.p,0,D3D11_MAP_READ,0,&mapped),"failure readback");unsigned failed=*(const unsigned*)mapped.pData;g.context->Unmap(failureReadback.p,0);
    require(failed==0,"GPU solver encountered a nonfinite intermediate; reduce step/check material and loads");
    g.context->CopyResource(lambdaReadback.p,multipliers.buffer.p);
    checked(g.context->Map(lambdaReadback.p,0,D3D11_MAP_READ,0,&mapped),"synchronized link force readback");
    out.finalLinkForces_N.resize(graph.links.size());const float* finalLambda=static_cast<const float*>(mapped.pData);
    for(size_t j=0;j<graph.links.size();++j)out.finalLinkForces_N[graph.links[j].originalIndex]=-double(finalLambda[j])/(lastDt*lastDt);
    g.context->Unmap(lambdaReadback.p,0);
    out.diagnostics=V::makeObj();auto& d=out.diagnostics;
    d.set("backend",V::makeStr("D3D11 hardware compute / float32"));d.set("adapter",V::makeStr(g.adapter));
    d.set("gpu_constraint_cache_hit",V::makeBool(hit));d.set("gpu_constraint_sha256",V::makeStr(key));
    d.set("gpu_constraint_uploaded_bytes",V::makeNum(double(uploaded)));d.set("gpu_constraint_resident_bytes",V::makeNum(double(bytes.size())));
    d.set("gpu_dynamic_buffer_bytes",V::makeNum(double(packed.size()*sizeof(GNode)+packedLinks.size()*sizeof(float)+nodes.size()*(o.pulleys.size()+1)*sizeof(float)+packedPulleys.size()*sizeof(GPulley)+sizeof zero)));
    d.set("dispatches",V::makeNum(dispatches));d.set("steps",V::makeNum(steps));
    d.set("transfer_inclusive_ms",V::makeNum(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()));
    return out;
}
}
#endif
