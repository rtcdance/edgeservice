#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include "grpc_server.h"
#include "http_server.h"
#include "inspect_impl.h"
#include "utils/config_utils.h"
#include "utils/http_utils.h"

static void init_spdlog() {
  try {
    std::string log_path = get_executable_dir() + "/log/edgeservice.log";
    auto logger = spdlog::rotating_logger_mt(
        "edgeservice", log_path, 20 * 1024 * 1024, 5);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [thread %t] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
  } catch (const spdlog::spdlog_ex &ex) {
    std::cerr << "Log init failed: " << ex.what() << std::endl;
  }
}

int main(int argc, char *argv[]) {
  init_spdlog();
  std::string config_path;
  bool has_config = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if ((arg == "-c") && i + 1 < argc) {
      config_path = argv[++i];
      has_config = true;
      break;
    }
  }
  if (!has_config) {
    std::cerr << "用法: " << argv[0] << " -c <config_path>\n";
    return 1;
  }
  load_global_config(config_path);
  int rest_port = 18080;
  int grpc_port = 50051;
  const auto &conf = get_config();
  if (conf.contains("rest_port")) rest_port = conf["rest_port"].get<int>();
  if (conf.contains("grpc_port")) grpc_port = conf["grpc_port"].get<int>();
  start_inspect_task_worker();
  // 启动RESTful HTTP服务
  HttpServer server(rest_port);
  std::thread http_thread([&server]() { server.start(); });
  // 启动gRPC服务
  std::thread grpc_thread(
      [grpc_port]() { RunGrpcServer("0.0.0.0:" + std::to_string(grpc_port)); });
  http_thread.join();
  grpc_thread.join();
  stop_inspect_task_worker();
  return 0;
}
