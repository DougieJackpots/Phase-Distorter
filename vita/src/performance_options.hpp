#pragma once
#include <istream>
#include <string>
#include <stdexcept>
namespace vita {
struct PerformanceOptions {
    bool enhanced_timing = true, replay_only = false, save_enabled = true, core_timing = true;
    unsigned composition_stride = 1;
    unsigned long long frames = 0;
    std::string input_script;
    static unsigned long long number(const std::string& value) {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("Expected an unsigned integer: " + value);
        return std::stoull(value);
    }
    void set(const std::string& key, const std::string& value) {
        if (key == "input_script") { input_script = value; return; }
        const auto n = number(value);
        if (key == "composition_stride") {
            if (n < 1 || n > 3) throw std::invalid_argument("composition_stride must be 1, 2 or 3");
            composition_stride = unsigned(n);
        } else if (key == "frames") frames = n;
        else if (key == "enhanced_timing" || key == "replay_only" || key == "save_enabled" || key == "core_timing") {
            if (n > 1) throw std::invalid_argument(key + " must be 0 or 1");
            if (key == "enhanced_timing") enhanced_timing = bool(n);
            else if (key == "replay_only") replay_only = bool(n);
            else if (key == "save_enabled") save_enabled = bool(n);
            else core_timing = bool(n);
        } else throw std::invalid_argument("Unknown performance option: " + key);
    }
    static std::string trim(std::string s) {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
    }
    void read(std::istream& input) {
        for (std::string line; std::getline(input, line);) {
            line = trim(line.substr(0, line.find('#')));
            if (line.empty()) continue;
            const auto equal = line.find('=');
            if (equal == std::string::npos) throw std::invalid_argument("Expected key=value in performance.cfg");
            set(trim(line.substr(0, equal)), trim(line.substr(equal + 1)));
        }
    }
    void arguments(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string flag = argv[i];
            if (flag == "--replay-only") replay_only = true;
            else if (flag == "--no-save") save_enabled = false;
            else {
                if (i + 1 == argc) throw std::invalid_argument("Missing value for " + flag);
                std::string key;
                if (flag == "--input-script") key = "input_script";
                else if (flag == "--enhanced-timing") key = "enhanced_timing";
                else if (flag == "--composition-stride") key = "composition_stride";
                else if (flag == "--frames") key = "frames";
                else throw std::invalid_argument("Unknown argument: " + flag);
                set(key, argv[++i]);
            }
        }
        if (replay_only) {
            if (input_script.empty()) throw std::invalid_argument("replay_only requires an input_script");
            save_enabled = false; // Benchmarks start cold and never modify user SRAM.
        }
    }
};
} // namespace vita
