#pragma once
#include <memory>
#include <string>
#include <vector>
namespace ark {
// ARKLIB01 is a deterministic container for original compressed resource bytes.
// It is not a CAD document, component authority or executable plugin format.
class ResourceLibrary {
public:
    explicit ResourceLibrary(const std::string& path);
    bool contains(const std::string& name) const;
    std::vector<unsigned char> read(const std::string& name) const;
    std::vector<std::string> names() const;
private:
    struct Data;
    std::shared_ptr<const Data> data_;
};
}
