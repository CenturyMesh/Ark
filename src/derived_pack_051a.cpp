#include "ark/derived_pack.hpp"
// The frame-header inspection API is stable within the vendored, statically linked zstd.
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace ark {
namespace {
constexpr char packMagic[]="ARK-DERIVED-PACK1\n";
constexpr size_t magicBytes=sizeof packMagic-1;
// Measured on a 294-body document: level 6 with long-distance matching packs
// 127 MiB of entries into 4.9 MiB in about 0.3 s and decodes in 0.05 s.
constexpr int packLevel=6;
// 128 MiB is zstd's default decoder window limit; packs never need a larger one.
constexpr unsigned packWindowLog=27;
constexpr size_t maxSplicesPerEntry=4096;
constexpr size_t maxBodyBytes=magicBytes+10+maxDocumentDerivedBytes+maxDocumentDerivedEntries*(64+30);
constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[noreturn]] void fail(const std::string& message){throw std::runtime_error("ARK derived pack: "+message);}
bool hexKey(const std::string& key){return key.size()==64&&key.find_first_not_of("0123456789abcdef")==std::string::npos;}
int sextet(unsigned char c){
    if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;
    if(c=='+')return 62;if(c=='/')return 63;return -1;
}
void appendBase64(std::string& out,const char* data,size_t size){
    size_t i=0;for(;i+3<=size;i+=3){const unsigned v=(unsigned char)data[i]<<16|(unsigned char)data[i+1]<<8|(unsigned char)data[i+2];
        out+=alphabet[v>>18];out+=alphabet[(v>>12)&63];out+=alphabet[(v>>6)&63];out+=alphabet[v&63];}
    if(size-i==1){const unsigned v=(unsigned char)data[i]<<16;out+=alphabet[v>>18];out+=alphabet[(v>>12)&63];out+="==";}
    else if(size-i==2){const unsigned v=(unsigned char)data[i]<<16|(unsigned char)data[i+1]<<8;out+=alphabet[v>>18];out+=alphabet[(v>>12)&63];out+=alphabet[(v>>6)&63];out+='=';}
}
size_t encodedLength(size_t raw){return (raw+2)/3*4;}
// Accepts only padded base64 that re-encodes to the same characters.
bool canonicalBase64(const char* text,size_t size,std::string& raw){
    if(!size||size%4)return false;raw.clear();raw.reserve(size/4*3);
    for(size_t i=0;i<size;i+=4){
        const bool last=i+4==size;const int a=sextet(text[i]),b=sextet(text[i+1]);if(a<0||b<0)return false;
        if(last&&text[i+2]=='='){if(text[i+3]!='='||(b&15))return false;raw+=(char)(a<<2|b>>4);break;}
        const int c=sextet(text[i+2]);if(c<0)return false;
        if(last&&text[i+3]=='='){if(c&3)return false;raw+=(char)(a<<2|b>>4);raw+=(char)((b&15)<<4|c>>2);break;}
        const int d=sextet(text[i+3]);if(d<0)return false;
        raw+=(char)(a<<2|b>>4);raw+=(char)((b&15)<<4|c>>2);raw+=(char)((c&3)<<6|d);
    }
    std::string again;again.reserve(size);appendBase64(again,raw.data(),raw.size());return again.size()==size&&again.compare(0,size,text,size)==0;
}
bool base64Char(char c){return sextet((unsigned char)c)>=0;}
void varint(std::string& out,uint64_t n){do{unsigned char c=(unsigned char)(n&127);n>>=7;out+=(char)(c|(n?128:0));}while(n);}

struct Splice {size_t offset=0,encoded=0;std::string raw;};
struct Plan {const DocumentDerivedPayload* record=nullptr;std::vector<Splice> splices;std::string header;size_t bodyBytes=0;};
// A splice is a JSON string value `:"<base64>"` long enough to matter.
std::vector<Splice> findSplices(const std::string& entry){
    std::vector<Splice> found;size_t from=0;
    while(found.size()<maxSplicesPerEntry){
        const auto colon=entry.find(":\"",from);if(colon==std::string::npos)break;
        const size_t start=colon+2;size_t end=start;while(end<entry.size()&&base64Char(entry[end]))++end;
        size_t stop=end;while(stop<entry.size()&&stop-end<2&&entry[stop]=='=')++stop;
        Splice splice;
        if(stop<entry.size()&&entry[stop]=='"'&&stop-start>=derivedPackMinimumSplice&&canonicalBase64(entry.data()+start,stop-start,splice.raw)){
            splice.offset=start;splice.encoded=stop-start;found.push_back(std::move(splice));from=stop+1;
        }else from=start;
    }
    return found;
}
Plan plan(const DocumentDerivedPayload& record){
    Plan p;p.record=&record;p.splices=findSplices(record.bytes);
    p.header=record.key;varint(p.header,record.bytes.size());varint(p.header,p.splices.size());
    size_t cursor=0,removed=0,raw=0;
    for(const auto& splice:p.splices){varint(p.header,splice.offset-cursor);varint(p.header,splice.raw.size());cursor=splice.offset+splice.encoded;removed+=splice.encoded;raw+=splice.raw.size();}
    p.bodyBytes=p.header.size()+record.bytes.size()-removed+raw;return p;
}

struct Compressor {
    ZSTD_CCtx* cctx=ZSTD_createCCtx();std::string& out;size_t limit;std::vector<char> buffer=std::vector<char>(ZSTD_CStreamOutSize());
    Compressor(std::string& target,size_t storedLimit):out(target),limit(storedLimit){if(!cctx)fail("cannot allocate the zstd compressor");}
    ~Compressor(){ZSTD_freeCCtx(cctx);}
    void set(ZSTD_cParameter parameter,int value){const size_t result=ZSTD_CCtx_setParameter(cctx,parameter,value);if(ZSTD_isError(result))fail(std::string("zstd parameter: ")+ZSTD_getErrorName(result));}
    // Returns false as soon as the compressed bytes exceed the stored limit.
    bool feed(const char* data,size_t size,ZSTD_EndDirective mode){
        ZSTD_inBuffer input{data,size,0};
        for(;;){
            ZSTD_outBuffer output{buffer.data(),buffer.size(),0};const size_t remaining=ZSTD_compressStream2(cctx,&output,&input,mode);
            if(ZSTD_isError(remaining))fail(std::string("zstd compression: ")+ZSTD_getErrorName(remaining));
            if(output.pos>limit-std::min(limit,out.size()))return false;out.append(buffer.data(),output.pos);
            if(mode==ZSTD_e_end?remaining==0:input.pos==input.size)return true;
        }
    }
};
bool compress(const std::vector<Plan>& plans,size_t count,size_t storedLimit,std::string& stored,size_t& fedEntries){
    stored.clear();fedEntries=0;std::string prefix(packMagic,magicBytes);varint(prefix,count);
    uint64_t total=prefix.size();for(size_t i=0;i<count;++i)total+=plans[i].bodyBytes;
    unsigned window=10;while(window<packWindowLog&&(uint64_t(1)<<window)<total)++window;
    Compressor zstd(stored,storedLimit);
    zstd.set(ZSTD_c_compressionLevel,packLevel);zstd.set(ZSTD_c_windowLog,(int)window);zstd.set(ZSTD_c_enableLongDistanceMatching,1);
    zstd.set(ZSTD_c_checksumFlag,1);zstd.set(ZSTD_c_contentSizeFlag,1);
    if(ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(zstd.cctx,total)))fail("cannot pledge the pack size");
    if(!zstd.feed(prefix.data(),prefix.size(),ZSTD_e_continue))return false;
    for(size_t i=0;i<count;++i){
        const auto& p=plans[i];const auto& bytes=p.record->bytes;
        if(!zstd.feed(p.header.data(),p.header.size(),ZSTD_e_continue))return false;size_t cursor=0;
        for(const auto& splice:p.splices){
            if(!zstd.feed(bytes.data()+cursor,splice.offset-cursor,ZSTD_e_continue)||!zstd.feed(splice.raw.data(),splice.raw.size(),ZSTD_e_continue))return false;
            cursor=splice.offset+splice.encoded;
        }
        if(!zstd.feed(bytes.data()+cursor,bytes.size()-cursor,ZSTD_e_continue))return false;fedEntries=i+1;
    }
    return zstd.feed(nullptr,0,ZSTD_e_end);
}

