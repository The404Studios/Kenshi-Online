/*
 * nlohmann/json stub - REPLACE WITH REAL LIBRARY
 *
 * This is a STUB file. For the project to build properly, download the real
 * nlohmann/json library:
 *
 * Download: https://github.com/nlohmann/json/releases/latest
 * File: json.hpp (single header)
 * Place at: Re_Kenshi_Plugin/vendor/nlohmann/json.hpp
 *
 * Quick download:
 * wget https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <stdexcept>

// Minimal stub for compilation - REPLACE WITH REAL LIBRARY
namespace nlohmann {
    class json {
    public:
        json() = default;
        json(const std::map<std::string, json>& m) {}
        json(const std::vector<json>& v) {}
        json(const std::string& s) {}
        json(int i) {}
        json(double d) {}
        json(bool b) {}
        json(std::nullptr_t) {}

        static json object() { return json(); }
        static json array() { return json(); }
        static json parse(const std::string& s) { return json(); }

        std::string dump(int indent = -1) const { return "{}"; }

        template<typename T>
        T value(const std::string& key, const T& defaultValue) const {
            return defaultValue;
        }

        json& operator[](const std::string& key) { return *this; }
        const json& operator[](const std::string& key) const { return *this; }
        json& operator[](size_t index) { return *this; }
        const json& operator[](size_t index) const { return *this; }

        bool contains(const std::string& key) const { return false; }
        size_t size() const { return 0; }
        bool empty() const { return true; }
    };
}

// WARNING: This is a minimal stub. For production use, download the real library:
// https://github.com/nlohmann/json/releases/latest/download/json.hpp
