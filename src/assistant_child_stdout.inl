// Included inside the anonymous namespace of the Windows CLI channels.
// Drains a child's stdout on its own thread. A chatty CLI (partial stream
// events, tool-use echoes) otherwise blocks on a full pipe while the host runs a
// tool or writes a reply larger than the stdin pipe buffer, and both ends stall
// until the transport timeout terminates the child. Unread bytes stay bounded:
// past the bound the reader waits for the consumer instead of growing.
class ChildStdout {
    HANDLE pipe=nullptr;std::size_t limit=0;std::thread worker;std::mutex mutex;std::condition_variable arrived,drained;std::string buffer;bool ended=false,closing=false;
public:
    ~ChildStdout(){stop();}
    void start(HANDLE output,std::size_t bound){pipe=output;limit=bound;worker=std::thread([this]{std::vector<char> bytes(16384);
        for(;;){DWORD read=0;if(!ReadFile(pipe,bytes.data(),(DWORD)bytes.size(),&read,nullptr)||!read)break;std::unique_lock lock(mutex);buffer.append(bytes.data(),read);arrived.notify_all();drained.wait(lock,[&]{return buffer.size()<=limit||closing;});if(closing)break;}
        std::lock_guard lock(mutex);ended=true;arrived.notify_all();});}
    // Appends what arrives within `wait`; false once stdout closed and nothing remains.
    bool take(std::string& pending,std::chrono::milliseconds wait){std::unique_lock lock(mutex);if(buffer.empty()&&!ended)arrived.wait_for(lock,wait);if(buffer.empty())return !ended;pending.append(buffer);buffer.clear();drained.notify_all();return true;}
    void stop(){if(!worker.joinable())return;{std::lock_guard lock(mutex);closing=true;}drained.notify_all();
        for(;;){{std::lock_guard lock(mutex);if(ended)break;}CancelSynchronousIo(worker.native_handle());std::this_thread::sleep_for(std::chrono::milliseconds(5));}worker.join();}
};
