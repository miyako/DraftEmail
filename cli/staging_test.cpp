#include "protocol.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#endif
namespace fs=std::filesystem;
void require(bool ok) { if(!ok) throw std::runtime_error("staging assertion failed"); }
#ifdef _WIN32
void require_private(const fs::path& path) {
    HANDLE token=nullptr;
    require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0);
    DWORD size=0;
    GetTokenInformation(token,TokenUser,nullptr,0,&size);
    std::vector<unsigned char> data(size);
    BOOL found=GetTokenInformation(token,TokenUser,data.data(),size,&size);
    CloseHandle(token);
    require(found!=0);
    PSID user=reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
    PSID owner=nullptr; PACL dacl=nullptr; PSECURITY_DESCRIPTOR descriptor=nullptr;
    require(GetNamedSecurityInfoW(path.c_str(),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,
                                 &owner,nullptr,&dacl,nullptr,&descriptor)==ERROR_SUCCESS);
    struct Descriptor { PSECURITY_DESCRIPTOR value; ~Descriptor() { LocalFree(value); } } owned{descriptor};
    require(owner && EqualSid(owner,user));
    SECURITY_DESCRIPTOR_CONTROL control=0; DWORD revision=0;
    require(GetSecurityDescriptorControl(descriptor,&control,&revision)!=0);
    require((control&SE_DACL_PROTECTED)!=0 && dacl && dacl->AceCount==2);
    unsigned char system[SECURITY_MAX_SID_SIZE]; DWORD system_size=sizeof(system);
    require(CreateWellKnownSid(WinLocalSystemSid,nullptr,system,&system_size)!=0);
    bool user_ace=false,system_ace=false;
    for(DWORD i=0;i<dacl->AceCount;++i) {
        void* raw=nullptr; require(GetAce(dacl,i,&raw)!=0);
        auto* ace=static_cast<ACCESS_ALLOWED_ACE*>(raw);
        require(ace->Header.AceType==ACCESS_ALLOWED_ACE_TYPE);
        PSID sid=&ace->SidStart;
        require(EqualSid(sid,user)||EqualSid(sid,system));
        require((ace->Mask&FILE_ALL_ACCESS)==FILE_ALL_ACCESS);
        user_ace=user_ace||EqualSid(sid,user);
        system_ace=system_ace||EqualSid(sid,system);
    }
    require(user_ace&&system_ace);
}
#endif
int main() {
    try {
        auto source=fs::current_path()/fs::u8path("original 世界 'quoted'.txt");
        { std::ofstream out(source,std::ios::binary); out<<"private attachment"; }
        mail::Request r; r.attachments={source.u8string()};
        require(mail::check_attachment(source.u8string())==18);
        auto staged=mail::stage(r);
        require(staged.token.size()==32 && staged.paths.size()==1);
        require(fs::exists(fs::u8path(staged.paths[0])));
        require(staged.paths[0]!=source.u8string());
#ifdef _WIN32
        auto copy=fs::u8path(staged.paths[0]);
        require_private(copy);
        require_private(copy.parent_path());
        require_private(copy.parent_path().parent_path());
#endif
        std::ifstream in(fs::u8path(staged.paths[0]),std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)),{});
        require(content=="private attachment"); in.close();
        try { mail::cleanup("../"+staged.token); require(false); } catch(const mail::Error&) {}
        require(fs::exists(source));
        mail::cleanup(staged.token);
        require(!fs::exists(fs::u8path(staged.paths[0])) && fs::exists(source));
        try { mail::cleanup(staged.token); require(false); } catch(const mail::Error&) {}
        auto second=mail::stage(r);
        require(second.token!=staged.token);
        auto unexpected=fs::u8path(second.paths[0]).parent_path()/"unexpected-directory";
        fs::create_directory(unexpected);
        try { mail::cleanup(second.token); require(false); } catch(const mail::Error&) {}
        require(fs::exists(fs::u8path(second.paths[0])));
        fs::remove(unexpected); mail::cleanup(second.token);
        fs::remove(source);
        fs::remove(fs::current_path()/"DraftEmail-staging");
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
