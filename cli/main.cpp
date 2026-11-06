#include "protocol.hpp"
#include <iostream>
#include <vector>
constexpr auto kVersion = "0.1.4";
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

int run(const std::vector<std::string>& args) {
    mail::Staging staged;
    try {
        for (const auto& s:args) if(!mail::utf8(s)) throw mail::Error("invalid_utf8","arguments must be valid UTF-8");
        if(args==std::vector<std::string>{"--version"}) {
            std::cout<<"{\"protocol\":1,\"version\":"<<mail::quote(kVersion)<<"}\n"; return 0;
        }
        if(args==std::vector<std::string>{"capabilities"}) { std::cout<<mail::capabilities()<<'\n'; return 0; }
        if(args.size()==3 && args[0]=="cleanup" && args[1]=="--staging") {
            mail::cleanup(args[2]); std::cout<<"{\"protocol\":1,\"status\":\"cleaned\"}\n"; return 0;
        }
        if(args.size()!=3 || (args[0]!="validate" && args[0]!="compose") || args[1]!="--request")
            throw mail::Error("invalid_arguments","expected --version, capabilities, validate|compose --request <JSON>, or cleanup --staging <token>");
        bool composing=args[0]=="compose";
        auto request=mail::validate(args[2],composing);
        if(!composing) { std::cout<<"{\"protocol\":1,\"status\":\"validated\"}\n"; return 0; }
        mail::available(request);
        staged=mail::stage(request);
        auto status=mail::compose(request,staged);
        std::cout<<"{\"protocol\":1,\"status\":"<<mail::quote(status)<<",\"staging\":"<<staged.json()<<"}\n";
        return 0;
    } catch(const mail::Error& e) {
        std::cerr<<"{\"protocol\":1,\"error\":{\"code\":"<<mail::quote(e.code)<<",\"message\":"<<mail::quote(e.what())<<"}";
        if(!staged.token.empty()) std::cerr<<",\"staging\":"<<staged.json();
        std::cerr<<"}\n"; return e.status;
    } catch(const std::exception&) {
        std::cerr<<"{\"protocol\":1,\"error\":{\"code\":\"internal_error\",\"message\":\"native operation failed\"}";
        if(!staged.token.empty()) std::cerr<<",\"staging\":"<<staged.json();
        std::cerr<<"}\n"; return 3;
    }
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stderr),_O_BINARY);
    std::vector<std::string> args;
    for(int i=1;i<argc;++i) {
        int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);
        if(!n) { std::cerr<<"{\"protocol\":1,\"error\":{\"code\":\"invalid_utf8\",\"message\":\"arguments must be valid UTF-8\"}}\n"; return 2; }
        std::string s(n,'\0');
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,s.data(),n,nullptr,nullptr);
        s.pop_back(); args.push_back(std::move(s));
    }
    return run(args);
}
#else
int main(int argc,char** argv) { return run(std::vector<std::string>(argv+1,argv+argc)); }
#endif
