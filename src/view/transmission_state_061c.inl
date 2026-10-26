struct TransmissionSuite061 {
    HWND window=nullptr,list=nullptr,report=nullptr;
    arkjson::Value result,candidates;
    std::vector<u64> mateIds;int selected=0;
    bool candidatesShown=false,stale=false;
    std::string message="Inspect the assembly, edit a transmission mate, or search a design.";
    u64 revision=~u64{},epoch=~u64{};
    std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);
    std::future<arkjson::Value> worker;
};
static TransmissionSuite061 g_transmission061;
static bool transmissionAction061(int);
static void advanceTransmission061();
static void stopTransmission061(){*g_transmission061.cancel=true;if(g_transmission061.worker.valid())g_transmission061.worker.wait();}
