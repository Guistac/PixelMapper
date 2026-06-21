#pragma once
#include <string>
#include <unordered_map>
#include <sstream>
#include <cctype>

namespace PixelMapper::Network {

struct SimpleJson {
    std::unordered_map<std::string, std::string> values;

    static SimpleJson parse(const std::string& json) {
        SimpleJson obj;
        size_t i = 0;
        while (i < json.size()) {
            // Find key starting character
            if (json[i] == '"') {
                size_t startKey = ++i;
                while (i < json.size() && json[i] != '"') {
                    if (json[i] == '\\' && i + 1 < json.size()) i++; // skip escaped quote
                    i++;
                }
                std::string key = json.substr(startKey, i - startKey);
                i++; // skip closing '"'

                // Find separator colon
                while (i < json.size() && json[i] != ':') i++;
                i++; // skip colon

                // Find value start
                while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) i++;

                if (i < json.size() && json[i] == '"') {
                    // String value
                    size_t startVal = ++i;
                    while (i < json.size() && json[i] != '"') {
                        if (json[i] == '\\' && i + 1 < json.size()) i++;
                        i++;
                    }
                    std::string val = json.substr(startVal, i - startVal);
                    obj.values[key] = val;
                    i++; // skip closing '"'
                } else if (i < json.size() && json[i] == '{') {
                    // Nested JSON object
                    size_t startVal = i;
                    int braceDepth = 1;
                    i++;
                    while (i < json.size() && braceDepth > 0) {
                        if (json[i] == '{') braceDepth++;
                        else if (json[i] == '}') braceDepth--;
                        i++;
                    }
                    std::string val = json.substr(startVal, i - startVal);
                    obj.values[key] = val;
                    continue;
                } else if (i < json.size() && json[i] == '[') {
                    // Nested JSON array
                    size_t startVal = i;
                    int braceDepth = 1;
                    i++;
                    while (i < json.size() && braceDepth > 0) {
                        if (json[i] == '[') braceDepth++;
                        else if (json[i] == ']') braceDepth--;
                        i++;
                    }
                    std::string val = json.substr(startVal, i - startVal);
                    obj.values[key] = val;
                    continue;
                } else {
                    // Primitives (Numbers, Booleans, null)
                    size_t startVal = i;
                    while (i < json.size() && json[i] != ',' && json[i] != '}') {
                        i++;
                    }
                    std::string val = json.substr(startVal, i - startVal);
                    while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) {
                        val.pop_back();
                    }
                    obj.values[key] = val;
                }
            }
            i++;
        }
        return obj;
    }

    std::string getString(const std::string& key) const {
        auto it = values.find(key);
        return it != values.end() ? it->second : "";
    }

    bool getBool(const std::string& key) const {
        auto it = values.find(key);
        return it != values.end() && (it->second == "true" || it->second == "1");
    }

    int getInt(const std::string& key) const {
        auto it = values.find(key);
        return it != values.end() ? std::stoi(it->second) : 0;
    }

    static std::string build(const std::unordered_map<std::string, std::string>& kvs) {
        std::stringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& [k, v] : kvs) {
            if (!first) ss << ",";
            first = false;
            ss << "\"" << k << "\":";
            
            if (v.empty()) {
                ss << "\"\"";
            } else if (v[0] == '{' || v[0] == '[' || v == "true" || v == "false" || std::isdigit(static_cast<unsigned char>(v[0])) || 
                       (v.size() > 1 && v[0] == '-' && std::isdigit(static_cast<unsigned char>(v[1])))) {
                ss << v;
            } else {
                ss << "\"" << v << "\"";
            }
        }
        ss << "}";
        return ss.str();
    }
};

} // namespace PixelMapper::Network
