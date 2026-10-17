#include "ark/project_memory.hpp"
#include "ark/memory_embeddings.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using V=arkjson::Value;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
const V& field(const V& value,const char* name){const auto* item=value.find(name);if(!item)throw std::runtime_error(std::string("required receipt field missing: ")+name+" in "+value.dump());return *item;}
struct Corpus {std::string name;std::vector<std::string> documents,queries;};
V measure(ark::ProjectMemory& memory,const Corpus& corpus){
    V results=V::makeArr();unsigned top1=0;
    for(std::size_t i=0;i<corpus.queries.size();++i){
        V args=V::makeObj();args.set("text",V::makeStr(corpus.queries[i]));args.set("limit",V::makeNum(12));
        auto receipt=memory.call("search",args,corpus.name,"synthetic-quality-reader");
        check(field(receipt,"semantic_used").b,"real provider was not used by core retrieval");
        check(field(receipt,"retrieval_mode").asStr()=="hybrid_fts5_cosine","core mislabeled semantic retrieval");
        check(receipt.find("semantic_error")==nullptr,"core semantic inference failed");
        const auto& rows=field(receipt,"records");std::size_t targetRank=0;double targetCosine=-2;
        for(std::size_t r=0;r<rows.arr.size();++r){const auto& row=rows.arr[r];if(field(row,"key").asStr()=="record-"+std::to_string(i)){targetRank=r+1;targetCosine=field(row,"cosine_similarity").asNum();}}
        check(targetRank>0,"expected technical decision absent from real semantic candidates");
        top1+=(targetRank==1);V result=V::makeObj();result.set("query",V::makeStr(corpus.queries[i]));result.set("target_key",V::makeStr("record-"+std::to_string(i)));result.set("target_rank",V::makeNum((double)targetRank));result.set("target_cosine",V::makeNum(targetCosine));results.arr.push_back(result);
    }
    V out=V::makeObj();out.set("corpus",V::makeStr(corpus.name));out.set("top1",V::makeNum(top1));out.set("queries",V::makeNum((double)corpus.queries.size()));out.set("results",results);return out;
}
}
int main(int argc,char** argv){try{
    check(argc>=2,"usage: ark-project-memory-semantic-tests ABSOLUTE_ASSET_DIRECTORY [FRESH_OUTPUT_DIRECTORY]");
    auto provider=ark::makeLocalMemoryEmbeddings(argv[1]);check(field(provider->health(),"ready").b,"real local model unavailable");
    auto directory=argc>=3?std::filesystem::path(argv[2]):std::filesystem::temp_directory_path()/("ark-semantic-test-"+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    directory=std::filesystem::absolute(directory);check(!std::filesystem::exists(directory),"test requires a fresh synthetic output directory");
    ark::ProjectMemoryConfig config;config.rootDirectory=directory.string();config.embeddings=provider;
    const std::vector<Corpus> corpora={
        {"synthetic-semantic-en",{
            "Optimistic concurrency requires expected_revision. Reject outdated mutations atomically to prevent lost updates.",
            "The assembly root is grounded. A rigid mate locks all relative movement between its components.",
            "A sheet metal flange uses bend radius and material thickness. The flat pattern uses the neutral axis allowance.",
            "The embedding model is stored locally. Offline inference never sends design data to a cloud server."
        },{
            "How do we stop simultaneous writers from overwriting each other's edits?",
            "How can two assembled parts remain fixed relative to one another?",
            "How is the developed length calculated when folding a thin steel plate?",
            "Does semantic recall need an internet connection or upload my files?"
        }},
        {"synthetic-semantic-pt",{
            "A publicação atómica rejeita uma alteração quando a revisão esperada está desatualizada.",
            "Uma junta rígida mantém as peças da montagem fixas umas em relação às outras.",
            "O desenvolvimento da chapa inclui o raio de dobragem e a espessura do material."
        },{
            "Como evitar que dois autores substituam as alterações um do outro?",
            "Como bloquear o movimento relativo dos componentes unidos?",
            "Como calcular o comprimento da placa metálica antes de dobrar?"
        }}
    };
    V report=V::makeObj(),runs=V::makeArr();report.set("provider",provider->health());report.set("scope",V::makeStr("synthetic real-model native persistence and compression comparison; no general quality/cost claim"));
    {
        ark::ProjectMemory memory(config);
        for(const auto& corpus:corpora){
            for(std::size_t i=0;i<corpus.documents.size();++i){V args=V::makeObj();args.set("key",V::makeStr("record-"+std::to_string(i)));args.set("title",V::makeStr("Technical decision"));args.set("content",V::makeStr(corpus.documents[i]));args.set("kind",V::makeStr("decision"));args.set("source",V::makeStr("synthetic-corpus"));args.set("source_fingerprint",V::makeStr(std::string(64,'a')));args.set("expected_revision",V::makeNum(0));memory.call("store",args,corpus.name,"synthetic-quality-writer");}
            V run=V::makeObj();run.set("before",measure(memory,corpus));V compression=V::makeObj(),keys=V::makeArr(),revisions=V::makeObj();for(std::size_t i=0;i<corpus.documents.size();++i){auto key="record-"+std::to_string(i);keys.arr.push_back(V::makeStr(key));revisions.set(key,V::makeNum(1));}compression.set("keys",keys);compression.set("expected_revisions",revisions);compression.set("max_cosine_loss",V::makeNum(0.02));auto compressed=memory.call("compress",compression,corpus.name,"synthetic-quality-writer");check(field(compressed,"vector_bytes_after").asNum()<field(compressed,"vector_bytes_before").asNum(),"real vectors were not compressed");check(field(compressed,"max_measured_cosine_loss").asNum()<=0.02,"real compression exceeded accepted loss");run.set("compression",compressed);run.set("after",measure(memory,corpus));runs.arr.push_back(run);
        }
        auto isolated=memory.call("search",V::makeObj(),"different-synthetic-project","synthetic-quality-reader");check(field(isolated,"records").arr.empty(),"project isolation failed with real vectors");
    }
    {ark::ProjectMemory reopened(config);for(const auto& corpus:corpora){for(std::size_t i=0;i<corpus.documents.size();++i){V selector=V::makeObj();selector.set("key",V::makeStr("record-"+std::to_string(i)));auto receipt=reopened.call("get",selector,corpus.name,"synthetic-quality-reader");const auto& row=field(receipt,"records").arr.front();check(field(row,"revision").asNum()==2,"compressed memory revision did not persist");check(field(row,"content").asStr()==corpus.documents[i],"compression changed original text");}}}
    report.set("runs",runs);std::cout<<report.dump()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
