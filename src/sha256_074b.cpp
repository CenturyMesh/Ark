#include "ark/sha256_074b.hpp"
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif
namespace ark {
bool platformSha256074B(std::string_view bytes,std::string& digest){
#ifdef _WIN32
    // A background episode can first hash a large value while main is already
    // draining that queue during static destruction. Registering another static
    // destructor here can deadlock the CRT exit lock against queue.join(). Keep
    // this one algorithm handle for process lifetime; Windows releases it at
    // process exit. Per-call hash objects still have ordinary bounded RAII.
    static const BCRYPT_ALG_HANDLE provider=[](){BCRYPT_ALG_HANDLE handle=nullptr;return BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0?nullptr:handle;}();
    struct Hash {BCRYPT_HASH_HANDLE handle=nullptr;~Hash(){if(handle)BCryptDestroyHash(handle);}} hash;
    if(!provider||BCryptCreateHash(provider,&hash.handle,nullptr,0,nullptr,0,0)<0)return false;
    for(size_t offset=0;offset<bytes.size();){const auto count=static_cast<ULONG>(std::min<size_t>(bytes.size()-offset,1u<<30));
        if(BCryptHashData(hash.handle,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data()+offset)),count,0)<0)return false;offset+=count;}
    unsigned char result[32]{};if(BCryptFinishHash(hash.handle,result,sizeof result,0)<0)return false;
    static constexpr char hex[]="0123456789abcdef";std::string encoded(64,'0');for(size_t i=0;i<32;++i){encoded[2*i]=hex[result[i]>>4];encoded[2*i+1]=hex[result[i]&15];}digest=std::move(encoded);return true;
#else
    (void)bytes;(void)digest;return false;
#endif
}
}
