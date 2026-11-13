static void imageValidationMemoContracts074(){
    ark::clearImageValidationCache074B();ark::Image image;image.w=16;image.h=12;image.px.assign(16*12*3,127);
    const auto bytes=ark::encodePNG(image);const auto attachment=ark::assistantImageAttachment(bytes,"synthetic.png",ark::AssistantImageDecoder::Portable);
    V asset=V::makeObj();asset.set("name",V::makeStr("Source"));asset.set("mime_type",V::makeStr(attachment.mimeType));asset.set("data_base64",V::makeStr(attachment.base64));
    const auto first=ark::imageAssetFromJson(asset);const auto baseline=ark::imageValidationCacheInfo074B();
    asset.set("name",V::makeStr("Same pixels, new name"));const auto second=ark::imageAssetFromJson(asset);const auto cached=ark::imageValidationCacheInfo074B();
    check(first.sha256==second.sha256&&first.base64==second.base64,"Validation memo changed pixel source");
    check(cached.find("decode_attempts")->num==baseline.find("decode_attempts")->num&&cached.find("hits")->num>baseline.find("hits")->num,"Identical encoded source was unnecessarily decoded again");
    for(int variant=0;variant<4;++variant){V malformed=asset;if(variant==0)malformed.set("sha256",V::makeStr(std::string(64,'0')));else if(variant==1)malformed.set("width",V::makeNum(first.width+1));else if(variant==2)malformed.set("mime_type",V::makeStr("image/jpeg"));else{auto encoded=attachment.base64;encoded[0]='!';malformed.set("data_base64",V::makeStr(encoded));}bool rejected=false;try{(void)ark::imageAssetFromJson(malformed);}catch(const std::exception&){rejected=true;}check(rejected,"Validation memo accepted changed or forged source metadata/bytes");}
    const auto third=ark::imageAssetFromJson(asset);check(third.sha256==first.sha256,"Rejected lookup poisoned validated source");
    ark::Document document;document.imageAssets.push_back(first);equivalent(document);
}
