// PDF 1.4 writer. The vector presentation consumes the same derived display
// polylines and annotations as SVG. No mesh or PDF data re-enters the CAD model.
std::string drawingPdfPresentation(const std::vector<V>& reports) {
    require(!reports.empty()&&reports.size()<=256,"PDF needs 1..256 current sheets");
    auto literal=[](const std::string& utf8){
        std::string bytes;
        for(size_t i=0;i<utf8.size();){
            unsigned int cp=(unsigned char)utf8[i++];
            if(cp>=0xc2&&cp<=0xdf){require(i<utf8.size(),"Invalid PDF text UTF-8");const auto b=(unsigned char)utf8[i++];require((b&0xc0)==0x80,"Invalid PDF text UTF-8");cp=((cp&31)<<6)|(b&63);}
            else if(cp>=128)throw std::runtime_error("PDF_FONT_UNSUPPORTED: use Latin-1 sheet labels or export SVG for this script");
            require(cp==9||cp==10||cp==13||(cp>=32&&cp<=255&&!(cp>=127&&cp<=159)),"PDF_FONT_UNSUPPORTED: sheet text requires Latin-1");
            if(cp==9||cp==10||cp==13)cp=' ';
            if(cp=='('||cp==')'||cp=='\\')bytes+='\\';
            bytes+=(char)cp;
        }
        return "("+bytes+")";
    };
    std::vector<std::string> objects(3);
    objects[0]="<< /Type /Catalog /Pages 2 0 R /ViewerPreferences << /PrintScaling /None >> >>";
    objects[2]="<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>";
    std::ostringstream kids;kids<<"<< /Type /Pages /Count "<<reports.size()<<" /Kids [";
    for(size_t page=0;page<reports.size();++page){
        const auto& report=reports[page];require(word(report,"status")=="current","PDF requires current exact source bindings");
        const auto& authored=report.find("presentation")?*report.find("presentation"):need(report,"authored");
        const auto& paper=need(authored,"paper_mm");const double width=paper.arr[0].num,height=paper.arr[1].num,scale=number(need(authored,"scale")),pt=72./25.4;
        const auto* recipe=authored.find("template");const bool blueprint=recipe&&word(*recipe,"id")=="ark_blueprint";
        V annotations=V::makeArr();svg(report,&annotations);
        std::ostringstream content;content<<std::setprecision(12)<<"q\n"<<pt<<" 0 0 "<<-pt<<" 0 "<<height*pt<<" cm\n1 J 1 j\n";
        auto color=[&](int rgb,bool fill){content<<((rgb>>16)&255)/255.<<' '<<((rgb>>8)&255)/255.<<' '<<(rgb&255)/255.<<(fill?" rg\n":" RG\n");};
        color(blueprint?0x132d4b:0xfcfdfe,true);content<<"0 0 "<<width<<' '<<height<<" re f\n";
        for(const auto& view:need(report,"views").arr){
            const auto& offset=need(need(view,"authored"),"offset_mm");
            for(const char* collection:{"hatches","edges","centrelines"})if(const auto* rows=view.find(collection))for(const auto& edge:rows->arr){
                const auto& points=need(edge,"display_polyline_mm");require(points.t==V::Arr&&points.arr.size()>=2,"PDF curve has no display samples");
                const auto role=word(edge,"visibility");const bool hidden=role=="hidden",hatch=std::string(collection)=="hatches",centre=std::string(collection)=="centrelines";
                color(blueprint?(hidden||hatch?0x628fb1:0xcee7f6):(hidden||hatch?0x788692:0x25394b),false);content<<(hatch?.12:hidden?.15:.25)<<" w\n"<<(centre?"[4 1 0.5 1] 0 d\n":hidden?"[2 1] 0 d\n":"[] 0 d\n");
                bool first=true;for(const auto& p:points.arr){content<<offset.arr[0].num+p.arr[0].num*scale<<' '<<offset.arr[1].num-p.arr[1].num*scale<<(first?" m\n":" l\n");first=false;}content<<"S\n";
            }
        }
        content<<"[] 0 d\n0.2 w\n";
        for(const auto& item:annotations.arr){
            const int rgb=(int)number(need(item,"color"));
            if(word(item,"kind")=="line"){color(rgb,false);const auto& a=need(item,"a"),&b=need(item,"b");content<<a.arr[0].num<<' '<<a.arr[1].num<<" m "<<b.arr[0].num<<' '<<b.arr[1].num<<" l S\n";}
            else {color(rgb,true);const auto& at=need(item,"at");content<<"BT /F1 "<<number(need(item,"height"))<<" Tf 1 0 0 -1 "<<at.arr[0].num<<' '<<at.arr[1].num<<" Tm "<<literal(need(item,"text").str)<<" Tj ET\n";}
        }
        color(0x536475,true);content<<"BT /F1 2.4 Tf 1 0 0 -1 "<<width-32<<' '<<height-3<<" Tm "<<literal("Sheet "+std::to_string(page+1)+" / "+std::to_string(reports.size()))<<" Tj ET\nQ\n";
        const auto stream=content.str();const int pageId=(int)objects.size()+1,streamId=pageId+1;
        std::ostringstream pageObject;pageObject<<std::setprecision(12)<<"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "<<width*pt<<' '<<height*pt<<"] /Resources << /Font << /F1 3 0 R >> >> /Contents "<<streamId<<" 0 R >>";
        objects.push_back(pageObject.str());objects.push_back("<< /Length "+std::to_string(stream.size())+" >>\nstream\n"+stream+"endstream");kids<<pageId<<" 0 R ";
    }
    kids<<"] >>";objects[1]=kids.str();
    std::string pdf="%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";std::vector<size_t> offsets;
    for(size_t i=0;i<objects.size();++i){offsets.push_back(pdf.size());pdf+=std::to_string(i+1)+" 0 obj\n"+objects[i]+"\nendobj\n";}
    const auto start=pdf.size();std::ostringstream xref;xref<<"xref\n0 "<<objects.size()+1<<"\n0000000000 65535 f \n";for(auto offset:offsets)xref<<std::setw(10)<<std::setfill('0')<<offset<<" 00000 n \n";
    xref<<"trailer\n<< /Size "<<objects.size()+1<<" /Root 1 0 R >>\nstartxref\n"<<start<<"\n%%EOF\n";pdf+=xref.str();return pdf;
}
