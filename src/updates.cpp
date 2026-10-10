#include "ark/updates.hpp"
#include "ark/version.hpp"
#include "ark/json.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <cwctype>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincrypt.h>
#endif

namespace ark {
const char* app_version() noexcept { return ARK_APP_VERSION; }
bool redirect_to_updated_runtime() noexcept {
#ifdef _WIN32
    try {
        wchar_t marker[8]{};
        if (GetEnvironmentVariableW(L"ARK_UPDATE_LAUNCHED",marker,8) && std::wstring(marker)==L"1") {
            SetEnvironmentVariableW(L"ARK_UPDATE_LAUNCHED",nullptr); return false;
        }
        int count=0; wchar_t** arguments=CommandLineToArgvW(GetCommandLineW(),&count);
        if (!arguments) return false;
        arkjson::Value values=arkjson::Value::makeArr();
        for(int index=1; index<count; ++index) {
            const std::wstring value(arguments[index]);
            if(value==L"--shot" || value==L"--headless" || value.starts_with(L"--self-test")) { LocalFree(arguments); return false; }
            const int bytes=WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
            std::string utf8(bytes,'\0');
            WideCharToMultiByte(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),utf8.data(),bytes,nullptr,nullptr);
            values.arr.push_back(arkjson::Value::makeStr(std::move(utf8)));
        }
        LocalFree(arguments);
        wchar_t local[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local))) return false;
        const auto root=std::filesystem::path(local)/L"ARK"/L"app";
        const auto state=root/L"current.json";
        if(!std::filesystem::is_regular_file(state) || std::filesystem::file_size(state)>4096) return false;
        std::ifstream input(state,std::ios::binary);
        const std::string data((std::istreambuf_iterator<char>(input)),{});
        arkjson::Value json;
        if(!arkjson::parse(data,json)) return false;
        const auto* current=json.find("current"); const auto* pending=json.find("pending"); const auto* schema=json.find("schema");
        if(!schema || schema->asNum()!=1 || !current || current->t!=arkjson::Value::Str || !pending || pending->t!=arkjson::Value::Str) return false;
        if(pending->str.empty() && current->str==ARK_APP_VERSION) return false;
        auto version=[](const std::string& text,std::array<unsigned,3>& result) {
            size_t offset=0;
            for(size_t component=0;component<3;++component) {
                unsigned value=0; size_t digits=0;
                while(offset<text.size() && text[offset]>='0' && text[offset]<='9') {
                    if(++digits>9) return false;
                    value=value*10+static_cast<unsigned>(text[offset++]-'0');
                }
                if(!digits) return false;
                result[component]=value;
                if(component<2 && (offset>=text.size() || text[offset++]!='.')) return false;
            }
            return offset==text.size();
        };
        wchar_t module[32768]{};
        const DWORD moduleLength=GetModuleFileNameW(nullptr,module,32768);
        if(!moduleLength || moduleLength>=32768) return false;
        auto folded=[](std::wstring text) { for(auto& ch:text) ch=static_cast<wchar_t>(std::towlower(ch)); return text; };
        const auto installedPrefix=folded((root/L"versions").wstring()+L"\\");
        const bool installedExecutable=folded(std::wstring(module)).starts_with(installedPrefix);
        std::array<unsigned,3> selectedVersion{}, executableVersion{};
        const auto& selected=pending->str.empty()?current->str:pending->str;
        if(!version(selected,selectedVersion) || !version(ARK_APP_VERSION,executableVersion)) return false;
        // A newer standalone/development build must never be redirected to an older install.
        if(!installedExecutable && selectedVersion<=executableVersion && pending->str.empty()) return false;
        if(!installedExecutable && selectedVersion<executableVersion) return false;
        const auto script=root/L"launcher"/L"ark-launch.ps1";
        if(!std::filesystem::is_regular_file(script)) return false;
        wchar_t system[32768]{}, directory[32768]{};
        if(!GetSystemDirectoryW(system,32768)) return false;
        const DWORD directoryLength=GetCurrentDirectoryW(32768,directory);
        if(!directoryLength || directoryLength>=32768) return false;
        const int directoryBytes=WideCharToMultiByte(CP_UTF8,0,directory,static_cast<int>(directoryLength),nullptr,0,nullptr,nullptr);
        std::string directoryUtf8(directoryBytes,'\0');
        WideCharToMultiByte(CP_UTF8,0,directory,static_cast<int>(directoryLength),directoryUtf8.data(),directoryBytes,nullptr,nullptr);
        auto payload=arkjson::Value::makeObj();
        payload.set("arguments",std::move(values)); payload.set("workingDirectory",arkjson::Value::makeStr(std::move(directoryUtf8)));
        const std::string serialized=payload.dump();
        DWORD encodedSize=0;
        if(!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(serialized.data()),static_cast<DWORD>(serialized.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&encodedSize)) return false;
        std::string encoded(encodedSize,'\0');
        if(!CryptBinaryToStringA(reinterpret_cast<const BYTE*>(serialized.data()),static_cast<DWORD>(serialized.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,encoded.data(),&encodedSize)) return false;
        encoded.resize(encodedSize);
        if(!encoded.empty() && encoded.back()=='\0') encoded.pop_back();
        const std::wstring encodedWide(encoded.begin(),encoded.end());
        const auto powershell=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
        std::wstring command=L"\""+powershell.wstring()+L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script.wstring()+L"\" -InstallRoot \""+root.wstring()+L"\" -ArgumentsBase64 "+encodedWide;
        if(command.size()>=32760) return false;
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return false;
        CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
    } catch (...) {}
#endif
    return false;
}
bool open_update_manager(std::string& error) {
#ifdef _WIN32
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) { error="Could not locate ARK."; return false; }
    const auto runtime=std::filesystem::path(path).parent_path();
    const auto script=runtime/L"updates"/L"ark-update.ps1";
    if (!std::filesystem::is_regular_file(script)) { error="The update manager is missing. Rebuild or reinstall the ARK package."; return false; }
    wchar_t system[32768]{};
    if (!GetSystemDirectoryW(system,32768)) { error="Could not locate Windows PowerShell."; return false; }
    const auto powershell=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
    std::wstring command=L"\""+powershell.wstring()+L"\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -STA -File \""+script.wstring()+L"\" -Action Interactive -RuntimeDirectory \""+runtime.wstring()+L"\"";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,runtime.c_str(),&startup,&process)) {
        error="Could not open the update manager (Windows error "+std::to_string(GetLastError())+")."; return false;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return true;
#else
    error="The packaged update manager is available on Windows."; return false;
#endif
}
void acknowledge_update_startup() noexcept {
#ifdef _WIN32
    try {
        wchar_t file[32768]{}, nonce[128]{};
        if (!GetEnvironmentVariableW(L"ARK_UPDATE_ACK_FILE",file,32768) || !GetEnvironmentVariableW(L"ARK_UPDATE_ACK_NONCE",nonce,128)) return;
        // These values come only from the local launcher, never a release manifest.
        const std::wstring value(nonce);
        if (value.size()!=32 || value.find_first_not_of(L"0123456789abcdef")!=std::wstring::npos) return;
        std::ofstream output(std::filesystem::path(file),std::ios::binary|std::ios::trunc);
        for (wchar_t ch:value) output.put(static_cast<char>(ch));
        output.close();
        SetEnvironmentVariableW(L"ARK_UPDATE_ACK_FILE",nullptr);
        SetEnvironmentVariableW(L"ARK_UPDATE_ACK_NONCE",nullptr);
    } catch (...) {}
#endif
}
}
