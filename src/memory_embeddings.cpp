#include "ark/memory_embeddings.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#define ORT_API_MANUAL_INIT
#include "onnxruntime/onnxruntime_cxx_api.h"
#endif

namespace ark {
std::vector<MemoryEmbedding> MemoryEmbeddingProvider::embedBatch(const std::vector<std::string>& texts) {
    if(texts.size()>16) throw std::runtime_error("Memory embeddings: batch exceeds 16 texts");
    std::vector<MemoryEmbedding> result; result.reserve(texts.size());
    for(const auto& text:texts) result.push_back(embed(text));
    return result;
}
namespace {
using V=arkjson::Value; namespace fs=std::filesystem;
constexpr const char* modelHash="1724c10ba3b33b58afb6ce73bbf3e0528921bd4d4e1bf5a569b6d73f234e72bf";
constexpr const char* vocabHash="fe0fda7c425b48c516fc8f160d594c8022a0808447475c1a7c6d6479763f310c";
constexpr const char* denseHash="0a21b1ce908e772ebf09f93c20ca09524c32706e9918d9c0169a3f0663b191ed";
constexpr const char* runtimeHash="dec964ab1ee36cc9b0ae247d13b376627992fc57dec0454354017ab8fd84f1ea";
constexpr const char* runtimeSharedHash="a2b3a50956aa75a9879c8472bc7df4f7a8072bcd2db19a1b7d988e7688f293ef";
constexpr std::size_t dimensions=512, hiddenDimensions=768, maxTokens=128, overlap=16, maxTextBytes=32768;
const std::string fingerprint=std::string("ark-distiluse-v2:onnx-int8:")+modelHash+":wordpiece-cased:"+vocabHash+":dense-tanh:"+denseHash+":mean-l2:128:overlap16:weighted-window-l2";
void require(bool condition,const std::string& error){if(!condition)throw std::runtime_error("Memory embeddings: "+error);}
#ifdef _WIN32
std::string readFile(const fs::path& path,std::size_t maximum){
    require(fs::is_regular_file(path)&&!fs::is_symlink(path),"missing or non-regular asset "+path.filename().string());
    auto length=fs::file_size(path);require(length>0&&length<=maximum,"invalid asset size "+path.filename().string());
    std::ifstream file(path,std::ios::binary);std::string data((std::size_t)length,'\0');file.read(data.data(),(std::streamsize)length);
    require(file.good(),"cannot read "+path.filename().string());return data;
}
std::string sha256(const std::string& bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA-256 provider failed");
    struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{algorithm,hash};
    DWORD size=0,written=0;require(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof(size),&written,0)>=0,"SHA-256 configuration failed");
    std::vector<unsigned char> object(size),digest(32);require(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0,"SHA-256 allocation failed");
    require(BCryptHashData(hash,(PUCHAR)bytes.data(),(ULONG)bytes.size(),0)>=0&&BCryptFinishHash(hash,digest.data(),32,0)>=0,"SHA-256 calculation failed");
    static const char* hex="0123456789abcdef";std::string result;for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
std::wstring wide(const std::string& text){
    int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0);require(length>0,"text must be valid nonempty UTF-8");
    std::wstring result(length,L'\0');require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),(int)text.size(),result.data(),length)==length,"invalid UTF-8");return result;
}
std::string narrow(const std::wstring& text){
    if(text.empty())return {};int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),(int)text.size(),nullptr,0,nullptr,nullptr);require(length>0,"invalid UTF-16");
    std::string result(length,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),(int)text.size(),result.data(),length,nullptr,nullptr);return result;
}
bool chinese(unsigned cp){return (cp>=0x4e00&&cp<=0x9fff)||(cp>=0x3400&&cp<=0x4dbf)||(cp>=0x20000&&cp<=0x2a6df)||(cp>=0x2a700&&cp<=0x2b73f)||(cp>=0x2b740&&cp<=0x2b81f)||(cp>=0x2b820&&cp<=0x2ceaf)||(cp>=0xf900&&cp<=0xfaff)||(cp>=0x2f800&&cp<=0x2fa1f);}
class WordPiece {
    std::unordered_map<std::string,int64_t> vocab;
public:
    explicit WordPiece(const std::string& bytes){std::istringstream lines(bytes);std::string line;int64_t id=0;while(std::getline(lines,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();vocab.emplace(line,id++);}require(vocab.size()==119547,"unexpected WordPiece vocabulary");require(vocab.at("[CLS]")==101&&vocab.at("[SEP]")==102&&vocab.at("[UNK]")==100&&vocab.at("[PAD]")==0,"unexpected special token IDs");}
    std::vector<int64_t> encode(const std::string& text) const {
        require(!text.empty()&&text.size()<=maxTextBytes&&text.find('\0')==std::string::npos,"text must contain 1..32768 UTF-8 bytes without NUL");
        // The published DistilUSE tokenizer is cased and preserves accents.
        // Do not apply MiniLM uncased preprocessing to these different weights.
        auto characters=wide(text);
        std::vector<std::string> words;std::wstring current;
        auto flush=[&](){if(!current.empty()){words.push_back(narrow(current));current.clear();}};
        for(std::size_t i=0;i<characters.size();++i){wchar_t ch=characters[i];WORD c1=0;GetStringTypeW(CT_CTYPE1,&ch,1,&c1);
            if(ch=='['){bool special=false;for(const auto* token:{L"[UNK]",L"[SEP]",L"[PAD]",L"[CLS]",L"[MASK]"}){std::wstring name(token);if(characters.compare(i,name.size(),name)==0){flush();words.push_back(narrow(name));i+=name.size()-1;special=true;break;}}if(special)continue;}
            if(ch==0xfffd||((c1&C1_CNTRL)&&ch!='\t'&&ch!='\n'&&ch!='\r'))continue;
            if((c1&C1_SPACE)||ch=='\t'||ch=='\n'||ch=='\r'){flush();continue;}
            unsigned cp=(unsigned)ch;std::wstring unit(1,ch);if(ch>=0xd800&&ch<=0xdbff&&i+1<characters.size()){wchar_t second=characters[++i];require(second>=0xdc00&&second<=0xdfff,"invalid Unicode surrogate");unit+=second;cp=0x10000+((ch-0xd800)<<10)+(second-0xdc00);}
            bool punctuation=(cp>=33&&cp<=47)||(cp>=58&&cp<=64)||(cp>=91&&cp<=96)||(cp>=123&&cp<=126)||(c1&C1_PUNCT);
            if(chinese(cp)||punctuation){flush();words.push_back(narrow(unit));}else current+=unit;
        }flush();
        std::vector<int64_t> ids{101};
        for(const auto& word:words){std::vector<std::size_t> boundaries{0};for(std::size_t j=1;j<word.size();++j)if(((unsigned char)word[j]&0xc0)!=0x80)boundaries.push_back(j);boundaries.push_back(word.size());
            if(boundaries.size()>101){ids.push_back(100);continue;}std::vector<int64_t> pieces;std::size_t start=0;bool unknown=false;
            while(start+1<boundaries.size()){std::size_t end=boundaries.size()-1;bool found=false;for(;end>start;--end){auto piece=(start?"##":"")+word.substr(boundaries[start],boundaries[end]-boundaries[start]);auto match=vocab.find(piece);if(match!=vocab.end()){pieces.push_back(match->second);found=true;break;}}if(!found){unknown=true;break;}start=end;}
            if(unknown)ids.push_back(100);else for(auto piece:pieces)ids.push_back(piece);
        }ids.push_back(102);return ids;
    }
};
#endif
class LocalEmbeddings final:public MemoryEmbeddingProvider {
    mutable std::mutex mutex;std::string error;bool ready=false;
#ifdef _WIN32
    HMODULE module=nullptr;
    std::vector<float> denseWeights,denseBias;
    std::unique_ptr<Ort::Env> env;std::unique_ptr<Ort::Session> session;std::unique_ptr<WordPiece> tokenizer;
    void load(const fs::path& assets){
        require(assets.is_absolute(),"asset directory must be absolute");auto directory=fs::canonical(assets);require(fs::is_directory(directory),"asset directory missing");
        require(sha256(readFile(directory/"model_quantized.onnx",160*1024*1024))==modelHash,"model SHA-256 mismatch");auto vocabulary=readFile(directory/"vocab.txt",2*1024*1024);require(sha256(vocabulary)==vocabHash,"vocabulary SHA-256 mismatch");
        auto dense=readFile(directory/"dense.safetensors",2*1024*1024);require(sha256(dense)==denseHash,"projection SHA-256 mismatch");require(dense.size()>8,"projection header missing");std::uint64_t headerLength=0;std::memcpy(&headerLength,dense.data(),8);require(headerLength<=16384&&8+headerLength<=dense.size(),"projection header invalid");V metadata;require(arkjson::parse(dense.substr(8,(std::size_t)headerLength),metadata),"projection metadata invalid");
        auto tensor=[&](const char* key,std::vector<std::size_t> shape){auto entry=metadata.find(key);require(entry&&entry->t==V::Obj,"projection tensor missing");auto dtype=entry->find("dtype"),layout=entry->find("shape"),offsets=entry->find("data_offsets");require(dtype&&dtype->asStr()=="F32"&&layout&&layout->t==V::Arr&&layout->arr.size()==shape.size()&&offsets&&offsets->t==V::Arr&&offsets->arr.size()==2,"projection tensor contract invalid");std::size_t count=1;for(std::size_t i=0;i<shape.size();++i){require(layout->arr[i].asNum()==(double)shape[i],"projection dimensions invalid");count*=shape[i];}auto begin=(std::size_t)offsets->arr[0].asNum(),end=(std::size_t)offsets->arr[1].asNum();require(end>=begin&&end-begin==count*sizeof(float)&&8+headerLength+end<=dense.size(),"projection offsets invalid");std::vector<float> data(count);std::memcpy(data.data(),dense.data()+8+headerLength+begin,count*sizeof(float));return data;};
        denseBias=tensor("linear.bias",{dimensions});denseWeights=tensor("linear.weight",{dimensions,hiddenDimensions});
        require(sha256(readFile(directory/"onnxruntime.dll",32*1024*1024))==runtimeHash,"runtime SHA-256 mismatch");require(sha256(readFile(directory/"onnxruntime_providers_shared.dll",1024*1024))==runtimeSharedHash,"runtime provider SHA-256 mismatch");
        module=LoadLibraryExW((directory/L"onnxruntime.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);require(module!=nullptr,"cannot load pinned ONNX Runtime DLL");
        auto getApi=reinterpret_cast<const OrtApiBase* (ORT_API_CALL*)()>(GetProcAddress(module,"OrtGetApiBase"));require(getApi!=nullptr,"ONNX Runtime API missing");auto api=getApi()->GetApi(ORT_API_VERSION);require(api!=nullptr,"ONNX Runtime API version mismatch");Ort::InitApi(api);
        env=std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR,"ark-local-memory");Ort::SessionOptions options;options.SetIntraOpNumThreads(2);options.SetInterOpNumThreads(1);options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session=std::make_unique<Ort::Session>(*env,(directory/L"model_quantized.onnx").c_str(),options);tokenizer=std::make_unique<WordPiece>(vocabulary);
        require(session->GetInputCount()==2&&session->GetOutputCount()>=1,"unexpected model input/output contract");Ort::AllocatorWithDefaultOptions allocator;for(std::size_t i=0;i<2;++i){auto name=session->GetInputNameAllocated(i,allocator);const char* expected[]={"input_ids","attention_mask"};require(std::string(name.get())==expected[i],"unexpected model input name");}
        auto outputName=session->GetOutputNameAllocated(0,allocator);require(std::string(outputName.get())=="last_hidden_state","unexpected model output name");auto outputType=session->GetOutputTypeInfo(0);auto output=outputType.GetTensorTypeAndShapeInfo();require(output.GetElementType()==ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,"unexpected output type");auto shape=output.GetShape();require(shape.size()==3&&shape[2]==(int64_t)hiddenDimensions,"unexpected model output dimensions");ready=true;
    }
#endif
public:
    explicit LocalEmbeddings(const std::string& path){try{
#ifdef _WIN32
        load(fs::path(wide(path)));
#else
        (void)path;throw std::runtime_error("Memory embeddings: this packaged CPU runtime supports Windows x64 only");
#endif
    }catch(const std::exception& e){error=e.what();ready=false;}}
    ~LocalEmbeddings(){
#ifdef _WIN32
        session.reset();env.reset();tokenizer.reset();if(module)FreeLibrary(module);
#endif
    }
    V health() const override{std::lock_guard lock(mutex);V value=V::makeObj();value.set("ready",V::makeBool(ready));value.set("semantic",V::makeBool(ready));value.set("provider",V::makeStr("onnxruntime-cpu-1.23.2"));value.set("model",V::makeStr("Xenova/distiluse-base-multilingual-cased-v2@cad454171d918d9873a2701ba245054b6c1760dd"));value.set("projection_revision",V::makeStr("bfe45d0732ca50787611c0fe107ba278c7f3f889"));value.set("model_fingerprint",V::makeStr(fingerprint));value.set("dimensions",V::makeNum(dimensions));value.set("window_tokens",V::makeNum(maxTokens));value.set("window_overlap",V::makeNum(overlap));value.set("max_text_bytes",V::makeNum(maxTextBytes));value.set("long_text",V::makeStr("all WordPiece tokens; overlapping windows, masked mean, Dense/Tanh, novel-token-weighted average and L2"));value.set("offline",V::makeBool(true));if(!error.empty())value.set("error",V::makeStr(error));return value;}
    MemoryEmbedding embed(const std::string& text) override{auto result=embedBatch({text});return std::move(result.front());}
    std::vector<MemoryEmbedding> embedBatch(const std::vector<std::string>& texts) override{
        require(texts.size()<=16,"batch exceeds 16 texts");if(texts.empty())return {};std::lock_guard lock(mutex);require(ready,error.empty()?"provider is unavailable":error);
#ifdef _WIN32
        struct Window{std::size_t document;std::vector<int64_t> tokens;float weight;};std::vector<Window> windows;
        for(std::size_t row=0;row<texts.size();++row){auto tokens=tokenizer->encode(texts[row]);std::size_t payload=tokens.size()-2;for(std::size_t start=0;;start+=maxTokens-2-overlap){auto count=std::min(maxTokens-2,payload-start);Window window{row,{101},(float)std::max<std::size_t>(1,start?count-overlap:count)};window.tokens.insert(window.tokens.end(),tokens.begin()+1+start,tokens.begin()+1+start+count);window.tokens.push_back(102);windows.push_back(std::move(window));if(start+count>=payload)break;}}
        std::vector<MemoryEmbedding> result(texts.size());for(auto& value:result){value.modelFingerprint=fingerprint;value.values.assign(dimensions,0);}
        auto normalize=[](std::vector<float>& vector){double norm=0;for(float n:vector){require(std::isfinite(n),"nonfinite model output");norm+=(double)n*n;}require(std::isfinite(norm)&&norm>1e-15,"zero or invalid embedding");norm=std::sqrt(norm);for(auto& n:vector)n=(float)(n/norm);};
        for(std::size_t first=0;first<windows.size();first+=16){std::size_t batch=std::min<std::size_t>(16,windows.size()-first),length=0;for(std::size_t row=0;row<batch;++row)length=std::max(length,windows[first+row].tokens.size());
            std::vector<int64_t> ids(batch*length,0),mask(ids.size(),0);for(std::size_t row=0;row<batch;++row)for(std::size_t j=0;j<windows[first+row].tokens.size();++j){ids[row*length+j]=windows[first+row].tokens[j];mask[row*length+j]=1;}
            std::vector<int64_t> shape{(int64_t)batch,(int64_t)length};auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);std::vector<Ort::Value> inputs;for(auto* data:{&ids,&mask})inputs.push_back(Ort::Value::CreateTensor<int64_t>(memory,data->data(),data->size(),shape.data(),shape.size()));const char* inputNames[]={"input_ids","attention_mask"};const char* outputNames[]={"last_hidden_state"};auto output=session->Run(Ort::RunOptions{nullptr},inputNames,inputs.data(),inputs.size(),outputNames,1);auto actual=output[0].GetTensorTypeAndShapeInfo().GetShape();require(actual==std::vector<int64_t>{(int64_t)batch,(int64_t)length,(int64_t)hiddenDimensions},"inference returned unexpected shape");auto data=output[0].GetTensorData<float>();
            for(std::size_t row=0;row<batch;++row){const auto& window=windows[first+row];std::vector<float> pooled(hiddenDimensions,0);for(std::size_t token=0;token<window.tokens.size();++token)for(std::size_t d=0;d<hiddenDimensions;++d){float n=data[(row*length+token)*hiddenDimensions+d];require(std::isfinite(n),"nonfinite model output");pooled[d]+=n/(float)window.tokens.size();}std::vector<float> projected(dimensions);for(std::size_t d=0;d<dimensions;++d){double sum=denseBias[d];for(std::size_t h=0;h<hiddenDimensions;++h)sum+=(double)denseWeights[d*hiddenDimensions+h]*pooled[h];projected[d]=(float)std::tanh(sum);}normalize(projected);for(std::size_t d=0;d<dimensions;++d)result[window.document].values[d]+=window.weight*projected[d];}
        }for(auto& value:result)normalize(value.values);return result;
#else
        return {};
#endif
    }
};
}
std::shared_ptr<MemoryEmbeddingProvider> makeLocalMemoryEmbeddings(const std::string& path){return std::make_shared<LocalEmbeddings>(path);}
}
