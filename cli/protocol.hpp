#pragma once
#include <map>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mail {
struct Error : std::runtime_error {
    std::string code;
    int status;
    Error(std::string c, std::string m, int s = 2)
        : std::runtime_error(m), code(std::move(c)), status(s) {}
};
bool utf8(const std::string&);
std::string quote(const std::string&);
struct Value {
    enum Kind { null_value, boolean, number, text, array, object } kind = null_value;
    std::string scalar;
    std::vector<Value> items;
    std::map<std::string, Value> fields;
};
Value parse(const std::string&);
struct Request {
    std::string client, subject, body;
    std::vector<std::string> to, cc, bcc, attachments;
    bool interactive = false;
};
Request validate(const std::string&, bool);
struct Staging {
    std::string token;
    std::vector<std::string> paths;
    std::string json() const;
};
uint64_t check_attachment(const std::string&);
Staging stage(const Request&);
void cleanup(const std::string&);
std::string capabilities();
void available(const Request&);
std::string compose(const Request&, const Staging&);
}
