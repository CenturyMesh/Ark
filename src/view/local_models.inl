// Catalog and model metadata use the same bounded native transport as inference.
// The seam is populated only by deterministic UI tests, never persisted.
static AssistantTransport g_localModelDiscoveryTestTransport;
static bool editLocalModel(AssistantConfig& config){
    using V=arkjson::Value;
    if(config.provider!="ollama"||config.authMode!=AssistantAuthMode::ApiKey)config=assistantProviderPreset("ollama");
    auto discover=[&]{V result=V::makeArr();auto query=config;query.timeoutSeconds=10;for(const auto& model:assistantLocalModels(query,g_localModelDiscoveryTestTransport))result.arr.push_back(V::makeStr(model));return result;};
    V catalog;try{catalog=g_headless?discover():callSessionWithProgress("local_model_catalog",V::makeObj(),discover);}catch(const std::exception&){throw std::runtime_error("Cannot read Ollama models. Start Ollama (ollama serve), then retry Local models. Existing connector unchanged.");}
    std::vector<std::string> models;for(const auto& item:catalog.arr)models.push_back(item.str);
    if(models.empty())throw std::runtime_error("Ollama has no installed models. Install a tool-capable model in Ollama, then retry.");
    // Blank choice requires an explicit selection; never silently choose a model.
    if(std::find(models.begin(),models.end(),config.model)==models.end())config.model.clear();models.insert(models.begin(),"");
    std::vector<EditorField> fields{{"Installed model",config.model,false,models},{"Thinking effort","auto",false,{"auto","none","low","medium","high"}}};
    if(std::find(fields[1].choices.begin(),fields[1].choices.end(),config.reasoningEffort)!=fields[1].choices.end())fields[1].value=config.reasoningEffort;
    if(!editFields("Local models / Ollama","Choose an installed model. Apply checks tools and images, without inference. Thinking: auto = model default; none = off when supported.",fields))return false;
    config.model=trimField(fields[0].value);config.reasoningEffort=fields[1].value=="auto"?"":fields[1].value;
    validateAssistantConfig(config);
    auto inspect=[&]{return assistantLocalModelInfo(config,g_localModelDiscoveryTestTransport);};
    const auto info=g_headless?inspect():callSessionWithProgress("local_model_capabilities",V::makeObj(),inspect);
    if(!info.find("tools")->b||!info.find("completion")->b)throw std::runtime_error("This model does not declare tool calling and completion support. Choose another Ollama model for agentic CAD work.");
    config.imageInputSupported=info.find("vision")->b;config.timeoutSeconds=300;
    if(!info.find("thinking")->b)config.reasoningEffort.clear();
    return true;
}
