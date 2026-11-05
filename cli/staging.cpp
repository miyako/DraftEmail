#include "protocol.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <aclapi.h>
#else
#include <Security/SecRandom.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace mail {
namespace fs=std::filesystem;
namespace {
constexpr uint64_t max_file=25*1024*1024, max_total=100*1024*1024;
fs::path path(const std::string& s) { return fs::u8path(s); }
[[noreturn]] void attachment_error() { throw Error("invalid_attachment","attachments must be readable absolute regular files, without symlink/reparse entries, at most 25 MiB each"); }
void no_links(const fs::path& p) {
    fs::path current;
    for(const auto& part:p) {
        current/=part;
        std::error_code ec;
        auto status=fs::symlink_status(current,ec);
        if(ec || fs::is_symlink(status)) attachment_error();
#ifdef _WIN32
        DWORD attr=GetFileAttributesW(current.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES || (attr&FILE_ATTRIBUTE_REPARSE_POINT)) attachment_error();
#endif
    }
}
#ifdef _WIN32
void owner_only(const fs::path& p) {
    HANDLE token=nullptr;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)) throw Error("staging_failed","cannot verify staging owner",3);
    DWORD size=0; GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<unsigned char> data(size);
    bool ok=GetTokenInformation(token,TokenUser,data.data(),size,&size)!=0;
    CloseHandle(token);
    PSID owner=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
    DWORD status=GetNamedSecurityInfoW(p.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION,
                                     &owner,nullptr,nullptr,nullptr,&descriptor);
    ok=ok && status==ERROR_SUCCESS && owner && EqualSid(owner,reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid);
    if(descriptor) LocalFree(descriptor);
    if(!ok) throw Error("staging_failed","staging directory has unexpected owner",3);
}
struct File {
    HANDLE h=INVALID_HANDLE_VALUE;
    explicit File(const fs::path& p) {
        h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                      FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(h==INVALID_HANDLE_VALUE) attachment_error();
    }
    ~File() { if(h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
    uint64_t size() {
        BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER n{};
        if(!GetFileInformationByHandle(h,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
           GetFileType(h)!=FILE_TYPE_DISK || !GetFileSizeEx(h,&n) || n.QuadPart<0 || uint64_t(n.QuadPart)>max_file) attachment_error();
        return uint64_t(n.QuadPart);
    }
};
#else
struct File {
    int fd=-1;
    explicit File(const fs::path& p) { fd=open(p.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK); if(fd<0) attachment_error(); }
    ~File() { if(fd>=0) close(fd); }
    uint64_t size() {
        struct stat st{};
        if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<0 || uint64_t(st.st_size)>max_file) attachment_error();
        return uint64_t(st.st_size);
    }
};
#endif
fs::path root(bool create) {
#ifdef DRAFTEMAIL_STAGING_TEST
    fs::path parent=fs::current_path();
#elif defined(_WIN32)
    wchar_t buffer[32768];
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,32768);
    if(n==0||n>=32768) throw Error("staging_failed","LOCALAPPDATA unavailable",3);
    fs::path parent(buffer);
#else
    auto* pw=getpwuid(getuid());
    if(!pw || !pw->pw_dir) throw Error("staging_failed","home directory unavailable",3);
    fs::path parent=fs::path(pw->pw_dir)/"Library"/"Application Support";
#endif
    // The parent must already exist; do not follow user-controlled links.
    no_links(parent);
    fs::path result=parent/"DraftEmail-staging";
    if(create && !fs::exists(result)) {
#ifdef _WIN32
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;FA;;;SY)(A;;FA;;;OW)",SDDL_REVISION_1,&descriptor,nullptr))
            throw Error("staging_failed","cannot create private ACL",3);
        SECURITY_ATTRIBUTES sa{sizeof(sa),descriptor,FALSE};
        BOOL made=CreateDirectoryW(result.c_str(),&sa);
        LocalFree(descriptor);
        if(!made && GetLastError()!=ERROR_ALREADY_EXISTS) throw Error("staging_failed","cannot create staging root",3);
#else
        if(mkdir(result.c_str(),0700) && errno!=EEXIST) throw Error("staging_failed","cannot create staging root",3);
#endif
    }
    if(!fs::exists(result)) throw Error("invalid_staging","staging token does not exist");
    no_links(result);
    if(!fs::is_directory(result)) throw Error("staging_failed","staging root is not a directory",3);
#ifndef _WIN32
    struct stat st{};
    if(lstat(result.c_str(),&st) || st.st_uid!=getuid() || (st.st_mode&077)!=0)
        throw Error("staging_failed","staging root is not private",3);
#else
    owner_only(result);
    // Reapply a protected current-owner/system DACL before putting any data here.
    if(create) {
        PSECURITY_DESCRIPTOR sd=nullptr;
        if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;OW)",SDDL_REVISION_1,&sd,nullptr))
            throw Error("staging_failed","cannot secure staging root",3);
        BOOL ok=SetFileSecurityW(result.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);
        LocalFree(sd);
        if(!ok) throw Error("staging_failed","cannot secure staging root",3);
    }
