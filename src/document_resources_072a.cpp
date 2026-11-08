#include "ark/document_resources_072a.hpp"
#include "ark/resource_policy_071d.hpp"
#include "ark/json.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace ark {
std::size_t documentReadMemoryBudget072(){
    constexpr std::uint64_t MiB=1024ull*1024ull,GiB=1024ull*MiB;
    const auto hardware=probeResourceHardware071D();
    if(!hardware.totalRamBytes||hardware.availableRamBytes>hardware.totalRamBytes)return 256ull*MiB;
    const auto reserve=std::min(hardware.totalRamBytes/2,std::min<std::uint64_t>(4*GiB,std::max<std::uint64_t>(256*MiB,hardware.totalRamBytes/8)));
    // A reserve must not turn low-memory small reads into a blanket refusal.
    const auto effectiveReserve=std::min(reserve,hardware.availableRamBytes/4);
    const auto headroom=hardware.availableRamBytes-effectiveReserve;
    return (std::size_t)std::min<std::uint64_t>(headroom/2,std::numeric_limits<std::size_t>::max());
}
std::size_t documentCheckedProduct072(std::size_t count,std::size_t bytes){
    if(bytes&&count>std::numeric_limits<std::size_t>::max()/bytes)throw std::runtime_error("DOCUMENT_READ_RESOURCE_PRESSURE: allocation size overflow; document retained");
    return count*bytes;
}
void DocumentReadBudget072::charge(std::size_t bytes){
    if(used>limit||bytes>limit-used)throw std::runtime_error("DOCUMENT_READ_RESOURCE_PRESSURE: estimated decoding allocation exceeds current available-memory headroom; close unused work or retry when memory is available. The existing document is retained");
    used+=bytes;
}
void checkDocumentJsonRead072(const std::string& source,std::size_t maximum){
    DocumentReadBudget072 budget(maximum);
    // Input/output text, string capacities and parse/model working copies.
    budget.charge(documentCheckedProduct072(source.size(),4));
    constexpr auto nodeCost=3*(sizeof(arkjson::Value)+sizeof(std::pair<std::string,arkjson::Value>)+64);
    budget.charge(nodeCost);bool quoted=false,escaped=false;
    for(unsigned char c:source){
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;budget.charge(nodeCost);}
        else if(c=='{'||c=='['||c==','||c==':')budget.charge(nodeCost);
    }
}
}
