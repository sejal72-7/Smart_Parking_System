#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct AppConfig {
    std::string dbHost = "127.0.0.1";
    unsigned int dbPort = 3306;
    std::string dbUser = "root";
    std::string dbPassword = "";
    std::string dbName = "smart_parking";
    int serverPort = 8080;
    std::string frontendDir = "../frontend";

    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static AppConfig load(const std::string& filePath = "config.env") {
        AppConfig cfg;
        namespace fs = std::filesystem;

        // Search common relative locations so running from build/ or backend/ or root works cleanly
        std::vector<std::string> candidates = {
            filePath,
            "backend/" + filePath,
            "../" + filePath,
            "../../backend/" + filePath
        };

        std::string resolvedConfigPath;
        for (const auto& path : candidates) {
            if (fs::exists(path)) {
                resolvedConfigPath = path;
                break;
            }
        }

        if (!resolvedConfigPath.empty()) {
            std::ifstream in(resolvedConfigPath);
            std::string line;
            while (std::getline(in, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;
                size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = trim(line.substr(0, eq));
                std::string val = trim(line.substr(eq + 1));

                if (key == "DB_HOST") cfg.dbHost = val;
                else if (key == "DB_PORT" && !val.empty()) cfg.dbPort = static_cast<unsigned int>(std::stoul(val));
                else if (key == "DB_USER") cfg.dbUser = val;
                else if (key == "DB_PASSWORD") cfg.dbPassword = val;
                else if (key == "DB_NAME") cfg.dbName = val;
                else if (key == "SERVER_PORT" && !val.empty()) cfg.serverPort = std::stoi(val);
                else if (key == "FRONTEND_DIR") cfg.frontendDir = val;
            }
        }

        // Resolve frontendDir relative to the config file's parent if needed
        if (!fs::exists(cfg.frontendDir)) {
            std::vector<std::string> feCandidates = {
                "frontend",
                "../frontend",
                "../../frontend"
            };
            for (const auto& p : feCandidates) {
                if (fs::exists(p) && fs::is_directory(p)) {
                    cfg.frontendDir = p;
                    break;
                }
            }
        }

        return cfg;
    }
};
