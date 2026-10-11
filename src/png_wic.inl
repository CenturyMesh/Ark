// Lossless Windows encoder. The portable stored-deflate encoder remains usable
// when the OS codec is unavailable; pixel dimensions and RGB values never change.
#ifdef _WIN32
template<class T>struct PngComPtr {T* p=nullptr;~PngComPtr(){if(p)p->Release();}T** put(){return &p;}T* operator->()const{return p;}};
static std::vector<u8> compressedPng(const Image& image){
    if(image.w<=0||image.h<=0||image.px.size()!=(size_t)image.w*image.h*3||image.px.size()>0xffffffffu)return {};
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Apartment{HRESULT code;~Apartment(){if(SUCCEEDED(code))CoUninitialize();}} apartment{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return {};
    PngComPtr<IWICImagingFactory> factory;PngComPtr<IWICBitmapEncoder> encoder;PngComPtr<IWICBitmapFrameEncode> frame;PngComPtr<IStream> stream;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())))||FAILED(CreateStreamOnHGlobal(nullptr,TRUE,stream.put())))return {};
    if(FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()))||FAILED(encoder->Initialize(stream.p,WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(frame.put(),nullptr))||FAILED(frame->Initialize(nullptr))||FAILED(frame->SetSize(image.w,image.h)))return {};
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;if(FAILED(frame->SetPixelFormat(&format)))return {};
    std::vector<u8> converted;const u8* pixels=image.px.data();
    if(IsEqualGUID(format,GUID_WICPixelFormat24bppBGR)){converted=image.px;for(size_t i=0;i<converted.size();i+=3)std::swap(converted[i],converted[i+2]);pixels=converted.data();}
    else if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))return {};
    if(FAILED(frame->WritePixels(image.h,image.w*3,(UINT)image.px.size(),const_cast<BYTE*>(pixels)))||FAILED(frame->Commit())||FAILED(encoder->Commit()))return {};
    STATSTG stat{};if(FAILED(stream->Stat(&stat,STATFLAG_NONAME))||stat.cbSize.QuadPart>0xffffffffu)return {};
    LARGE_INTEGER zero{};if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))return {};
    std::vector<u8> bytes((size_t)stat.cbSize.QuadPart);ULONG read=0;if(FAILED(stream->Read(bytes.data(),(ULONG)bytes.size(),&read))||read!=bytes.size())return {};return bytes;
}
#else
static std::vector<u8> compressedPng(const Image&){return {};}
#endif
