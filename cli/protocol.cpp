#include "protocol.hpp"
#include <cctype>
#include <set>

namespace mail {
bool utf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i++];
        if (c <= 127) continue;
        unsigned n, cp, min;
        if (c >= 0xc2 && c <= 0xdf) { n=1; cp=c&31; min=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n=2; cp=c&15; min=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n=3; cp=c&7; min=0x10000; }
        else return false;
        if (n > s.size()-i) return false;
        while (n--) { unsigned char d=s[i++]; if ((d&0xc0)!=0x80) return false; cp=(cp<<6)|(d&63); }
        if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    return true;
}
std::string quote(const std::string& s) {
    std::string out="\"";
    const char* hex="0123456789abcdef";
    for (unsigned char c:s) {
        if (c=='"' || c=='\\') { out+='\\'; out+=c; }
        else if (c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=c;
    }
    return out+'"';
}
namespace {
class Parser {
    const std::string& s;
    size_t i=0;
    [[noreturn]] void bad() { throw Error("invalid_json","request must be strict JSON without duplicate keys"); }
    void ws() { while (i<s.size() && (s[i]==' ' || s[i]=='\t' || s[i]=='\r' || s[i]=='\n')) ++i; }
    char take() { if (i==s.size()) bad(); return s[i++]; }
    unsigned hex4() {
        unsigned cp=0;
        for (int j=0;j<4;++j) {
            char c=take(); unsigned n;
            if (c>='0'&&c<='9') n=c-'0';
            else if (c>='a'&&c<='f') n=c-'a'+10;
            else if (c>='A'&&c<='F') n=c-'A'+10;
            else { bad(); }
            cp=(cp<<4)|n;
        }
        return cp;
    }
    void append(std::string& out, unsigned cp) {
        if (cp<0x80) out+=char(cp);
        else if (cp<0x800) { out+=char(0xc0|(cp>>6)); out+=char(0x80|(cp&63)); }
        else if (cp<0x10000) { out+=char(0xe0|(cp>>12)); out+=char(0x80|((cp>>6)&63)); out+=char(0x80|(cp&63)); }
        else { out+=char(0xf0|(cp>>18)); out+=char(0x80|((cp>>12)&63)); out+=char(0x80|((cp>>6)&63)); out+=char(0x80|(cp&63)); }
    }
    std::string string() {
        if (take()!='"') bad();
        std::string out;
        while (true) {
            unsigned char c=take();
            if (c=='"') break;
            if (c<32) bad();
            if (c!='\\') { out+=char(c); continue; }
            char e=take();
            switch (e) {
            case '"': case '\\': case '/': out+=e; break;
            case 'b': out+='\b'; break;
            case 'f': out+='\f'; break;
            case 'n': out+='\n'; break;
            case 'r': out+='\r'; break;
            case 't': out+='\t'; break;
            case 'u': {
                unsigned cp=hex4();
                if (cp>=0xd800 && cp<=0xdbff) {
                    if (take()!='\\' || take()!='u') bad();
                    unsigned low=hex4(); if (low<0xdc00 || low>0xdfff) bad();
                    cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;
                } else if (cp>=0xdc00 && cp<=0xdfff) bad();
                append(out,cp); break;
            }
            default: bad();
            }
        }
        if (out.find('\0')!=std::string::npos) throw Error("invalid_request","NUL is forbidden");
        return out;
    }
    Value value(unsigned depth) {
        if (depth>16) throw Error("limit_exceeded","JSON nesting exceeds 16");
        ws(); if (i==s.size()) bad();
        Value v;
        if (s[i]=='"') { v.kind=Value::text; v.scalar=string(); return v; }
        if (s[i]=='{' || s[i]=='[') {
            bool obj=take()=='{'; char end=obj?'}':']';
            v.kind=obj?Value::object:Value::array;
            ws(); if (i<s.size() && s[i]==end) { ++i; return v; }
            do {
                ws();
                std::string key;
                if (obj) { key=string(); ws(); if (take()!=':') bad(); }
                Value item=value(depth+1);
                if (obj) { if (!v.fields.emplace(key,std::move(item)).second) bad(); }
                else v.items.push_back(std::move(item));
                ws(); char c=take();
                if (c==end) return v;
                if (c!=',') bad();
            } while (true);
        }
        for (const auto& literal: {std::string("true"),std::string("false"),std::string("null")}) {
            if (s.compare(i,literal.size(),literal)==0) {
                i+=literal.size(); v.kind=literal=="null"?Value::null_value:Value::boolean;
                v.scalar=literal; return v;
            }
        }
        size_t start=i;
        if (s[i]=='-') ++i;
        if (i==s.size()) bad();
        if (s[i]=='0') ++i;
        else { if (s[i]<'1'||s[i]>'9') bad(); while (i<s.size() && s[i]>='0'&&s[i]<='9') ++i; }
        if (i<s.size() && s[i]=='.') { ++i; size_t p=i; while (i<s.size()&&s[i]>='0'&&s[i]<='9') ++i; if (p==i) bad(); }
        if (i<s.size()&&(s[i]=='e'||s[i]=='E')) { ++i; if (i<s.size()&&(s[i]=='+'||s[i]=='-')) ++i; size_t p=i; while(i<s.size()&&s[i]>='0'&&s[i]<='9') ++i; if(p==i) bad(); }
        v.kind=Value::number; v.scalar=s.substr(start,i-start); return v;
    }
public:
    explicit Parser(const std::string& input):s(input) {}
    Value run() { Value v=value(0); ws(); if(i!=s.size()) bad(); return v; }
};
void require(bool ok, const std::string& message) { if (!ok) throw Error("invalid_request",message); }
bool mailbox(const std::string& s) {
    if (s.empty() || s.size()>254) return false;
    auto at=s.find('@');
    if (at==std::string::npos || at==0 || at>64 || at!=s.rfind('@') || at==s.size()-1) return false;
    const std::string atom="!#$%&'*+-/=?^_`{|}~.";
    for(size_t i=0;i<at;++i) {
        unsigned char c=s[i];
        if (!(std::isalnum(c) && c<128) && atom.find(char(c))==std::string::npos) return false;
    }
    if(s[0]=='.'||s[at-1]=='.'||s.substr(0,at).find("..")!=std::string::npos) return false;
    size_t label=0;
    for(size_t i=at+1;i<=s.size();++i) {
        if(i==s.size()||s[i]=='.') {
            if(label==0||label>63||s[i-label]=='-'||s[i-1]=='-') return false;
            label=0;
        } else {
            unsigned char c=s[i];
            if (!(c<128 && (std::isalnum(c)||c=='-'))) return false;
            ++label;
        }
    }
    return true;
}
}
Value parse(const std::string& s) {
    if(s.size()>24576) throw Error("limit_exceeded","request exceeds 24576 UTF-8 bytes");
    if(!utf8(s)) throw Error("invalid_utf8","arguments must be valid UTF-8");
    return Parser(s).run();
}
Request validate(const std::string& raw,bool composing) {
    Value root=parse(raw);
    require(root.kind==Value::object,"request must be an object");
    std::set<std::string> allowed={"protocol","client","to","cc","bcc","subject","body","attachments","interactive"};
    for(const auto& item:root.fields) if(!allowed.count(item.first)) throw Error("unknown_field","unknown request field: "+item.first);
    auto field=[&](const std::string& key,Value::Kind kind)->const Value& {
        auto it=root.fields.find(key);
        require(it!=root.fields.end(),"missing field: "+key);
        require(it->second.kind==kind,"wrong type: "+key);
        return it->second;
    };
    require(field("protocol",Value::number).scalar=="1","protocol must be integer 1");
    Request r;
    r.client=field("client",Value::text).scalar;
    require(r.client=="apple-mail" || r.client=="simple-mapi","unsupported client");
    r.subject=field("subject",Value::text).scalar;
    r.body=field("body",Value::text).scalar;
    if(r.subject.size()>998 || r.body.size()>16384) throw Error("limit_exceeded","subject or body exceeds limit");
    require(r.subject.find_first_of("\r\n")==std::string::npos,"subject must not contain CR or LF");
    auto list=[&](const std::string& key,std::vector<std::string>& out,size_t maximum) {
        const Value& v=field(key,Value::array);
        if(v.items.size()>maximum) throw Error("limit_exceeded","too many "+key);
        for(const auto& item:v.items) { require(item.kind==Value::text,"non-Text item: "+key); out.push_back(item.scalar); }
    };
    list("to",r.to,100); list("cc",r.cc,100); list("bcc",r.bcc,100); list("attachments",r.attachments,20);
    if(r.to.size()+r.cc.size()+r.bcc.size()>100) throw Error("limit_exceeded","more than 100 recipients");
    for(const auto* group:{&r.to,&r.cc,&r.bcc}) for(const auto& s:*group) require(mailbox(s),"invalid mailbox address");
    auto interactive=root.fields.find("interactive");
    if(interactive!=root.fields.end()) { require(interactive->second.kind==Value::boolean,"wrong type: interactive"); r.interactive=interactive->second.scalar=="true"; }
    require(!composing || r.interactive,"compose requires interactive:true");
    if(r.client=="apple-mail" && (!r.cc.empty()||!r.bcc.empty())) throw Error("unsupported_field","apple-mail does not expose cc or bcc");
    uint64_t total=0;
    for(const auto& path:r.attachments) {
        if(path.size()>4096) throw Error("limit_exceeded","attachment path exceeds 4096 bytes");
        total+=check_attachment(path);
        if(total>100*1024*1024) throw Error("limit_exceeded","attachments exceed 100 MiB total");
    }
    return r;
}
std::string Staging::json() const {
    std::string s="{\"token\":"+quote(token)+",\"paths\":[";
    for(size_t i=0;i<paths.size();++i) { if(i) s+=","; s+=quote(paths[i]); }
    return s+"],\"retained\":"+ (token.empty()?"false":"true")+"}";
}
}
