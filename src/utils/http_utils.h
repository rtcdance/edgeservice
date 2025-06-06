#pragma once
#include <spdlog/spdlog.h>
#include <uuid/uuid.h>

#include <codecvt>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>

#include "3rdparty/include/cpp-httplib/httplib.h"

inline bool is_digits(const std::string &s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}

// 安全提取 camera_id
inline bool parse_camera_id(const nlohmann::json &j, const std::string &key,
                            uint64_t &camera_id, std::string &err_msg) {
  camera_id = 0;
  err_msg.clear();
  if (j.contains(key)) {
    if (j[key].is_string()) {
      try {
        std::string id_str = j[key].get<std::string>();
        if (!id_str.empty() &&
            std::all_of(id_str.begin(), id_str.end(), ::isdigit)) {
          camera_id = std::stoull(id_str);
        } else {
          err_msg = key + " 必须为数字字符串";
          return false;
        }
      } catch (...) {
        err_msg = key + " 字段解析异常";
        return false;
      }
    } else if (j[key].is_number_integer()) {
      camera_id = j[key].get<uint64_t>();
    } else if (j[key].is_number_float()) {
      camera_id = static_cast<uint64_t>(j[key].get<double>());
    } else {
      err_msg = key + " 字段类型不支持";
      return false;
    }
  } else {
    err_msg = key + " 字段缺失";
    return false;
  }
  return true;
}

inline bool is_valid_url(const std::string &url) {
  static const std::regex url_regex(R"(^((rtsp|http|https)://)[^\s]+$)",
                                    std::regex::icase);
  return std::regex_match(url, url_regex);
}

inline bool is_valid_uuid(const std::string &uuid) {
  static const std::regex uuid_regex(
      R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})");
  return std::regex_match(uuid, uuid_regex);
}

inline bool is_valid_host(const std::string &host) {
  // 简单校验：IP(v4)或域名（允许字母、数字、-、.）
  static const std::regex host_regex(
      R"(^([a-zA-Z0-9\-\.]+|((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?))$)");
  return std::regex_match(host, host_regex);
}

inline std::string generate_uuid() {
  uuid_t uuid;
  char uuid_str[37] = "";
  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);
  return std::string(uuid_str);
}

// 标准JSON请求体预处理（UTF-8校验+全角引号替换+异常捕获）
template <typename Func>
inline void handle_json_post(const httplib::Request &req,
                             httplib::Response &res, Func &&handler) {
  nlohmann::json resp;
  if (!nlohmann::json::accept(req.body)) {
    spdlog::error("JSON语法检测未通过: {}", req.body);
    resp = {{"code", 2},
            {"msg", "JSON格式错误(语法检测未通过，请检查引号/逗号/转义符)"}};
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_content(resp.dump(), "application/json");
    return;
  }
  try {
    nlohmann::json j = nlohmann::json::parse(req.body);
    handler(j, res);
  } catch (const nlohmann::json::parse_error &e) {
    spdlog::error("JSON parse_error: {}", e.what());
    resp = {{"code", 2},
            {"msg", std::string("JSON格式错误(parse_error)：") + e.what()}};
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_content(resp.dump(), "application/json");
    return;
  } catch (const std::exception &e) {
    spdlog::error("JSON解析异常: {}", e.what());
    resp = {{"code", 2},
            {"msg", std::string("JSON格式错误(解析异常)：") + e.what()}};
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_content(resp.dump(), "application/json");
    return;
  } catch (...) {
    spdlog::error("JSON解析发生未知异常");
    resp = {{"code", 2},
            {"msg",
             "JSON格式错误("
             "未知异常，请检查请求体格式，必须为标准JSON且使用英文双引号)"}};
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_content(resp.dump(), "application/json");
    return;
  }
}