struct Cursor {
    const std::string& body;size_t at=0;
    const char* take(size_t size){if(size>body.size()-at)fail("truncated pack body");const char* p=body.data()+at;at+=size;return p;}
    uint64_t number(){uint64_t result=0;for(unsigned i=0;i<10;++i){const unsigned char c=(unsigned char)*take(1);if(i==9&&(c&254))fail("varint overflow");result|=uint64_t(c&127)<<(i*7);if(!(c&128)){if(i&&!c)fail("noncanonical varint");return result;}}fail("varint overflow");}
};
} // namespace

std::string derivedPackManifest(const DocumentDerivedPayloads& records,size_t count){
    std::string text="ARK-DERIVED-MANIFEST1\n";count=std::min(count,records.size());
    for(size_t i=0;i<count;++i){text+=records[i].key;text+=' ';text+=std::to_string(records[i].bytes.size());text+=' ';text+=documentContainerDigest(records[i].bytes);text+='\n';}
    return documentContainerDigest(text);
}

std::string encodeDerivedPack(const DocumentDerivedPayloads& records,size_t storedLimit,DerivedPackReport* report){
    if(records.size()>maxDocumentDerivedEntries)fail("too many entries");
    std::set<std::string> keys;size_t decoded=0;
    for(const auto& record:records){
        if(!hexKey(record.key)||!keys.insert(record.key).second)fail("invalid or duplicate entry key");
        if(record.bytes.empty()||record.bytes.size()>maxDocumentDerivedEntryBytes||record.bytes.size()>maxDocumentDerivedBytes-decoded)fail("entry exceeds the derived capacity");
        decoded+=record.bytes.size();
    }
    std::vector<Plan> plans;plans.reserve(records.size());for(const auto& record:records)plans.push_back(plan(record));
    std::string stored;size_t count=records.size(),fed=0;
    while(count&&!compress(plans,count,storedLimit,stored,fed)){
        // Over the stored capacity: keep the entries fed before the overflow, or
        // 90% when only the final flush overflowed, and retry in document order.
        count=std::min(count-1,fed<count?fed:count*9/10);
    }
    if(!count)stored.clear();
    if(report){
        DerivedPackReport r;r.entries=count;r.storedBytes=stored.size();
        for(size_t i=0;i<records.size();++i){
            if(i<count){r.decodedBytes+=records[i].bytes.size();r.splicedStrings+=plans[i].splices.size();for(const auto& s:plans[i].splices)r.splicedBytes+=s.raw.size();}
            else{++r.omittedEntries;r.omittedBytes+=records[i].bytes.size();}
        }
        *report=r;
    }
    return stored;
}

