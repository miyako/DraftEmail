#define NOMINMAX
#include "protocol.hpp"
#include <windows.h>
#include <mapi.h>
#include <filesystem>
#include <thread>
#include <future>
#include <cwctype>

namespace mail {
namespace {
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    if(!n) throw Error("invalid_utf8","arguments must be valid UTF-8");
    std::wstring out(n,0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),out.data(),n);
    return out;
}
std::wstring registry(HKEY hive,const wchar_t* key,const wchar_t* name) {
    HKEY h=nullptr;
    if(RegOpenKeyExW(hive,key,0,KEY_READ|KEY_WOW64_64KEY,&h)!=ERROR_SUCCESS) return {};
    DWORD type=0,size=0;
    auto result=RegQueryValueExW(h,name,nullptr,&type,nullptr,&size);
    if(result!=ERROR_SUCCESS || type!=REG_SZ || size<sizeof(wchar_t) || size>65536) { RegCloseKey(h); return {}; }
    std::wstring value(size/sizeof(wchar_t),0);
    result=RegQueryValueExW(h,name,nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&size);
    RegCloseKey(h);
    if(result!=ERROR_SUCCESS || value.back()!=0) return {};
    value.pop_back();
    if(value.find(L'\0')!=std::wstring::npos) return {};
    return value;
}
bool x64_image(const std::wstring& name) {
    HANDLE h=CreateFileW(name.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                        FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{}; DWORD read=0; bool ok=false;
    BY_HANDLE_FILE_INFORMATION info{};
    if(GetFileInformationByHandle(h,&info) &&
       !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
       ReadFile(h,&dos,sizeof(dos),&read,nullptr) && read==sizeof(dos) &&
       dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>=sizeof(dos) && dos.e_lfanew<16*1024*1024) {
        LARGE_INTEGER offset{}; offset.QuadPart=dos.e_lfanew;
        DWORD signature=0; IMAGE_FILE_HEADER header{};
        ok=SetFilePointerEx(h,offset,nullptr,FILE_BEGIN) &&
            ReadFile(h,&signature,sizeof(signature),&read,nullptr) && read==sizeof(signature) &&
            signature==IMAGE_NT_SIGNATURE && ReadFile(h,&header,sizeof(header),&read,nullptr) &&
            read==sizeof(header) && header.Machine==IMAGE_FILE_MACHINE_AMD64;
    }
    CloseHandle(h); return ok;
}
void provider() {
    SYSTEM_INFO info{}; GetNativeSystemInfo(&info);
    if(sizeof(void*)!=8 || info.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64)
        throw Error("unsupported_architecture","native Windows x64 process and provider required",3);
    auto selected=registry(HKEY_CURRENT_USER,L"Software\\Clients\\Mail",nullptr);
    if(selected.empty()) selected=registry(HKEY_LOCAL_MACHINE,L"Software\\Clients\\Mail",nullptr);
    if(selected!=L"Microsoft Outlook")
        throw Error("unsupported_provider","default mail provider must be classic Microsoft Outlook x64; new Outlook is unsupported",3);
    auto dll=registry(HKEY_LOCAL_MACHINE,L"Software\\Clients\\Mail\\Microsoft Outlook",L"DLLPathEx");
    if(dll.empty()) dll=registry(HKEY_LOCAL_MACHINE,L"Software\\Clients\\Mail\\Microsoft Outlook",L"DLLPath");
    std::filesystem::path p(dll);
    std::wstring filename=p.filename().wstring();
    for(auto& c:filename) c=towupper(c);
    if(!p.is_absolute() || filename!=L"OLMAPI32.DLL" || !x64_image(dll))
        throw Error("provider_incompatible","registered classic Outlook OLMAPI32.DLL must be an absolute readable x64 PE image",3);
    DWORD session=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&session) || session==0)
        throw Error("interactive_session_required","a local interactive Windows desktop session is required",3);
    HWINSTA station=GetProcessWindowStation(); USEROBJECTFLAGS flags{}; DWORD needed=0;
    if(!station || !GetUserObjectInformationW(station,UOI_FLAGS,&flags,sizeof(flags),&needed) || !(flags.dwFlags&WSF_VISIBLE))
        throw Error("interactive_session_required","a visible Windows window station is required",3);
    HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
    if(!desktop) throw Error("interactive_session_required","an accessible interactive input desktop is required",3);
    CloseDesktop(desktop);
}
}
std::string capabilities() {
    return R"({"protocol":1,"backend":"simple-mapi-w","client":"simple-mapi","fields":{"to":true,"cc":true,"bcc":true,"subject":true,"body":true,"attachments":true},"interactiveRequired":true,"timeoutSeconds":540,"deliveryConfirmed":false,"stagingCleanup":true,"providerPolicy":"classic-outlook-x64-only","newOutlook":false})";
}
void available(const Request& r) {
    if(r.client!="simple-mapi") throw Error("unsupported_client","this platform supports simple-mapi only",3);
    provider();
    HMODULE module=LoadLibraryExW(L"MAPI32.DLL",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!module) throw Error("backend_unavailable","system MAPI32 unavailable",3);
    bool unicode=GetProcAddress(module,"MAPISendMailW")!=nullptr;
    FreeLibrary(module);
    if(!unicode) throw Error("unicode_mapi_unavailable","MAPISendMailW required; ANSI fallback is forbidden",3);
}
std::string compose(const Request& r,const Staging& staged) {
    // The blocking provider owns its UI thread. A timeout ends this helper, not a
    // delivery decision; staged attachments remain available for the provider.
    auto task=std::make_shared<std::packaged_task<ULONG()>>([r,staged]() {
        HMODULE module=LoadLibraryExW(L"MAPI32.DLL",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module) throw Error("backend_unavailable","system MAPI32 unavailable",3);
        struct Module { HMODULE h; ~Module(){ FreeLibrary(h); } } owned{module};
        using Send=ULONG (FAR PASCAL*)(LHANDLE,ULONG_PTR,MapiMessageW*,FLAGS,ULONG);
        auto send=reinterpret_cast<Send>(GetProcAddress(module,"MAPISendMailW"));
        if(!send) throw Error("unicode_mapi_unavailable","MAPISendMailW required; ANSI fallback is forbidden",3);
        std::vector<std::wstring> addresses;
        addresses.reserve(r.to.size()+r.cc.size()+r.bcc.size());
        std::vector<MapiRecipDescW> recipients;
        recipients.reserve(addresses.capacity());
        auto add=[&](const std::vector<std::string>& group,ULONG type) {
            for(const auto& address:group) {
                addresses.push_back(L"SMTP:"+wide(address));
                MapiRecipDescW recipient{};
                recipient.ulRecipClass=type; recipient.lpszAddress=addresses.back().data();
                recipients.push_back(recipient);
            }
        };
        add(r.to,MAPI_TO); add(r.cc,MAPI_CC); add(r.bcc,MAPI_BCC);
        std::vector<std::wstring> paths,names;
        paths.reserve(staged.paths.size()); names.reserve(staged.paths.size());
        std::vector<MapiFileDescW> files;
        for(size_t i=0;i<staged.paths.size();++i) {
            paths.push_back(wide(staged.paths[i]));
            names.push_back(std::filesystem::u8path(r.attachments[i]).filename().wstring());
            MapiFileDescW file{};
            file.nPosition=ULONG(-1); file.lpszPathName=paths.back().data(); file.lpszFileName=names.back().data();
            files.push_back(file);
        }
        auto subject=wide(r.subject),body=wide(r.body);
        MapiMessageW message{};
        message.lpszSubject=subject.data(); message.lpszNoteText=body.data();
        message.nRecipCount=ULONG(recipients.size()); message.lpRecips=recipients.empty()?nullptr:recipients.data();
        message.nFileCount=ULONG(files.size()); message.lpFiles=files.empty()?nullptr:files.data();
        return send(0,0,&message,MAPI_DIALOG|MAPI_LOGON_UI|MAPI_FORCE_UNICODE,0);
    });
    auto result=task->get_future();
    std::thread worker([task](){ (*task)(); });
    if(result.wait_for(std::chrono::seconds(540))!=std::future_status::ready) {
        worker.detach();
        throw Error("timeout","MAPI did not return within 540 seconds; outcome unknown",4);
    }
    worker.join();
    ULONG status=result.get();
    if(status==SUCCESS_SUCCESS) return "handed_off";
    if(status==MAPI_E_USER_ABORT) return "cancelled";
    if(status==MAPI_E_UNICODE_NOT_SUPPORTED) throw Error("unicode_mapi_unavailable","provider does not support Unicode; ANSI fallback is forbidden",3);
    throw Error("backend_error","MAPISendMailW failed with status "+std::to_string(status)+"; outcome not confirmed",3);
}
}
