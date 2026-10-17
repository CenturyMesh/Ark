bool assistantToolReadOnly(const V& tools,const std::string& name,const V& args){
    if(name=="tool_feedback")return true;
    if(name=="snapshot"&&args.t==V::Obj&&!args.find("path"))return true;
    if(name=="image_asset"&&field(args,"action")=="inspect")return true;
    if(name=="library_vendors"&&args.t==V::Obj&&(!args.find("operation")||field(args,"operation")=="list"))return true;
    for(const auto& tool:tools.arr)if(field(tool,"name")==name){
        const auto* annotations=tool.find("annotations");const auto* hint=annotations?annotations->find("readOnlyHint"):nullptr;
        return hint&&hint->t==V::Bool&&hint->b;
    }
    return false;
}
struct AssistantSteering::Impl {
    mutable std::mutex mutex;
    AssistantConfig config;
    V tools,history,messages=V::makeArr();
    std::size_t bytes=0;
    std::uint64_t version=0,request=0;
    bool closed=false,cancelled=false;
};
void validateAssistantSteeringInput(const AssistantConfig& config,const V& history,const V& tools){
    V complete=V::makeArr();std::set<std::string> answered;
    for(const auto& item:history.arr)if(field(item,"type")=="function_call_output")answered.insert(field(item,"call_id"));
    for(const auto& item:history.arr)if(field(item,"type")!="function_call"||answered.count(field(item,"call_id")))complete.arr.push_back(item);
    assistantRequest(config,complete,tools);
}
AssistantSteering::AssistantSteering(const AssistantConfig& config,const V& tools,const V& history):impl_(std::make_unique<Impl>()){
    validateAssistantConfig(config);require(history.t==V::Arr,"Steering requires retained conversation history.");
    impl_->config=config;impl_->tools=tools;impl_->history=history;
}
AssistantSteering::~AssistantSteering()=default;
bool AssistantSteering::enqueue(const V& userMessage){
    validateAssistantUserMessage(userMessage);const auto bytes=userMessage.dump().size();std::lock_guard lock(impl_->mutex);
    if(impl_->closed||impl_->cancelled)return false;
    require(bytes<=impl_->config.maxConversationBytes&&impl_->bytes<=impl_->config.maxConversationBytes-bytes,"Pending steering exceeds the connector request bound; send a smaller reference.");
    V prospective=impl_->history;for(const auto& message:impl_->messages.arr)prospective.arr.push_back(message);prospective.arr.push_back(userMessage);
    validateAssistantSteeringInput(impl_->config,prospective,impl_->tools); // No provider/network contact.
    impl_->messages.arr.push_back(userMessage);impl_->bytes+=bytes;++impl_->version;return true;
}
V AssistantSteering::inbox(){
    std::lock_guard lock(impl_->mutex);V messages=V::makeArr();if(impl_->cancelled||impl_->closed)return messages;
    messages=std::move(impl_->messages);impl_->messages=V::makeArr();impl_->bytes=0;
    for(const auto& message:messages.arr)impl_->history.arr.push_back(message);
    impl_->request=impl_->version;return messages;
}
void AssistantSteering::checkpoint(const V& history){std::lock_guard lock(impl_->mutex);impl_->history=history;}
std::uint64_t AssistantSteering::requestVersion() const{std::lock_guard lock(impl_->mutex);return impl_->request;}
bool AssistantSteering::current(std::uint64_t request) const{std::lock_guard lock(impl_->mutex);return !impl_->cancelled&&!impl_->closed&&request==impl_->version&&request==impl_->request;}
bool AssistantSteering::finish(){std::lock_guard lock(impl_->mutex);if(impl_->cancelled||impl_->closed||!impl_->messages.arr.empty()||impl_->version!=impl_->request)return false;impl_->closed=true;return true;}
void AssistantSteering::cancel() noexcept{std::lock_guard lock(impl_->mutex);impl_->cancelled=true;}
V AssistantSteering::pending() const{std::lock_guard lock(impl_->mutex);return impl_->messages;}
V AssistantSteering::history() const{std::lock_guard lock(impl_->mutex);return impl_->history;}
