#include "protocol.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
void require(bool ok) { if(!ok) throw std::runtime_error("staging assertion failed"); }
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
