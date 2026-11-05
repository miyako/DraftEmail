#include "provider.hpp"
#include <iostream>

namespace {
void require(bool ok) { if(!ok) throw std::runtime_error("provider assertion failed"); }
template<class F> void error(const char* code,F check) {
    try { check(); } catch(const mail::Error& e) { require(e.code==code); return; }
    throw std::runtime_error("expected provider rejection");
}
std::vector<unsigned char> pe() {
    std::vector<unsigned char> bytes(1024);
    auto word=[&](size_t at,unsigned value) { bytes[at]=value&255; bytes[at+1]=(value>>8)&255; };
    auto dword=[&](size_t at,unsigned value) { word(at,value); word(at+2,value>>16); };
    word(0,0x5a4d); dword(60,64);
    dword(64,0x4550); word(68,0x8664); word(70,1);
    word(84,240); word(86,0x2000); word(88,0x20b);
    dword(148,512); dword(196,16); dword(200,0x1000); dword(204,0x100);
    dword(340,0x1000); dword(344,512); dword(348,512);
    dword(532,1); dword(536,1); dword(540,0x1040); dword(544,0x1044); dword(548,0x1048);
    dword(576,0x1180); dword(580,0x1060); word(584,0);
    std::string symbol="MAPISendMailW";
    for(size_t i=0;i<symbol.size();++i) bytes[608+i]=symbol[i];
    return bytes;
}
}
int main() {
    try {
        using mail::ProviderImage;
        using mail::ProviderRegistration;
        auto valid=[](const std::wstring&) { return ProviderImage{true,true}; };
        ProviderRegistration machine{true,L"C:\\Program Files\\Mail\\unicode.dll",L"C:\\legacy.dll"};
        ProviderRegistration user{true,L"D:\\User Mail\\provider.dll",L""}, missing;
        require(mail::default_provider_name(L"Custom Mail",L"Microsoft Outlook")==L"Custom Mail");
        require(mail::default_provider_name(L"",L"Custom Mail")==L"Custom Mail");
        error("missing_configuration",[]{ mail::default_provider_name(L"",L""); });
        for(const auto* name:{L"Outlook (new)",L"NEW OUTLOOK",L"Microsoft Outlook (new)",L"Outlook for Windows",L"olk",L"bad\\key"})
            error("unsupported_provider",[&]{ mail::default_provider_name(name,L""); });
        require(mail::select_provider(L"Custom Mail",missing,machine,valid)==machine.dll_path_ex);
        require(mail::select_provider(L"Custom Mail",user,machine,valid)==user.dll_path_ex);
        machine.dll_path_ex.clear();
        require(mail::select_provider(L"Custom Mail",missing,machine,valid)==machine.dll_path);
        error("missing_configuration",[&]{ mail::select_provider(L"Custom Mail",ProviderRegistration{true,L"",L""},machine,valid); });
        error("missing_configuration",[&]{ mail::select_provider(L"Custom Mail",missing,missing,valid); });
        for(const auto* path:{L"relative.dll",L"C:relative.dll",L"\\\\host\\share\\provider.dll"}) {
            ProviderRegistration invalid{true,path,L""};
            error("unsupported_provider",[&]{ mail::select_provider(L"Custom Mail",missing,invalid,valid); });
        }
        error("unsupported_provider",[&]{ mail::select_provider(L"Custom Mail",missing,machine,
            [](const std::wstring&){ return ProviderImage{false,true}; }); });
        error("unsupported_provider",[&]{ mail::select_provider(L"Custom Mail",missing,machine,
            [](const std::wstring&){ return ProviderImage{true,false}; }); });
        ProviderRegistration outlook{true,L"C:\\Office\\OLMAPI32.DLL",L""};
        require(mail::select_provider(L"Microsoft Outlook",missing,outlook,valid)==outlook.dll_path_ex);
        error("unsupported_provider",[&]{ mail::select_provider(L"Microsoft Outlook",missing,machine,valid); });
        auto bytes=pe();
        require(mail::inspect_provider_image(bytes).x64 && mail::inspect_provider_image(bytes).unicode);
        for(size_t length=0;length<bytes.size();++length) {
            auto truncated=bytes; truncated.resize(length);
            auto result=mail::inspect_provider_image(truncated);
            if(length<897) require(!result.unicode);
        }
        auto invalid=bytes; invalid[68]=0x4c; invalid[69]=1;
        require(!mail::inspect_provider_image(invalid).x64);
        invalid=bytes; invalid[86]=0; invalid[87]=0;
        require(!mail::inspect_provider_image(invalid).x64);
        invalid=bytes; invalid[620]='A';
        require(mail::inspect_provider_image(invalid).x64 && !mail::inspect_provider_image(invalid).unicode);
        invalid=bytes; invalid[576]=0x60; invalid[577]=0x10;
        require(!mail::inspect_provider_image(invalid).unicode);
        invalid=bytes; invalid[584]=1;
        require(!mail::inspect_provider_image(invalid).unicode);
        invalid=bytes; invalid[580]=0xff; invalid[581]=0xff; invalid[582]=0xff; invalid[583]=0xff;
        require(!mail::inspect_provider_image(invalid).unicode);
        invalid=bytes; invalid[60]=0xff; invalid[61]=0xff; invalid[62]=0xff; invalid[63]=0xff;
        require(!mail::inspect_provider_image(invalid).x64);
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
