#pragma once
#include <cstddef>
#include <string>
namespace ark {
// Resource protection for loading/expanding external data. These are NOT
// authoring, history, transaction or per-edit size quotas. Resident writes grow
// normally; allocation/storage failures still preserve the previous document.
std::size_t documentReadMemoryBudget072();
std::size_t documentCheckedProduct072(std::size_t count,std::size_t bytes);
struct DocumentReadBudget072 {
    std::size_t limit,used=0;
    explicit DocumentReadBudget072(std::size_t maximum=documentReadMemoryBudget072()):limit(maximum){}
    void charge(std::size_t bytes);
};
// Conservative allocation preflight before the existing strict JSON parser.
// Syntax, duplicates and nesting remain the parser's responsibility.
void checkDocumentJsonRead072(const std::string& source,std::size_t memoryBudget=documentReadMemoryBudget072());
}
