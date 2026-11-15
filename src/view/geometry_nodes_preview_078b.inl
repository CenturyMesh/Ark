// Display comparison only. Both snapshots use the same camera derived from
// checked current/candidate bounds; neither image may auto-fit independently.
static V geometrySharedCamera078(const V& receipt,u64 target){
 AABB box;bool measured=false;
 for(const char* phase:{"before","after"})for(const auto& row:field(field(receipt,phase),"features").arr)if((u64)field(row,"id").num==target){
  const auto* bounds=row.find("bounds_mm");if(!bounds||bounds->isNull())continue;
  if(bounds->t!=V::Arr||bounds->arr.size()!=6)throw std::runtime_error("Checked node bounds cannot define a comparison camera");for(const auto& coordinate:bounds->arr)if(coordinate.t!=V::Num||!std::isfinite(coordinate.num))throw std::runtime_error("Non-finite checked node bounds");
  const Vec3 low{bounds->arr[0].num,bounds->arr[1].num,bounds->arr[2].num},high{bounds->arr[3].num,bounds->arr[4].num,bounds->arr[5].num};if(low.x>high.x||low.y>high.y||low.z>high.z)throw std::runtime_error("Inverted checked node bounds");box.grow(low);box.grow(high);measured=true;
 }
 if(!measured)return V{};
 V camera=V::makeObj(),direction=V::makeArr(),up=V::makeArr(),center=V::makeArr();for(double x:{1.,-1.,.8})direction.arr.push_back(V::makeNum(x));for(double x:{0.,0.,1.})up.arr.push_back(V::makeNum(x));const auto targetPoint=(box.mn+box.mx)*.5;for(double x:{targetPoint.x,targetPoint.y,targetPoint.z})center.arr.push_back(V::makeNum(x));camera.set("direction",direction);camera.set("up",up);camera.set("target_mm",center);camera.set("distance_mm",V::makeNum(std::max(1.,box.diagonal()*2.1)));camera.set("fov_deg",V::makeNum(45));return camera;
}
static HBITMAP geometryComparisonBitmap078(const std::string& beforePath,const std::string& afterPath,int sourceWidth,int sourceHeight){
 if(sourceWidth<=0||sourceHeight<=0)throw std::runtime_error("Comparison snapshot dimensions are unavailable");
 struct Images {HBITMAP before=nullptr,after=nullptr,result=nullptr;~Images(){if(before)DeleteObject(before);if(after)DeleteObject(after);if(result)DeleteObject(result);}} images;
 images.before=candidateReviewBitmap(beforePath);images.after=candidateReviewBitmap(afterPath);if(!images.before||!images.after)throw std::runtime_error("Could not read the current/candidate comparison images");
 BITMAP a{},b{};if(!GetObject(images.before,sizeof a,&a)||!GetObject(images.after,sizeof b,&b)||a.bmWidth!=b.bmWidth||a.bmHeight!=b.bmHeight)throw std::runtime_error("Comparison bitmap dimensions differ");
 constexpr int width=784,height=300;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;images.result=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!images.result)throw std::runtime_error("Could not allocate the comparison bitmap");
 struct DCs {HDC output=CreateCompatibleDC(nullptr),source=CreateCompatibleDC(nullptr);HGDIOBJ oldOutput=nullptr,oldSource=nullptr;~DCs(){if(output&&oldOutput)SelectObject(output,oldOutput);if(source&&oldSource)SelectObject(source,oldSource);if(output)DeleteDC(output);if(source)DeleteDC(source);}} dc;if(!dc.output||!dc.source)throw std::runtime_error("Could not create comparison drawing contexts");dc.oldOutput=SelectObject(dc.output,images.result);
 fill(dc.output,{0,0,width,height},RGB(29,31,36));fill(dc.output,{391,6,393,height-6},RGB(72,84,98));
 // candidateReviewBitmap uses one fixed letterbox. Crop only that known
 // padding, then use the same pixel scale in both panels; never fit geometry.
 const double fitted=std::min((double)a.bmWidth/sourceWidth,(double)a.bmHeight/sourceHeight);const int cropW=std::max(1,(int)std::floor(sourceWidth*fitted)),cropH=std::max(1,(int)std::floor(sourceHeight*fitted)),cropX=(a.bmWidth-cropW)/2,cropY=(a.bmHeight-cropH)/2;
 const double scale=std::min(368./cropW,254./cropH);const int drawW=std::max(1,(int)std::floor(cropW*scale)),drawH=std::max(1,(int)std::floor(cropH*scale));
 for(int panel=0;panel<2;++panel){const int x=panel*392+(392-drawW)/2,y=35+(260-drawH)/2;const auto old=SelectObject(dc.source,panel?images.after:images.before);if(!dc.oldSource)dc.oldSource=old;SetStretchBltMode(dc.output,COLORONCOLOR);if(!StretchBlt(dc.output,x,y,drawW,drawH,dc.source,cropX,cropY,cropW,cropH,SRCCOPY))throw std::runtime_error("Could not draw the comparison images");text(dc.output,panel?"AFTER / PREVIEW":"BEFORE",{panel*392+14,5,panel*392+378,29},panel?RGB(99,220,192):RGB(206,219,233),state.font);}
 auto result=images.result;images.result=nullptr;return result;
}
