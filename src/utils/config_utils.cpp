#include "utils/config_utils.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>

static nlohmann::json g_config;

void load_global_config(const std::string& path) {
  try {
    std::ifstream ifs(path);
    if (ifs) {
      std::stringstream buffer;
      buffer << ifs.rdbuf();
      std::string content = buffer.str();
      if (!nlohmann::json::accept(content)) {
        spdlog::error("配置文件{}不是合法JSON，请检查", path);
        g_config = nlohmann::json{};
        std::abort();
      }
      g_config = nlohmann::json::parse(content);
    }
  } catch (...) {
    spdlog::warn("配置文件{}读取失败", path);
    g_config = nlohmann::json{};
  }
}

const nlohmann::json& get_config() { return g_config; }
