struct SectionPart062 {Mesh mesh;MeshBVH bvh{mesh};GPUMesh gpu,cap;};
struct SectionState062 {
    bool active=false,drawing=false,first=false;int x=0,y=0;u64 revision=0,epoch=0;
    Vec3 origin,normal{0,0,1};std::string label;double area=0;
    std::map<size_t,SectionPart062> parts;
    std::vector<bool> hidden;std::set<std::string> families;
};
static SectionState062 g_section062;
static void endSection062(){for(auto& entry:g_section062.parts){releaseMesh(entry.second.gpu);releaseMesh(entry.second.cap);}g_section062={};}
static void validateSection062(){if((g_section062.active||g_section062.drawing)&&(g_section062.revision!=g_session.revision||g_section062.epoch!=g_documentEpoch||measurementPreviewActive()||(g_section062.active&&(g_section062.hidden!=g_hiddenBodies||g_section062.families!=g_familyFilter054B)))){endSection062();g_status="Section view cleared: document or display pose changed";}}
static bool sectionActive062(){validateSection062();return g_section062.active;}
