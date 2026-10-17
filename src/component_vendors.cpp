#include "ark/component_vendors.hpp"
#include "ark/component_sources.hpp"
#include "ark/iteration_history.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace ark { namespace {
using V=arkjson::Value;namespace fs=std::filesystem;
struct Vendor {const char* id;const char* name;const char* url;const char* domains;const char* site;const char* api;const char* docs;const char* note;};
const Vendor registry[]={
 {"misumi","MISUMI","https://uk.misumi-ec.com/","misumi-ec.com","browser_account_for_cad","no_public_cad_api_verified","https://us.misumi-ec.com/service/new-account/","Configure the exact part in the regional catalog. CAD downloads may require your own account; download in your browser then ingest. No MISUMI API adapter is configured."},
 {"traceparts","TraceParts","https://www.traceparts.com/","traceparts.com","browser_account_for_cad","partner_api_key_and_download_user_required","https://developers.traceparts.com/docs/add-the-basic-feature-on-your-product-pages","Official API exists: per-part CAD availability, cadRequest and temporary cadFileUrl. Requires authorized API access and download-user attribution. ARK has no configured TraceParts adapter."},
 {"3dfindit","CADENAS / 3Dfindit","https://www.3dfindit.com/","3dfindit.com,cadenas.de,partcommunity.com","browser_account_or_catalog_conditions","commercial_integration_not_configured","https://www.cadenas.de/en/products/ecatalogsolutions/overview","Search manufacturer catalogs and configure exact references. Catalog/download terms and account requirements apply. No undocumented API endpoints are called."},
 {"mcmaster","McMaster-Carr","https://www.mcmaster.com/","mcmaster.com","browser_product_cad_if_available","approved_customer_certificate_and_token_required","https://www.mcmaster.com/help/api/","Official Product Information API includes CAD downloads. It requires approved-customer client certificate, login and bearer token, product subscriptions and CAD rate limits. No authenticated adapter is configured."},
 {"digikey","DigiKey","https://www.digikey.com/","digikey.com,digikey.pt","public_pages_cad_availability_varies","oauth_application_required_not_configured","https://developer.digikey.com/products/product-information-v4/productsearch/media","Official Product Information API and media links require an authorized application and token. Media is not guaranteed STEP; use manufacturer/reference evidence and inspect returned bytes."},
 {"mouser","Mouser","https://www.mouser.com/","mouser.com,mouser.pt","public_pages_cad_availability_varies","search_api_key_required_not_configured","https://www.mouser.com/api-search/","Official Search API provides product data with an API key. It does not establish a general STEP-download API. Follow observed manufacturer CAD resources under their terms."},
 {"snapeda","SnapMagic / SnapEDA","https://www.snapeda.com/","snapeda.com,snapmagic.com","browser_account_or_download_conditions","partnership_integration_not_configured","https://www.snapeda.com/get-api/","Official request-access API offers symbols, footprints and 3D models. No authorized adapter is configured. Verify actual model format, exact reference and license; no logged-in scraping."},
 {"kicad","KiCad community 3D library","https://gitlab.com/kicad/libraries/kicad-packages3D","gitlab.com","public_repository_and_step","public_gitlab_repository_api","https://www.kicad.org/libraries/license/","Public paged repository discovery and direct STEP are implemented. Community geometry is not manufacturer qualification. Preserve the applicable KiCad library license and exception."},
 {"web","Public component discovery","https://duckduckgo.com/","duckduckgo.com","public_search_subject_to_challenges","html_search_not_supplier_api","https://duckduckgo.com/","Public search supplies candidates only. Challenge pages and empty results are explicit failures. Read the actual supplier page before selecting a reference."},
 {"direct_https","Other public HTTPS source","","","public_https_without_account","direct_resource_only","","Use an observed public product/resource URL or a user-provided URL. STEP bytes are validated independently. Unknown sources require this policy as well as any known vendor approval."}
};
const Vendor& vendor(const std::string& id){for(const auto& v:registry)if(id==v.id)return v;throw std::runtime_error("Unknown approved vendor; inspect library_vendors first");}
std::string str(const V& a,const char* key,const std::string& fallback={}){auto p=a.find(key);if(!p)return fallback;if(p->t!=V::Str)throw std::runtime_error(std::string(key)+" must be text");return p->str;}
fs::path policyRoot(const std::string& requested){if(!requested.empty())return fs::absolute(fs::u8path(requested)).lexically_normal();if(auto v=std::getenv("ARK_COMPONENT_LIBRARY");v&&*v)return fs::absolute(fs::u8path(v)).lexically_normal();if(auto v=std::getenv("LOCALAPPDATA");v&&*v)return fs::u8path(v)/"ARK"/"components";if(auto v=std::getenv("HOME");v&&*v)return fs::u8path(v)/".local"/"share"/"ark"/"components";throw std::runtime_error("Configure a local component library root");}
V defaults(){V p=V::makeObj(),a=V::makeObj();for(const auto& v:registry)a.set(v.id,V::makeBool(true));p.set("schema",V::makeNum(1));p.set("revision",V::makeNum(1));p.set("approvals",a);return p;}
// The OS releases this lock even after abnormal process termination. An empty
// lock file on POSIX is harmless; its existence never represents lock ownership.
struct PolicyWriteLock {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    explicit PolicyWriteLock(const fs::path& path){handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Vendor policy is being updated or its lock is unavailable; list the current policy and retry");}
    ~PolicyWriteLock(){CloseHandle(handle);}
#else
    int descriptor=-1;
    explicit PolicyWriteLock(const fs::path& path){descriptor=::open(path.c_str(),O_CREAT|O_RDWR,0600);if(descriptor<0)throw std::runtime_error("Cannot open approved vendor policy lock");if(flock(descriptor,LOCK_EX|LOCK_NB)!=0){::close(descriptor);descriptor=-1;throw std::runtime_error("Vendor policy is being updated; list the current policy and retry");}}
    ~PolicyWriteLock(){if(descriptor>=0)::close(descriptor);}
#endif
    PolicyWriteLock(const PolicyWriteLock&)=delete;
};
V loadPolicy(const fs::path& root){auto p=defaults();const auto path=root/"approved-vendors.json";if(!fs::exists(path))return p;if(fs::is_symlink(path)||fs::file_size(path)>16384)throw std::runtime_error("Approved vendor policy is invalid or exceeds 16 KiB");std::ifstream in(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});if(!in.eof()&&in.fail())throw std::runtime_error("Cannot read approved vendor policy");if(!arkjson::parse(bytes,p)||p.t!=V::Obj)throw std::runtime_error("Cannot parse approved vendor policy; no network access authorized");for(const auto& item:p.obj)if(item.first!="schema"&&item.first!="revision"&&item.first!="approvals")throw std::runtime_error("Unsupported approved vendor policy field");auto s=p.find("schema"),r=p.find("revision"),a=p.find("approvals");if(!s||s->t!=V::Num||s->num!=1||!r||r->t!=V::Num||!std::isfinite(r->num)||r->num<1||r->num>9007199254740990.||std::floor(r->num)!=r->num||!a||a->t!=V::Obj||a->obj.size()!=std::size(registry))throw std::runtime_error("Invalid approved vendor policy contract");for(const auto& v:registry)if(auto b=a->find(v.id);!b||b->t!=V::Bool)throw std::runtime_error("Incomplete approved vendor policy; no network access authorized");return p;}
std::vector<std::string> domains(const Vendor& v){std::vector<std::string> out;std::string s=v.domains;size_t at=0;while(at<s.size()){auto end=s.find(',',at);out.push_back(s.substr(at,end==std::string::npos?end:end-at));if(end==std::string::npos)break;at=end+1;}return out;}
bool domainMatch(const Vendor& v,const std::string& host){for(const auto& d:domains(v))if(host==d||(host.size()>d.size()&&host.ends_with("."+d)))return true;return false;}
V result(const V& p){V out=V::makeObj(),rows=V::makeArr();for(const auto& v:registry){V row=V::makeObj();row.set("id",V::makeStr(v.id));row.set("name",V::makeStr(v.name));row.set("url",V::makeStr(v.url));V ds=V::makeArr();for(auto d:domains(v))ds.arr.push_back(V::makeStr(d));row.set("domains",ds);row.set("approved",*p.find("approvals")->find(v.id));row.set("site_access",V::makeStr(v.site));row.set("api_access",V::makeStr(v.api));row.set("adapter_status",V::makeStr(std::string(v.id)=="kicad"?"public_repository_implemented":std::string(v.id)=="direct_https"?"public_download_implemented":"public_candidate_discovery_only"));row.set("documentation_url",V::makeStr(v.docs));row.set("notes",V::makeStr(v.note));rows.arr.push_back(row);}out.set("revision",*p.find("revision"));out.set("policy_sha256",V::makeStr(iterationSha256(p.dump())));out.set("vendors",rows);out.set("secrets_stored",V::makeBool(false));out.set("approval_authority",V::makeStr("local_user_network_policy; not supplier, license or manufacturing qualification"));out.set("next_step",V::makeStr("library_source_search with an approved vendor id and exact reference; then library_source_page, review observed resource_links, library_fetch or local library_ingest, and revision-bound library_insert"));return out;}
}
std::string componentVendorForUrl(const std::string& url){const auto host=componentHttpsHost(url);for(const auto& v:registry)if(domainMatch(v,host))return v.id;return "direct_https";}
V componentVendors(const V& args){if(args.t!=V::Obj)throw std::runtime_error("Vendor policy arguments must be an object");for(const auto& i:args.obj)if(i.first!="operation"&&i.first!="root"&&i.first!="vendor"&&i.first!="approved"&&i.first!="expected_revision")throw std::runtime_error("Unknown vendor policy field");static std::mutex mutex;std::lock_guard lock(mutex);const auto root=policyRoot(str(args,"root"));auto p=loadPolicy(root);auto op=str(args,"operation","list");if(op=="list"){if(args.find("vendor")||args.find("approved")||args.find("expected_revision"))throw std::runtime_error("list accepts only operation and root");return result(p);}if(op!="update")throw std::runtime_error("Vendor policy operation must be list or update");const auto& v=vendor(str(args,"vendor"));auto b=args.find("approved"),r=args.find("expected_revision");if(!b||b->t!=V::Bool||!r||r->t!=V::Num||r->num!=p.find("revision")->num)throw std::runtime_error("Vendor policy revision conflict; list the policy before updating approval");fs::create_directories(root);PolicyWriteLock writer(root/".approved-vendors.lock");p=loadPolicy(root);if(r->num!=p.find("revision")->num)throw std::runtime_error("Vendor policy revision conflict; list the policy before updating approval");if(p.find("approvals")->find(v.id)->b==b->b)return result(p);auto approvals=*p.find("approvals");approvals.set(v.id,*b);p.set("approvals",approvals);p.set("revision",V::makeNum(r->num+1));static std::atomic<unsigned long long> serial{0};const auto staging=root/(".approved-vendors-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));if(!fs::create_directory(staging))throw std::runtime_error("Cannot create private vendor policy staging directory");struct StageCleanup{fs::path directory;~StageCleanup(){std::error_code e;fs::remove(directory/"policy.json",e);fs::remove(directory,e);}}temp{staging};const auto tmp=staging/"policy.json";{std::ofstream file(tmp,std::ios::binary|std::ios::trunc);auto bytes=p.dump();if(!file||!file.write(bytes.data(),(std::streamsize)bytes.size()))throw std::runtime_error("Cannot stage vendor policy");file.close();if(!file)throw std::runtime_error("Cannot flush vendor policy");}
#ifdef _WIN32
if(!MoveFileExW(tmp.c_str(),(root/"approved-vendors.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot atomically publish approved vendor policy");
#else
fs::rename(tmp,root/"approved-vendors.json");
#endif
return result(p);}
V componentVendorEvidence(const std::string& requested,const std::string& url,const std::string& root){const auto actual=componentVendorForUrl(url);const auto id=requested.empty()?actual:requested;const auto& v=vendor(id);const auto host=componentHttpsHost(url);if(id!="direct_https"&&!domainMatch(v,host))throw std::runtime_error("URL is outside the selected vendor domains; use the observed resource's actual provider");auto p=loadPolicy(policyRoot(root));if(!p.find("approvals")->find(v.id)->b||!p.find("approvals")->find(actual.c_str())->b)throw std::runtime_error("Component vendor is disabled by the local Approved Vendors policy");V out=V::makeObj();out.set("vendor",V::makeStr(id));out.set("observed_vendor",V::makeStr(actual));out.set("approved",V::makeBool(true));out.set("policy_revision",*p.find("revision"));out.set("policy_sha256",V::makeStr(iterationSha256(p.dump())));out.set("authority",V::makeStr("network_policy_only"));return out;}
}
