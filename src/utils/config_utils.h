#pragma once
#include <nlohmann/json.hpp>
#include <string>

void load_global_config(const std::string& path);
const nlohmann::json& get_config();
