#pragma once
#include "protocol.hpp"
#include <cstdint>
#include <functional>

namespace mail {
struct ProviderImage {
    bool x64=false;
    bool unicode=false;
};

// Inspect the on-disk export table without loading or running provider code.
inline ProviderImage inspect_provider_image(const std::vector<unsigned char>& b) {
    auto fits=[&](uint64_t at,uint64_t n) { return at<=b.size() && n<=b.size()-at; };
    auto u16=[&](size_t at)->uint32_t { return uint32_t(b[at])|(uint32_t(b[at+1])<<8); };
    auto u32=[&](size_t at)->uint32_t { return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24); };
    ProviderImage result;
    if(!fits(0,64)||u16(0)!=0x5a4d) return result;
    uint64_t pe=u32(60);
    if(!fits(pe,24)||u32(size_t(pe))!=0x4550||u16(size_t(pe+4))!=0x8664 ||
       !(u16(size_t(pe+22))&0x2000)) return result;
    uint32_t count=u16(size_t(pe+6)), optional=u16(size_t(pe+20));
    uint64_t opt=pe+24, sections=opt+optional;
    if(optional<120 || !fits(opt,optional)||u16(size_t(opt))!=0x20b ||
       !count||count>96||!fits(sections,uint64_t(count)*40)) return result;
    result.x64=true;
    if(u32(size_t(opt+108))<1) return result;
    uint32_t export_rva=u32(size_t(opt+112)), export_size=u32(size_t(opt+116));
    auto offset=[&](uint32_t rva,uint64_t size)->uint64_t {
        if(rva<u32(size_t(opt+60)) && fits(rva,size)) return rva;
        for(uint32_t i=0;i<count;++i) {
            size_t section=size_t(sections+uint64_t(i)*40);
            uint32_t start=u32(section+12), raw_size=u32(section+16), raw=u32(section+20);
            if(rva>=start && uint64_t(rva)-start<raw_size && size<=uint64_t(raw_size)-(rva-start) &&
               fits(uint64_t(raw)+rva-start,size)) return uint64_t(raw)+rva-start;
        }
        return UINT64_MAX;
    };
    if(!export_rva||export_size<40) return result;
    uint64_t exports=offset(export_rva,40);
    if(exports==UINT64_MAX) return result;
    uint32_t functions=u32(size_t(exports+20)), names=u32(size_t(exports+24));
    if(!functions||!names||functions>65536||names>65536) return result;
    uint64_t addresses=offset(u32(size_t(exports+28)),uint64_t(functions)*4);
    uint64_t strings=offset(u32(size_t(exports+32)),uint64_t(names)*4);
    uint64_t ordinals=offset(u32(size_t(exports+36)),uint64_t(names)*2);
    if(addresses==UINT64_MAX||strings==UINT64_MAX||ordinals==UINT64_MAX) return result;
    const std::string expected="MAPISendMailW";
    for(uint32_t i=0;i<names;++i) {
        uint64_t name=offset(u32(size_t(strings+uint64_t(i)*4)),expected.size()+1);
        if(name==UINT64_MAX) continue;
        bool matches=true;
        for(size_t j=0;j<expected.size();++j) if(b[size_t(name)+j]!=static_cast<unsigned char>(expected[j])) matches=false;
        if(!matches||b[size_t(name)+expected.size()]!=0) continue;
        uint32_t ordinal=u16(size_t(ordinals+uint64_t(i)*2));
        if(ordinal>=functions) return result;
        uint32_t function=u32(size_t(addresses+uint64_t(ordinal)*4));
        // Forwarded exports do not establish the target DLL's bitness/support.
        if(!function || (function>=export_rva && uint64_t(function)-export_rva<export_size)) return result;
        result.unicode=offset(function,1)!=UINT64_MAX;
        return result;
    }
    return result;
}

inline std::wstring provider_lower(std::wstring text) {
    for(auto& c:text) if(c>=L'A'&&c<=L'Z') c+=L'a'-L'A';
    return text;
}
inline std::wstring default_provider_name(const std::wstring& user,const std::wstring& machine) {
    auto name=user.empty()?machine:user;
    if(name.empty()) throw Error("missing_configuration","no default Simple MAPI client is registered",3);
    if(name.size()>255 || name.find_first_of(L"\\/\r\n\t")!=std::wstring::npos || name.find(L'\0')!=std::wstring::npos)
        throw Error("unsupported_provider","default Simple MAPI client registration is invalid",3);
    auto lower=provider_lower(name);
    for(const auto* unsupported:{L"outlook (new)",L"new outlook",L"microsoft outlook (new)",L"outlook for windows",L"olk"})
        if(lower==unsupported) throw Error("unsupported_provider","new Outlook does not support this Simple MAPI backend",3);
    return name;
}
struct ProviderRegistration {
    bool present=false;
    std::wstring dll_path_ex, dll_path;
};
inline std::wstring select_provider(const std::wstring& name,
                                   const ProviderRegistration& user,
                                   const ProviderRegistration& machine,
                                   const std::function<ProviderImage(const std::wstring&)>& inspect) {
    const auto& registration=user.present?user:machine;
    auto dll=registration.dll_path_ex.empty()?registration.dll_path:registration.dll_path_ex;
    if(!registration.present || dll.empty())
        throw Error("missing_configuration","default Simple MAPI provider requires DLLPathEx or DLLPath registration",3);
    bool absolute=dll.size()>3 && ((dll[0]>=L'A'&&dll[0]<=L'Z')||(dll[0]>=L'a'&&dll[0]<=L'z')) &&
                  dll[1]==L':' && (dll[2]==L'\\'||dll[2]==L'/');
    if(!absolute||dll.find(L'\0')!=std::wstring::npos)
        throw Error("unsupported_provider","registered provider DLL path must be drive-absolute",3);
    auto filename=provider_lower(dll.substr(dll.find_last_of(L"\\/")+1));
    if(provider_lower(name)==L"microsoft outlook" && filename!=L"olmapi32.dll")
        throw Error("unsupported_provider","classic Outlook registration must identify OLMAPI32.DLL",3);
    auto image=inspect(dll);
    if(!image.x64||!image.unicode)
        throw Error("unsupported_provider","registered provider must be a readable x64 DLL exporting MAPISendMailW; Unicode/bitness could not be established",3);
    return dll;
}
}
