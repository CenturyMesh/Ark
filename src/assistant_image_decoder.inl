// Keep the implementation outside namespace ark: upstream includes C/C++ headers.
// The decoder never opens paths or enables unrelated raster/HDR formats.
namespace {
constexpr std::size_t portableImageAllocationLimit=192*1024*1024;
struct alignas(std::max_align_t) PortableImageAllocation {std::size_t bytes;};
thread_local std::size_t portableImageAllocated=0;
void* portableImageMalloc(std::size_t bytes){
    if(bytes>portableImageAllocationLimit-sizeof(PortableImageAllocation))return nullptr;
    const auto total=bytes+sizeof(PortableImageAllocation);if(total>portableImageAllocationLimit-portableImageAllocated)return nullptr;
    auto* block=static_cast<PortableImageAllocation*>(std::malloc(total));if(!block)return nullptr;block->bytes=total;portableImageAllocated+=total;return block+1;
}
void portableImageFree(void* pointer){if(!pointer)return;auto* block=static_cast<PortableImageAllocation*>(pointer)-1;portableImageAllocated-=block->bytes;std::free(block);}
void* portableImageRealloc(void* pointer,std::size_t bytes){
    if(!pointer)return portableImageMalloc(bytes);if(!bytes){portableImageFree(pointer);return nullptr;}
    auto* old=static_cast<PortableImageAllocation*>(pointer)-1;const auto oldBytes=old->bytes;
    if(bytes>portableImageAllocationLimit-sizeof(PortableImageAllocation))return nullptr;const auto total=bytes+sizeof(PortableImageAllocation);
    if(total>portableImageAllocationLimit-(portableImageAllocated-oldBytes))return nullptr;
    auto* block=static_cast<PortableImageAllocation*>(std::realloc(old,total));if(!block)return nullptr;block->bytes=total;portableImageAllocated=portableImageAllocated-oldBytes+total;return block+1;
}
}
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(n) portableImageMalloc(n)
#define STBI_REALLOC(p,n) portableImageRealloc(p,n)
#define STBI_FREE(p) portableImageFree(p)
#include "stb/stb_image.h"
