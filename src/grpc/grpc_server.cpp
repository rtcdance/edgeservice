#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "edgeservice.grpc.pb.h"
#include "inspect_impl.h"
#include "utils/http_utils.h"

using edgeservice::AsyncCaptureRequest;
using edgeservice::AsyncCaptureResponse;
using edgeservice::CameraInfo;
using edgeservice::CameraService;
using edgeservice::CaptureRequest;
using edgeservice::CaptureResponse;
using edgeservice::GenPlayUrlRequest;
using edgeservice::GenPlayUrlResponse;
using edgeservice::GetResultRequest;
using edgeservice::GetResultResponse;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class CameraServiceImpl final : public CameraService::Service {
  Status CaptureAndCheck(ServerContext *context, const CaptureRequest *request,
                         CaptureResponse *response) override {
    auto result = capture_and_check(static_cast<uint64_t>(request->camera_id()),
                                    request->rtsp_url());
    response->set_code(result.value("code", 1));
    response->set_msg(result.value("msg", ""));
    return Status::OK;
  }

  Status AsyncCaptureAndCheck(ServerContext *context,
                              const AsyncCaptureRequest *request,
                              AsyncCaptureResponse *response) override {
    std::vector<std::pair<uint64_t, std::string>> cameras;
    for (const auto &cam : request->cameras()) {
      cameras.emplace_back(cam.camera_id(), cam.rtsp_url());
    }
    int timeout = request->timeout();
    nlohmann::json result;
    std::string task_id = auto_inspect_async(cameras, timeout, result);
    response->set_code(result.value("code", 1));
    response->set_msg(result.value("msg", ""));
    response->set_task_id(task_id);
    return Status::OK;
  }

  Status GetAsyncCaptureAndCheckResult(ServerContext *context,
                                       const GetResultRequest *request,
                                       GetResultResponse *response) override {
    const std::string &task_id = request->task_id();
    if (task_id.empty() || !is_valid_uuid(task_id)) {
      response->set_code(1);
      response->set_msg("task_id必须为合法的uuid字符串");
      response->set_task_id(task_id);
      return Status::OK;
    }
    auto result = get_inspect_result(task_id);
    response->set_code(result.value("code", 1));
    response->set_msg(result.value("msg", ""));
    response->set_task_id(result.value("task_id", ""));
    if (result.contains("results") && result["results"].is_array()) {
      for (const auto &r : result["results"]) {
        auto *res = response->add_results();
        uint64_t camera_id = 0;
        std::string err_msg;
        if (r.contains("camera_id")) {
          if (!parse_camera_id(r, "camera_id", camera_id, err_msg)) {
            res->set_camera_id(0);
            res->set_code(400);  // client参数错误
            res->set_msg("camera_id非法: " + err_msg);
            continue;
          }
        }
        res->set_camera_id(camera_id);
        if (r.contains("code") && r["code"].is_number_integer()) {
          res->set_code(r["code"].get<int>());
        } else {
          res->set_code(1);
        }
        if (r.contains("msg") && r["msg"].is_string()) {
          res->set_msg(r["msg"].get<std::string>());
        } else if (err_msg.size() > 0) {
          res->set_msg(err_msg);
        } else {
          res->set_msg("");
        }
      }
    }
    return Status::OK;
  }

  Status GenPlayUrl(ServerContext *context, const GenPlayUrlRequest *request,
                    GenPlayUrlResponse *response) override {
    uint64_t camera_id = request->camera_id();
    std::string play_host = request->play_host();
    int play_port = request->play_port();
    if (play_host.empty() || play_port <= 1024 || play_port > 65535 ||
        !is_valid_host(play_host)) {
      response->set_code(1);
      response->set_msg("参数不合法，play_host必须为合法的IP或域名");
      return Status::OK;
    }
    std::string play_url = "http://" + play_host + ":" +
                           std::to_string(play_port) + "/live/" +
                           std::to_string(camera_id);
    response->set_play_url(play_url);
    response->set_code(0);
    response->set_msg("");
    return Status::OK;
  }
};

void RunGrpcServer(const std::string &address) {
  CameraServiceImpl service;
  ServerBuilder builder;
  builder.AddListeningPort(address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  spdlog::info("[edgeservice] gRPC server listening on {}", address);
  server->Wait();
}