#endif
    return result;
}
std::string token() {
    unsigned char bytes[16];
#ifdef _WIN32
    if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
#else
    if(SecRandomCopyBytes(kSecRandomDefault,sizeof(bytes),bytes)!=errSecSuccess)
#endif
        throw Error("staging_failed","secure random source unavailable",3);
    std::string s; const char* hex="0123456789abcdef";
    for(auto b:bytes) { s+=hex[b>>4]; s+=hex[b&15]; }
    return s;
}
}
uint64_t check_attachment(const std::string& s) {
    fs::path p=path(s);
    if(!p.is_absolute()) attachment_error();
#ifdef _WIN32
    // Deliberately exclude device/UNC/alternate stream namespaces.
    if(s.size()<3 || s[1]!=':' || (s[2]!='\\'&&s[2]!='/') ||
       s.find(':',2)!=std::string::npos) attachment_error();
#endif
    for(const auto& part:p) if(part==".." || part==".") attachment_error();
    no_links(p);
    File f(p); return f.size();
}
Staging stage(const Request& r) {
    Staging out;
    if(r.attachments.empty()) return out;
    auto base=root(true);
    out.token=token();
    auto dir=base/out.token;
#ifdef _WIN32
    if(!CreateDirectoryW(dir.c_str(),nullptr)) throw Error("staging_failed","cannot create private staging",3);
#else
    if(mkdir(dir.c_str(),0700)) throw Error("staging_failed","cannot create private staging",3);
#endif
    try {
        uint64_t total=0;
        for(size_t i=0;i<r.attachments.size();++i) {
            check_attachment(r.attachments[i]);
            File source(path(r.attachments[i]));
            uint64_t size=source.size(); total+=size;
            if(total>max_total) throw Error("limit_exceeded","attachments exceed 100 MiB total");
            auto destination=dir/(std::to_string(i)+"-"+path(r.attachments[i]).filename().u8string());
#ifdef _WIN32
            HANDLE target=CreateFileW(destination.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(target==INVALID_HANDLE_VALUE) throw Error("staging_failed","cannot create staging file",3);
            bool ok=true; uint64_t copied=0; char buffer[65536]; DWORD n=0,w=0;
            while(ok && ReadFile(source.h,buffer,sizeof(buffer),&n,nullptr) && n) {
                copied+=n; if(copied>size || !WriteFile(target,buffer,n,&w,nullptr) || w!=n) ok=false;
            }
            CloseHandle(target);
            if(!ok||copied!=size) throw Error("staging_failed","attachment changed or copy failed",3);
#else
            int target=open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
            if(target<0) throw Error("staging_failed","cannot create staging file",3);
            bool ok=true; uint64_t copied=0; char buffer[65536]; ssize_t n=0;
            while((n=read(source.fd,buffer,sizeof(buffer)))>0) {
                copied+=uint64_t(n);
                if(copied>size) { ok=false; break; }
                ssize_t offset=0;
                while(offset<n) { ssize_t w=write(target,buffer+offset,size_t(n-offset)); if(w<=0) { ok=false; break; } offset+=w; }
                if(!ok) break;
            }
            if(n<0) ok=false;
            if(close(target)) ok=false;
            if(!ok || copied!=size) throw Error("staging_failed","attachment changed or copy failed",3);
#endif
            out.paths.push_back(destination.u8string());
        }
        return out;
    } catch(...) { std::error_code ec; fs::remove_all(dir,ec); throw; }
}
void cleanup(const std::string& id) {
    if(id.size()!=32 || id.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw Error("invalid_staging","token must be 32 lowercase hexadecimal characters");
    auto dir=root(false)/id;
    if(!fs::exists(dir)) throw Error("invalid_staging","staging token does not exist");
    no_links(dir);
    if(!fs::is_directory(dir)) throw Error("invalid_staging","staging token is not a directory");
#ifdef _WIN32
    owner_only(dir);
#endif
    for(const auto& entry:fs::directory_iterator(dir)) {
        no_links(entry.path());
        if(!fs::is_regular_file(entry.symlink_status())) throw Error("invalid_staging","unexpected staging entry");
    }
    for(const auto& entry:fs::directory_iterator(dir)) fs::remove(entry.path());
    fs::remove(dir);
}
}
