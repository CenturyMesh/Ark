// UI-thread measurements only. No prompts, geometry or provider data retained.
using RealtimeClock030=std::chrono::steady_clock;
static double realtimeMs030(RealtimeClock030::time_point from,RealtimeClock030::time_point to=RealtimeClock030::now()){
    return std::chrono::duration<double,std::milli>(to-from).count();
}
struct ProgressTiming030 {double captureMs=0,workerMs=0,resumeMs=0;};
struct ToolTiming030 {std::string tool;double queueMs=0,hostMs=0;ProgressTiming030 progress;};
static ProgressTiming030 g_progressTiming030;
static std::deque<ToolTiming030> g_toolTimings030;
static void rememberToolTiming030(ToolTiming030 sample){
    if(g_toolTimings030.size()==128)g_toolTimings030.pop_front();
    g_toolTimings030.push_back(std::move(sample));
}
