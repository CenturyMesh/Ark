#include "ark/memory_embeddings.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static double cosine(const ark::MemoryEmbedding& a,const ark::MemoryEmbedding& b){check(a.modelFingerprint==b.modelFingerprint,"model identity mismatch");double sum=0;for(std::size_t i=0;i<a.values.size();++i)sum+=(double)a.values[i]*b.values[i];return sum;}
int main(int argc,char** argv){try{
    auto missing=ark::makeLocalMemoryEmbeddings(std::filesystem::absolute("absent-semantic-assets").string());check(!missing->health().find("ready")->b,"missing assets must reject");bool rejected=false;try{missing->embed("revision guard");}catch(const std::exception&){rejected=true;}check(rejected,"missing model must not fabricate vectors");
    if(argc<2){std::cout<<"Missing-assets rejection verified; pass absolute asset directory to exercise real CPU model.\n";return 0;}
    const auto start=std::chrono::steady_clock::now();auto provider=ark::makeLocalMemoryEmbeddings(argv[1]);std::cout<<provider->health().dump()<<'\n';check(provider->health().find("ready")->b,"pinned CPU model failed to load");
    {const auto fixturePath=std::filesystem::path(__FILE__).parent_path()/"memory_embeddings_reference.json";std::ifstream file(fixturePath,std::ios::binary);check(file.good(),"independent reference fixture missing");std::string bytes((std::istreambuf_iterator<char>(file)),{});arkjson::Value fixture;check(arkjson::parse(bytes,fixture),"reference fixture JSON invalid");const auto* records=fixture.find("records");check(records&&records->t==arkjson::Value::Arr&&records->arr.size()==4,"reference fixture records missing");for(const auto& row:records->arr){auto actual=provider->embed(row.find("text")->asStr());const auto* expected=row.find("vector");check(expected&&expected->arr.size()==actual.values.size(),"reference dimensions differ");double maximum=0;for(std::size_t i=0;i<actual.values.size();++i)maximum=std::max(maximum,std::abs((double)actual.values[i]-expected->arr[i].asNum()));check(maximum<1e-5,"native tokenizer/model/projection differs from independent reference");}std::cout<<"Independent full-vector conformance: 4/4, absolute error < 1e-5\n";}
    auto documents=provider->embedBatch({
        "Optimistic concurrency requires expected_revision. Reject outdated mutations atomically to prevent lost updates.",
        "The assembly root is grounded. A rigid mate locks all relative movement between its components.",
        "A sheet metal flange uses bend radius and material thickness. The flat pattern uses the neutral axis allowance.",
        "The embedding model is stored locally. Offline inference never sends design data to a cloud server."
    });
    auto queries=provider->embedBatch({
        "How do we stop simultaneous writers from overwriting each other's edits?",
        "How can two assembled parts remain fixed relative to one another?",
        "How is the developed length calculated when folding a thin steel plate?",
        "Does semantic recall need an internet connection or upload my files?"
    });
    for(std::size_t q=0;q<queries.size();++q){std::size_t best=0;double score=-2;for(std::size_t d=0;d<documents.size();++d){double n=cosine(queries[q],documents[d]);std::cout<<"query "<<q<<" document "<<d<<" cosine "<<n<<'\n';if(n>score){score=n;best=d;}}check(best==q,"real paraphrase retrieval ranked wrong document");}
    auto text=provider->embedBatch({"REVISÃO ATÓMICA", "REVISÃO ATÓMICA"});check(cosine(text[0],text[1])>0.9999,"UTF-8 cased preprocessing is unstable");
    auto portugueseDocuments=provider->embedBatch({"A publicação atómica rejeita uma alteração quando a revisão esperada está desatualizada.","Uma junta rígida mantém as peças da montagem fixas umas em relação às outras.","O desenvolvimento da chapa inclui o raio de dobragem e a espessura do material."});
    auto portugueseQueries=provider->embedBatch({"Como evitar que dois autores substituam as alterações um do outro?","Como bloquear o movimento relativo dos componentes unidos?","Como calcular o comprimento da placa metálica antes de dobrar?"});
    unsigned portugueseTop1=0;for(std::size_t q=0;q<portugueseQueries.size();++q){std::size_t best=0,rank=1;double score=-2,target=cosine(portugueseQueries[q],portugueseDocuments[q]);for(std::size_t d=0;d<portugueseDocuments.size();++d){double n=cosine(portugueseQueries[q],portugueseDocuments[d]);std::cout<<"PT query "<<q<<" document "<<d<<" cosine "<<n<<'\n';if(n>score){score=n;best=d;}if(d!=q&&n>target)++rank;}portugueseTop1+=(best==q);std::cout<<"PT query "<<q<<" expected document rank "<<rank<<'\n';}
    for(const auto& vector:documents){check(vector.values.size()==512,"embedding dimensions invalid");double norm=0;for(float n:vector.values){check(std::isfinite(n),"nonfinite vector");norm+=(double)n*n;}check(std::abs(norm-1)<1e-5,"L2 normalization invalid");}
    auto a=std::async(std::launch::async,[&]{return provider->embed("atomic publication");});auto b=std::async(std::launch::async,[&]{return provider->embed("atomic publication");});check(cosine(a.get(),b.get())>0.9999,"concurrent inference drift");
    rejected=false;try{provider->embed(std::string(32769,'a'));}catch(const std::exception&){rejected=true;}check(rejected,"oversized input must reject");
    rejected=false;try{provider->embed(std::string("\xc3\x28",2));}catch(const std::exception&){rejected=true;}check(rejected,"invalid UTF-8 must reject");
    rejected=false;try{provider->embedBatch(std::vector<std::string>(17,"part"));}catch(const std::exception&){rejected=true;}check(rejected,"oversized batch must reject");
    auto longVector=provider->embed(std::string(16000,'a'));check(longVector.values.size()==512,"long input produced invalid dimensions");
    std::string prefix;for(int i=0;i<50;++i)prefix+="The local project archive stores historical meeting notes. ";const auto tail="The furnace temperature controller uses a thermocouple sensor to regulate heat treatment.";
    auto tailProof=provider->embedBatch({prefix,prefix+tail,tail});double before=cosine(tailProof[0],tailProof[2]),after=cosine(tailProof[1],tailProof[2]);std::cout<<"long-tail cosine prefix="<<before<<" with-tail="<<after<<'\n';check(after>before+1e-4,"long-tail content was ignored");
    if(argc>2){arkjson::Value vectors=arkjson::Value::makeArr();for(const auto& phrase:std::vector<std::string>{"REVISÃO ATÓMICA", "Atomic publication uses expected_revision.","中文零件 [MASK] assembly_rigid"}){auto vector=provider->embed(phrase);arkjson::Value row=arkjson::Value::makeObj(),numbers=arkjson::Value::makeArr();row.set("text",arkjson::Value::makeStr(phrase));for(auto n:vector.values)numbers.arr.push_back(arkjson::Value::makeNum(n));row.set("vector",numbers);vectors.arr.push_back(row);}std::ofstream file(argv[2],std::ios::binary);file<<vectors.dump();check(file.good(),"cannot write reference vectors");}
    std::cout<<"Quality pilot: English top1 4/4; Portuguese top1 "<<portugueseTop1<<"/3 (measured quality, not universal acceptance). Native normalization, concurrency, long-tail, missing assets and bounded input contracts passed. Total ms "<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
