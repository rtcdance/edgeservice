#include "http_server.h"

#include <spdlog/spdlog.h>

#include <codecvt>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

#include "3rdparty/include/cppcodec/cppcodec/base64_rfc4648.hpp"
#include "3rdparty/include/json/include/nlohmann/json.hpp"
#include "inspect_impl.h"
#include "utils/http_utils.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

HttpServer::HttpServer(int port) : port_(port) {
  uint32_t n = std::thread::hardware_concurrency();
  if (n == 0) n = 4;
  server_.new_task_queue = [n] { return new httplib::ThreadPool(n); };
  start_inspect_result_cleaner();
}

void HttpServer::setup_routes() {
  // 允许所有路径的OPTIONS预检请求
  server_.Options(
      R"(/.*)", [](const httplib::Request &, httplib::Response &res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.set_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        res.status = 204;
      });
  // 同步截图校验接口
  server_.Post("/api/camera/capture_and_check", [](const httplib::Request &req,
                                                   httplib::Response &res) {
    handle_json_post(req, res, [](const json &j, httplib::Response &res) {
      json resp;
      uint64_t camera_id = 0;
      std::string err_msg;
      if (!parse_camera_id(j, "camera_id", camera_id, err_msg)) {
        resp = {{"code", 1}, {"msg", err_msg}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(resp.dump(), "application/json");
        return;
      }
      std::string rtsp_url;
      if (j.contains("rtsp_url") && j["rtsp_url"].is_string()) {
        rtsp_url = j["rtsp_url"].get<std::string>();
      } else {
        resp = {{"code", 1}, {"msg", "rtsp_url必须为字符串"}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(resp.dump(), "application/json");
        return;
      }
      if (rtsp_url.empty() || !is_valid_url(rtsp_url)) {
        resp = {{"code", 1}, {"msg", "rtsp_url必须为合法的URL"}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(resp.dump(), "application/json");
        return;
      }
      resp = capture_and_check(camera_id, rtsp_url);
      res.set_header("Access-Control-Allow-Origin", "*");
      res.set_content(resp.dump(), "application/json");
    });
  });

  // 异步批量截图校验接口，返回task_id
  server_.Post(
      "/api/camera/async_capture_and_check",
      [](const httplib::Request &req, httplib::Response &res) {
        handle_json_post(req, res, [](const json &j, httplib::Response &res) {
          json resp;
          std::vector<std::pair<uint64_t, std::string>> cameras;
          int timeout_sec = 10;
          // timeout 必须为字符串且为纯数字
          if (j.contains("timeout")) {
            if (j["timeout"].is_string()) {
              std::string timeout_str = j["timeout"].get<std::string>();
              if (!is_digits(timeout_str)) {
                resp = {{"code", 1}, {"msg", "timeout必须为数字字符串"}};
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(resp.dump(), "application/json");
                return;
              }
              timeout_sec = std::stoi(timeout_str);
            } else if (j["timeout"].is_number_integer()) {
              timeout_sec = j["timeout"].get<int>();
            } else {
              resp = {{"code", 1}, {"msg", "timeout字段类型不支持"}};
              res.set_header("Access-Control-Allow-Origin", "*");
              res.set_content(resp.dump(), "application/json");
              return;
            }
          }
          if (j.contains("cameras") && j["cameras"].is_array()) {
            for (const auto &cam : j["cameras"]) {
              if (!cam.is_object()) {
                resp = {{"code", 1}, {"msg", "cameras数组元素必须为对象"}};
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(resp.dump(), "application/json");
                return;
              }
              uint64_t camera_id = 0;
              std::string err_msg;
              if (!parse_camera_id(cam, "camera_id", camera_id, err_msg)) {
                resp = {{"code", 1}, {"msg", err_msg}};
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(resp.dump(), "application/json");
                return;
              }
              std::string rtsp_url;
              if (cam.contains("rtsp_url") && cam["rtsp_url"].is_string()) {
                rtsp_url = cam["rtsp_url"].get<std::string>();
              } else {
                resp = {{"code", 1}, {"msg", "rtsp_url必须为字符串"}};
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(resp.dump(), "application/json");
                return;
              }
              if (rtsp_url.empty() || !is_valid_url(rtsp_url)) {
                resp = {{"code", 1}, {"msg", "rtsp_url必须为合法的URL"}};
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_content(resp.dump(), "application/json");
                return;
              }
              cameras.emplace_back(camera_id, rtsp_url);
            }
            json result;
            std::string task_id =
                auto_inspect_async(cameras, timeout_sec, result);
            resp = result;
          } else {
            resp = {{"code", 1}, {"msg", "参数缺失或格式错误"}};
          }
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
        });
      });

  // 查询指定异步批量校验结果接口
  server_.Get(
      "/api/camera/async_capture_and_check_result",
      [](const httplib::Request &req, httplib::Response &res) {
        std::string task_id;
        if (!req.has_param("task_id")) {
          json resp = {{"code", 1}, {"msg", "task_id参数缺失"}};
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
          return;
        }
        task_id = req.get_param_value("task_id");
        if (task_id.empty()) {
          json resp = {{"code", 1}, {"msg", "task_id不能为空"}};
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
          return;
        }
        // task_id 必须为合法 uuid
        if (!is_valid_uuid(task_id)) {
          json resp = {{"code", 1}, {"msg", "task_id必须为合法的uuid字符串"}};
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
          return;
        }
        spdlog::info("收到/async_capture_and_check_result查询: {}", task_id);
        json result = get_inspect_result(task_id);
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(result.dump(), "application/json");
      });

  // 生成播放地址接口（根据camera_id生成）
  server_.Post("/api/camera/gen_play_url", [](const httplib::Request &req,
                                              httplib::Response &res) {
    handle_json_post(req, res, [](const json &j, httplib::Response &res) {
      json resp;
      uint64_t camera_id = 0;
      std::string err_msg;
      if (!parse_camera_id(j, "camera_id", camera_id, err_msg)) {
        resp = {{"code", 1}, {"msg", err_msg}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(resp.dump(), "application/json");
        return;
      }
      std::string play_host;
      if (j.contains("play_host") && j["play_host"].is_string()) {
        play_host = j["play_host"].get<std::string>();
        if (!is_valid_host(play_host)) {
          resp = {{"code", 1}, {"msg", "play_host必须为合法的IP或域名"}};
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
          return;
        }
      } else {
        resp = {{"code", 1}, {"msg", "play_host必须存在且为字符串"}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(resp.dump(), "application/json");
        return;
      }
      uint16_t play_port = 0;
      if (j.contains("play_port")) {
        int port_val = 0;
        if (j["play_port"].is_number_integer()) {
          port_val = j["play_port"].get<int>();
        } else if (j["play_port"].is_string()) {
          std::string port_str = j["play_port"].get<std::string>();
          if (is_digits(port_str)) {
            port_val = std::stoi(port_str);
          } else {
            resp = {{"code", 1}, {"msg", "play_port必须为整型或数字字符串"}};
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(resp.dump(), "application/json");
            return;
          }
        } else {
          resp = {{"code", 1}, {"msg", "play_port必须为整型或数字字符串"}};
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
          return;
        }
        // 检查端口范围，防止负数和溢出
        if (port_val <= 1024 || port_val > 65535) {
          resp = {{"code", 1}, {"msg", "play_port必须在1025~65535之间"}};
          res.set_header("Access-Control-Allow-Origin", "*");
          res.set_content(resp.dump(), "application/json");
          return;
        }
        play_port = static_cast<uint16_t>(port_val);
      }
      std::string play_url = "http://" + play_host + ":" +
                             std::to_string(play_port) + "/live/" +
                             std::to_string(camera_id);
      if (!is_valid_url(play_url)) {
        resp = {{"code", 1}, {"msg", "生成的play_url不合法"}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(resp.dump(), "application/json");
        return;
      }
      resp = {{"play_url", play_url}};
      res.set_header("Access-Control-Allow-Origin", "*");
      res.set_content(resp.dump(), "application/json");
    });
  });
}

void HttpServer::start() {
  setup_routes();
  spdlog::info("[edgeservice] HTTP server listening on port {}", port_);
  server_.listen("0.0.0.0", port_);
}