DocumentDerivedPayloads decodeDerivedPack(const std::string& stored,DerivedPackReport* report){
    if(stored.empty()||stored.size()>maxDocumentDerivedStoredBytes)fail("stored size out of bounds");
    const size_t frame=ZSTD_findFrameCompressedSize(stored.data(),stored.size());
    if(ZSTD_isError(frame)||frame!=stored.size())fail("expected exactly one complete zstd frame");
    ZSTD_FrameHeader header{};if(ZSTD_getFrameHeader(&header,stored.data(),stored.size())!=0||header.frameType!=ZSTD_frame)fail("invalid zstd frame header");
    if(header.frameContentSize==ZSTD_CONTENTSIZE_UNKNOWN||header.frameContentSize>maxBodyBytes)fail("pack size missing or over capacity");
    if(header.windowSize>(uint64_t(1)<<packWindowLog)||!header.checksumFlag)fail("unsupported zstd window or missing checksum");
    std::string body((size_t)header.frameContentSize,'\0');
    {
        std::unique_ptr<ZSTD_DCtx,size_t(*)(ZSTD_DCtx*)> dctx(ZSTD_createDCtx(),ZSTD_freeDCtx);if(!dctx)fail("cannot allocate the zstd decompressor");
        const size_t produced=ZSTD_decompressDCtx(dctx.get(),body.data(),body.size(),stored.data(),stored.size());
        if(ZSTD_isError(produced))fail(std::string("zstd: ")+ZSTD_getErrorName(produced));if(produced!=body.size())fail("pack size mismatch");
    }
    Cursor in{body};if(std::string(in.take(magicBytes),magicBytes)!=packMagic)fail("unknown pack format");
    const auto count=in.number();if(count>maxDocumentDerivedEntries)fail("too many entries");
    DocumentDerivedPayloads records;records.reserve((size_t)count);std::set<std::string> keys;size_t decoded=0,spliced=0,splicedBytes=0;
    for(uint64_t n=0;n<count;++n){
        DocumentDerivedPayload record;record.key.assign(in.take(64),64);if(!hexKey(record.key)||!keys.insert(record.key).second)fail("invalid or duplicate entry key");
        const auto length=in.number();if(!length||length>maxDocumentDerivedEntryBytes||length>maxDocumentDerivedBytes-decoded)fail("entry exceeds the derived capacity");
        const auto splices=in.number();if(splices>maxSplicesPerEntry)fail("too many splices");
        std::vector<std::pair<uint64_t,uint64_t>> table;table.reserve((size_t)splices);uint64_t literal=0;
        for(uint64_t i=0;i<splices;++i){
            const auto before=in.number(),raw=in.number();
            if(!raw||raw>length||encodedLength((size_t)raw)<derivedPackMinimumSplice)fail("invalid splice");
            if(before>length-literal||encodedLength((size_t)raw)>length-literal-before)fail("splices exceed the entry length");
            literal+=before+encodedLength((size_t)raw);table.emplace_back(before,raw);
        }
        record.bytes.reserve((size_t)length);
        for(const auto& [before,raw]:table){record.bytes.append(in.take((size_t)before),(size_t)before);appendBase64(record.bytes,in.take((size_t)raw),(size_t)raw);splicedBytes+=(size_t)raw;}
        const size_t tail=(size_t)length-record.bytes.size();record.bytes.append(in.take(tail),tail);
        decoded+=record.bytes.size();spliced+=table.size();records.push_back(std::move(record));
    }
    if(in.at!=body.size())fail("trailing pack bytes");
    if(report){DerivedPackReport r;r.entries=records.size();r.decodedBytes=decoded;r.storedBytes=stored.size();r.splicedStrings=spliced;r.splicedBytes=splicedBytes;*report=r;}
    return records;
}

} // namespace ark
