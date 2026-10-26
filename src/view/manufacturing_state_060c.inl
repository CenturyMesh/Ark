struct ManufacturingPanel060 {
    HWND window=nullptr,list=nullptr,summary=nullptr;
    std::string name,selectedNode,message="Select an exact part and create a CNC or additive plan.",slicePath;
    std::vector<std::string> nodeIds;bool active=false,playing=false;
    size_t frame=0;double elapsed=0,speed=10,uiTick=0;u64 revision=~u64{},epoch=~u64{};
    std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);
    std::future<ManufacturingStudy> worker;ManufacturingStudy study;
    std::future<arkjson::Value> slicer;
};
static ManufacturingPanel060 g_manufacturing060;
static bool manufacturingAction060(int);
static void advanceManufacturing060(double);
static void drawManufacturingPreview060();
static void shutdownManufacturing060(){*g_manufacturing060.cancel=true;if(g_manufacturing060.worker.valid())g_manufacturing060.worker.wait();if(g_manufacturing060.slicer.valid())g_manufacturing060.slicer.wait();}
