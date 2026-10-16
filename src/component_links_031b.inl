// Links are observed page data. They are never automatically fetched or trusted
// as component geometry, procurement evidence, or instructions.
namespace {
size_t tagEnd031B(const std::string& html,size_t at){char quote=0;for(size_t i=at+1;i<html.size();++i){const char c=html[i];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='>')return i;}return std::string::npos;}
std::string linkAttribute031B(const std::string& tag,const std::string& key,bool* present=nullptr){
    if(present)*present=false;
    size_t i=tag.find_first_of(" \t\r\n");if(i==std::string::npos)return {};
    while(i<tag.size()){
        while(i<tag.size()&&((unsigned char)tag[i]<=32||tag[i]=='/'))++i;
        const size_t start=i;while(i<tag.size()&&((tag[i]>='a'&&tag[i]<='z')||(tag[i]>='A'&&tag[i]<='Z')||tag[i]=='-'||tag[i]=='_'||(tag[i]>='0'&&tag[i]<='9')))++i;
        if(start==i){++i;continue;}const auto name=lower(tag.substr(start,i-start));
        while(i<tag.size()&&(unsigned char)tag[i]<=32)++i;if(i==tag.size()||tag[i]!='=')continue;++i;
        while(i<tag.size()&&(unsigned char)tag[i]<=32)++i;if(i==tag.size())break;
        const char quote=(tag[i]=='\''||tag[i]=='"')?tag[i++]:0;const size_t value=i;
        if(quote){while(i<tag.size()&&tag[i]!=quote)++i;}else while(i<tag.size()&&(unsigned char)tag[i]>32&&tag[i]!='>')++i;
        if(name==key){if(present)*present=true;return decodeMarkup(tag.substr(value,i-value));}if(quote&&i<tag.size())++i;
    }return {};
}
std::string linkUrl031B(std::string href,const std::string& base){
    while(!href.empty()&&(unsigned char)href.front()<=32)href.erase(0,1);while(!href.empty()&&(unsigned char)href.back()<=32)href.pop_back();
    if(href.empty()||href.front()=='#')return {};if(auto hash=href.find('#');hash!=std::string::npos)href.resize(hash);
    if(href.rfind("//",0)==0)href="https:"+href;
    else if(href.rfind("https://",0)!=0){
        const auto colon=href.find(':'),slash=href.find('/');if(colon!=std::string::npos&&(slash==std::string::npos||colon<slash))return {};
        const auto hostEnd=base.find_first_of("/?",8);const std::string origin=base.substr(0,hostEnd);std::string location=hostEnd==std::string::npos||base[hostEnd]=='?'?"/":base.substr(hostEnd);
        if(auto q=location.find('?');q!=std::string::npos)location.resize(q);
        if(href.front()=='?')href=origin+location+href;
        else if(href.front()=='/')href=origin+href;
        else href=origin+location.substr(0,location.rfind('/')+1)+href;
    }
    // Resolve literal dot segments without decoding escaped path separators.
    const auto slash=href.find('/',8);if(slash!=std::string::npos){const auto q=href.find('?',slash);auto tail=q==std::string::npos?std::string{}:href.substr(q);auto source=href.substr(slash,q==std::string::npos?std::string::npos:q-slash);std::vector<std::string> parts;
        size_t p=1;while(p<=source.size()){auto end=source.find('/',p);auto part=source.substr(p,end==std::string::npos?std::string::npos:end-p);if(part==".."){if(!parts.empty())parts.pop_back();}else if(part!=".")parts.push_back(part);if(end==std::string::npos)break;p=end+1;}
        href.resize(slash);for(const auto& part:parts)href+="/"+part;if(parts.empty())href+='/';if((source.ends_with("/.")||source.ends_with("/.."))&&href.back()!='/')href+='/';href+=tail;}
    try{validateComponentHttpsUrl(href);}catch(const std::exception&){return {};}return href;
}
V componentLinks031B(const std::string& html,const std::string& finalUrl){
    V out=V::makeObj(),links=V::makeArr(),resources=V::makeArr();std::set<std::string> unique;auto normalized=lower(html);size_t at=0;bool truncated=false;
    // Ignore hidden markup before resolving the first HTML base element. A
    // script/comment cannot manufacture a base URL or resource anchor.
    size_t cursor=0;const std::set<std::string> rawTags{"script","style","noscript","svg","textarea","title","xmp","iframe","plaintext"};
    while((cursor=normalized.find('<',cursor))!=std::string::npos){
        if(normalized.compare(cursor,4,"<!--")==0){auto end=normalized.find("-->",cursor+4);end=end==std::string::npos?normalized.size():end+3;std::fill(normalized.begin()+cursor,normalized.begin()+end,' ');cursor=end;continue;}
        const auto end=tagEnd031B(normalized,cursor);if(end==std::string::npos)break;
        auto boundary=cursor+1;while(boundary<end&&((normalized[boundary]>='a'&&normalized[boundary]<='z')||(normalized[boundary]>='0'&&normalized[boundary]<='9')))++boundary;
        const auto name=normalized.substr(cursor+1,boundary-cursor-1);
        if(!rawTags.count(name)){cursor=end+1;continue;}
        size_t stop=end+1;
        if(name!="svg"||end==0||normalized[end-1]!='/'){
            const auto closing="</"+name;auto close=normalized.find(closing,end+1);
            while(close!=std::string::npos){const auto after=close+closing.size();if(after==normalized.size()||normalized[after]=='>'||(unsigned char)normalized[after]<=32)break;close=normalized.find(closing,after);}
            const auto closingEnd=close==std::string::npos?std::string::npos:tagEnd031B(normalized,close);stop=closingEnd==std::string::npos?normalized.size():closingEnd+1;
        }
        std::fill(normalized.begin()+cursor,normalized.begin()+stop,' ');cursor=stop;
    }
    std::string baseUrl=finalUrl;cursor=0;
    while((cursor=normalized.find('<',cursor))!=std::string::npos){const auto end=tagEnd031B(normalized,cursor);if(end==std::string::npos)break;
        const auto boundary=cursor+5;const bool isBase=normalized.compare(cursor,5,"<base")==0&&boundary<normalized.size()&&(normalized[boundary]=='>'||(unsigned char)normalized[boundary]<=32);
        if(!isBase){cursor=end+1;continue;}
        bool present=false;const auto href=linkAttribute031B(html.substr(cursor,end-cursor+1),"href",&present);if(!present){cursor=end+1;continue;}baseUrl=href.empty()?finalUrl:linkUrl031B(href,finalUrl);if(baseUrl.empty())throw std::runtime_error("Component page base href cannot be resolved to a supported public HTTPS URL");break;}
    while((at=normalized.find('<',at))!=std::string::npos){const auto close=tagEnd031B(normalized,at);if(close==std::string::npos)break;
        const auto tag=normalized.substr(at,std::min<size_t>(20,close-at+1));
        if(tag.rfind("<script",0)==0||tag.rfind("<style",0)==0||tag.rfind("<noscript",0)==0||tag.rfind("<svg",0)==0){auto name=tag.substr(1,tag.find_first_of(" >\t\r\n")-1);const auto end=normalized.find("</"+name+">",close);at=end==std::string::npos?html.size():end+name.size()+3;continue;}
        if(tag.rfind("<!--",0)==0){auto end=normalized.find("-->",at+4);at=end==std::string::npos?html.size():end+3;continue;}
        if(tag.size()<3||tag[1]!='a'||(tag[2]!='>'&&(unsigned char)tag[2]>32)){at=close+1;continue;}
        auto href=linkAttribute031B(html.substr(at,close-at+1),"href");auto end=normalized.find("</a",close+1);auto next=normalized.find("<a ",close+1);const auto labelEnd=end==std::string::npos||((next!=std::string::npos)&&next<end)?close+1:end;
        auto label=componentPageText(html.substr(close+1,labelEnd-close-1));if(label.size()>512){size_t endLabel=512;while(endLabel&&((unsigned char)label[endLabel]&0xc0)==0x80)--endLabel;label.resize(endLabel);}at=close+1;
        auto url=linkUrl031B(href,baseUrl);if(url.empty()||!unique.insert(url).second)continue;
        if(links.arr.size()>=128){truncated=true;break;}
        auto key=lower(url+" "+label);std::string kind="other";
        if(key.find(".step")!=std::string::npos||key.find(".stp")!=std::string::npos||key.find("step")!=std::string::npos)kind="step";
        else if(key.find(".iges")!=std::string::npos||key.find(".igs")!=std::string::npos)kind="iges";
        else if(key.find(".pdf")!=std::string::npos)kind="pdf";
        else if(key.find("drawing")!=std::string::npos||key.find("datasheet")!=std::string::npos||key.find("desenho")!=std::string::npos)kind="drawing";
        else if(key.find("download")!=std::string::npos||key.find("cad")!=std::string::npos)kind="cad_resource";
        V row=V::makeObj();row.set("url",V::makeStr(url));row.set("label",V::makeStr(label));row.set("kind",V::makeStr(kind));row.set("retrieved",V::makeBool(false));links.arr.push_back(row);if(kind!="other")resources.arr.push_back(row);
    }
    out.set("links",links);out.set("resource_links",resources);out.set("links_truncated",V::makeBool(truncated));out.set("link_base_url",V::makeStr(baseUrl));out.set("link_authority",V::makeStr("observed HTML anchor href and label; inferred resource kind; target bytes unverified"));return out;
}
}
