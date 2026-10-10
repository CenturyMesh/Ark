#include "ark/agent.hpp"
#include "ark/pricing.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace ark;using V=arkjson::Value;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double a,double b,const char* text){check(std::abs(a-b)<=1e-9*std::max(1.,std::abs(b)),text);}
V parse(const char* text){V value;check(arkjson::parse(text,value),"JSON fixture");return value;}
V call(AgentSession& session,const char* name,const char* args){auto response=session.call(name,parse(args));check(!response.find("isError")->b,response.dump().c_str());return *response.find("structuredContent");}
void physics(){
    AgentSession s;call(s,"doc_apply",R"({"commands":[{"op":"material.set","name":"Steel","physical":{"young_modulus_Pa":200000000000,"yield_strength_Pa":250000000}}]})");
    auto report=call(s,"physics_inspect",R"({"beam_test":{"material":"Steel","support":"cantilever_tip","length_mm":1000,"second_moment_mm4":1000000,"extreme_fiber_mm":25,"force_N":1000,"buckling":{"minimum_second_moment_mm4":1000000,"area_mm2":1000,"effective_length_factor":1,"compression_N":10000}},"weld_test":{"effective_throat_mm":3,"effective_length_mm":100,"force_N":30000,"allowable_stress_Pa":150000000}})");
    check(!report.find("mass_checked")->b,"analytical tests do not invent mass");auto& beam=*report.find("beam_test");near(beam.find("max_deflection_mm")->num,5./3,"cantilever analytic deflection");near(beam.find("max_bending_stress_Pa")->num,25000000,"bending units");near(beam.find("yield_ratio")->num,10,"declared yield ratio");near(beam.find("buckling")->find("euler_critical_load_N")->num,K_PI*K_PI*200000,"Euler critical load");check(!beam.find("design_qualified")->b&&!beam.find("geometry_linked")->b,"analytical evidence qualified honestly");
    auto& weld=*report.find("weld_test");near(weld.find("average_stress_Pa")->num,100000000,"weld average stress");near(weld.find("required_effective_throat_mm")->num,2,"weld throat requirement");
    auto simply=call(s,"physics_inspect",R"({"beam_test":{"material":"Steel","support":"simply_supported_center","length_mm":1000,"second_moment_mm4":1000000,"extreme_fiber_mm":25,"force_N":-1000}})");near(simply.find("beam_test")->find("max_deflection_mm")->num,5./48,"support boundary coefficient");
    for(const char* bad:{R"({"beam_test":{"material":"Steel","support":"invented","length_mm":1,"second_moment_mm4":1,"extreme_fiber_mm":1,"force_N":1}})",R"({"weld_test":{"effective_throat_mm":0,"effective_length_mm":10,"force_N":1}})",R"({"weld_test":{"effective_throat_mm":3,"effective_length_mm":10,"force_N":1,"torque_Nm":1000}})",R"({"weld_test":{"effective_throat_mm":1e-300,"effective_length_mm":1e-300,"force_N":1e300}})"})check(s.call("physics_inspect",parse(bad)).find("isError")->b,"invalid/overflow analytical case rejected");
    auto before=s.revision;auto reject=s.call("doc_apply",parse(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"trial","expr":1}],"checks":[{"query":{"tool":"physics_inspect","args":{"weld_test":{"effective_throat_mm":1,"effective_length_mm":1,"force_N":1000}},"path":"weld_test.average_stress_Pa"},"op":"le","value":100}]})"));check(reject.find("isError")->b&&s.revision==before,"structural check prevents bad commit");
}
void pricing(){
    std::string md="### Standard pricing data\n\n| Model | Short context input | Short context cached input | Short context cache writes | Short context output | Long context input | Long context cached input | Long context cache writes | Long context output |\n| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n| model-a | $2.00 | $0.10 | $2.50 | $10.00 | $4.00 | $0.20 | $5.00 | $15.00 |\n\n### Batch pricing data\n| model-a | $999 |\n";
    auto catalog=parseOpenAiPricingMarkdown(md);check(catalog.models.size()==1&&catalog.error.empty(),"only explicit Standard table parsed");near(catalog.models[0].outputPerMillion,10,"standard not batch");
    Document d;UsageReceipt receipt;receipt.model="model-a";receipt.inputTokens=1000000;receipt.cachedInputTokens=800000;receipt.outputTokens=100000;receipt.reasoningTokens=50000;receipt.totalTokens=1100000;d.usage.entries.push_back(receipt);
    auto cost=estimateDocumentCost(d,catalog);near(cost.standardShortUsd,1.48,"cache subset and reasoning not double-counted");near(cost.standardLongUsd,2.46,"long scenario");check(!cost.partial&&cost.pricedTokens==1100000,"cost coverage");
    d.usage.entries[0].model="unknown";cost=estimateDocumentCost(d,catalog);check(cost.partial&&cost.unpricedTokens==1100000&&cost.pricedTokens==0,"unknown never zero-cost evidence");
    check(!parseOpenAiPricingMarkdown("broken").error.empty(),"changed schema not guessed");
    const std::string plans="<PricingCard name=\"Pro\" priceEyebrow=\"From\" price=\"$100\" interval=\"/month\">\n#### Token rates\n<table><tr><th>Credits per 1M tokens</th></tr><tr><td>GPT-6.1 Sol</td><td>50 credits</td><td>2.5 credits</td><td>250 credits</td></tr></table>";
    check(parseOpenAiSubscriptionMarkdown(plans).find("Pro from $100/month")!=std::string::npos,"published starting price preserved");auto credits=parseOpenAiCreditRates(plans);check(credits.size()==1&&credits[0].model=="gpt-6.1-sol","credit model rates parsed");near(credits[0].cachedInputPerMillion,2.5,"credit cached rate");
    catalog.subscriptionCredits=credits;d.usage.entries[0].model="gpt-6.1-sol";cost=estimateDocumentCost(d,catalog);near(cost.standardCredits,37,"credit standard equivalent");check(cost.unpricedTokens==1100000&&cost.pricedCreditTokens==1100000,"API and credit coverage independent");
}
int main(int argc,char** argv){try{physics();pricing();if(argc>1){auto catalog=fetchOpenAiPricing();check(!catalog.models.empty(),catalog.error.c_str());check(!catalog.subscriptionCredits.empty(),"live credit rate parser");std::cout<<"Live rates: "<<catalog.models.size()<<" API models, "<<catalog.subscriptionCredits.size()<<" credit models; "<<catalog.fetchedAtUtc<<'\n';}std::cout<<"Physics and pricing tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
