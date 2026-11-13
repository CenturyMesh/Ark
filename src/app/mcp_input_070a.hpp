#pragma once
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// A live forwarding proxy owns no drawing. Its redirected stdin may remain
// open in an MCP client after Studio closes, so do not block forever on getline.
// Observe disappearance only while waiting for the next request, never during
// dispatch. Standalone/headless and memory-only hosts retain ordinary stdin.
class McpInput070A {
public:
    explicit McpInput070A(const std::string& endpoint) {
#ifdef _WIN32
        input_=GetStdHandle(STD_INPUT_HANDLE);
        watch_=!endpoint.empty()&&GetFileType(input_)==FILE_TYPE_PIPE;
        pipe_="\\\\.\\pipe\\"+endpoint;
#else
        (void)endpoint;
#endif
    }
    bool next(std::string& line) {
#ifdef _WIN32
        if(watch_) {
            while(true) {
                if(endpointGone())return false;
                if(const auto newline=pending_.find('\n');newline!=std::string::npos) {
                    line=pending_.substr(0,newline);pending_.erase(0,newline+1);
                    if(!line.empty()&&line.back()=='\r')line.pop_back();
                    return true;
                }
                if(eof_) {
                    if(pending_.empty())return false;
                    line=std::move(pending_);pending_.clear();return true;
                }
                DWORD available=0;
                if(!PeekNamedPipe(input_,nullptr,0,nullptr,&available,nullptr)) {
                    eof_=true;continue;
                }
                if(available) {
                    char bytes[8192];DWORD read=0;
                    if(!ReadFile(input_,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr)||!read)eof_=true;
                    else pending_.append(bytes,read);
                }else std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        }
#endif
        return bool(std::getline(std::cin,line));
    }
private:
#ifdef _WIN32
    using Clock=std::chrono::steady_clock;
    bool endpointGone() {
        const auto now=Clock::now();
        if(now<nextProbe_)return false;
        nextProbe_=now+std::chrono::milliseconds(250);
        if(WaitNamedPipeA(pipe_.c_str(),NMPWAIT_NOWAIT)) {missingSince_={};return false;}
        const DWORD error=GetLastError();
        // Busy instances and access/transport errors do not prove absence.
        // This probe does not connect, consume a bridge slot or issue a tool.
        if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND) {missingSince_={};return false;}
        if(missingSince_==Clock::time_point{}) {
            missingSince_=now;
            std::cerr<<"ARK Studio endpoint is unavailable; waiting briefly for it. If Studio was closed, reconnect the MCP client to the new Studio endpoint.\n";
        }
        if(now-missingSince_<std::chrono::seconds(3))return false;
        std::cerr<<"ARK Studio endpoint remained absent; closing this idle --connect forwarding proxy. No standalone document was closed.\n";
        return true;
    }
    HANDLE input_=INVALID_HANDLE_VALUE;
    bool watch_=false,eof_=false;
    std::string pipe_,pending_;
    Clock::time_point nextProbe_{},missingSince_{};
#endif
};
