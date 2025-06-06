#include "inspect_impl.h"

#include <uuid/uuid.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "3rdparty/include/cpp-httplib/httplib.h"
#include "3rdparty/include/cppcodec/cppcodec/base64_rfc4648.hpp"
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include "utils/config_utils.h"
#include "utils/http_utils.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string get_executable_dir() {
#if defined(__APPLE__)
  char path[1024] = "";
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) == 0) {
    return fs::path(path).parent_path().string();
  }
  return ".";
#elif defined(__linux__)
  char path[1024] = "";
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len > 0) {
    path[len] = '\0';
    return fs::path(path).parent_path().string();
  }
  return ".";
#else
  return ".";
#endif
}

static std::string get_ffmpeg_path() {
  static std::string path = get_executable_dir() + "/ffmpeg";
  return path;
}

static std::string get_snapshot_dir() {
  static std::string dir = get_executable_dir() + "/snapshot";
  return dir;
}

// 巡检结果存储：支持多任务并发
static std::mutex g_result_mutex;
static std::unordered_map<std::string, json> g_inspect_results;

// 定期清理过期巡检结果（保留1小时内的）
static std::unordered_map<std::string, std::chrono::steady_clock::time_point>
    g_inspect_time;
static std::atomic<bool> g_cleaner_running{false};

// 异步任务结构体
struct InspectTask {
  std::string task_id;
  std::vector<std::pair<uint64_t, std::string>> cameras;
  int timeout_sec;
};

static std::queue<InspectTask> g_task_queue;
static std::mutex g_task_mutex;
static std::condition_variable g_task_cv;
static std::atomic<bool> g_task_thread_running{false};
static std::atomic<bool> g_task_thread_exit{false};

// 辅助函数：调用ffmpeg截图，返回base64字符串
static std::string capture_image_ffmpeg(const std::string &rtsp_url,
                                        uint64_t camera_id) {
  fs::create_directories(get_snapshot_dir());
  std::string out_path =
      get_snapshot_dir() + "/" + std::to_string(camera_id) + ".jpg";
  std::stringstream cmd;
  // 判断协议类型，rtsp支持-rtsp_transport tcp，http-flv不加该参数
  if (rtsp_url.find("rtsp://") == 0) {
    cmd << get_ffmpeg_path() << " -y -rtsp_transport tcp -i '" << rtsp_url
        << "' -frames:v 1 -q:v 2 -f image2 '" << out_path << "' 2>/dev/null";
  } else {
    cmd << get_ffmpeg_path() << " -y -i '" << rtsp_url
        << "' -frames:v 1 -q:v 2 -f image2 '" << out_path << "' 2>/dev/null";
  }
  int ret = std::system(cmd.str().c_str());
  if (ret != 0) return "";
  std::ifstream ifs(out_path, std::ios::binary);
  std::vector<uint8_t> buffer(std::istreambuf_iterator<char>(ifs), {});
  if (buffer.empty()) {
    fs::remove(out_path);
    return "";
  }
  std::string base64 = cppcodec::base64_rfc4648::encode(buffer);
  fs::remove(out_path);
  return base64;
}

// POST图片到AI校验服务
static json post_to_ai_service(const std::string &base64_img) {
  const auto &conf = get_config();
  std::string host = conf.value("ai_service_host", "124.70.8.249");
  int port = conf.value("ai_service_port", 1055);
  httplib::Client cli(host, port);
  json req_body = {{"image_base64", base64_img}};
  auto res = cli.Post("/v1/eyes/exists", req_body.dump(), "application/json");
  if (res && (res->status == 200 || res->status == 400)) {
    return json::parse(res->body);
  } else {
    return json{{"code", 500}, {"msg", "AI服务请求失败"}};
  }
}

json capture_and_check(uint64_t camera_id, const std::string &rtsp_url) {
  json resp;
  if (rtsp_url.empty()) {
    resp = {{"code", 1}, {"msg", "参数缺失"}};
  } else {
    std::string base64_img = capture_image_ffmpeg(rtsp_url, camera_id);
    if (base64_img.empty()) {
      resp = {{"code", 3}, {"msg", "截图失败"}};
    } else {
      json ai_result = post_to_ai_service(base64_img);
      std::string ai_code = "-1";
      std::string ai_msg = "AI服务未知错误";
      if (ai_result.is_object() && ai_result.contains("code")) {
        if (ai_result["code"].is_string()) {
          ai_code = ai_result["code"];
        } else if (ai_result["code"].is_number_integer()) {
          ai_code = std::to_string(ai_result["code"].get<int>());
        } else if (ai_result["code"].is_number_float()) {
          ai_code = std::to_string(ai_result["code"].get<double>());
        }
      }
      if (ai_result.is_object() && ai_result.contains("msg")) {
        if (ai_result["msg"].is_string()) {
          ai_msg = ai_result["msg"];
        }
      }
      if (ai_code == "100000") {
        resp = ai_result;
      } else if (ai_code == "200220") {
        resp = {{"code", 200220}, {"msg", "AI未检测到目标(可重试)"}};
      } else {
        int code_int = -1;
        if (is_digits(ai_code)) {
          try {
            code_int = std::stoi(ai_code);
          } catch (...) {
            code_int = -1;
          }
        }
        resp = {{"code", code_int}, {"msg", ai_msg}};
      }
    }
  }
  return resp;
}

// 后台任务处理线程
void start_inspect_task_worker() {
  if (g_task_thread_running.exchange(true)) return;
  g_task_thread_exit = false;
  std::thread([]() {
    while (true) {
      InspectTask task;
      {
        std::unique_lock<std::mutex> lock(g_task_mutex);
        g_task_cv.wait(
            lock, [] { return g_task_thread_exit || !g_task_queue.empty(); });
        if (g_task_thread_exit && g_task_queue.empty()) break;
        if (g_task_queue.empty()) continue;
        task = g_task_queue.front();
        g_task_queue.pop();
      }
      // 处理任务
      json resp;
      std::vector<json> results;
      auto start = std::chrono::steady_clock::now();
      for (const auto &cam : task.cameras) {
        uint64_t camera_id = cam.first;
        const std::string &rtsp_url = cam.second;
        auto now = std::chrono::steady_clock::now();
        int remain =
            task.timeout_sec -
            std::chrono::duration_cast<std::chrono::seconds>(now - start)
                .count();
        if (remain <= 0) {
          results.push_back(
              {{"code", 408}, {"msg", "轮检超时"}, {"camera_id", camera_id}});
          continue;
        }
        // 单摄像头处理
        json one_result = capture_and_check(camera_id, rtsp_url);
        one_result["camera_id"] = camera_id;
        results.push_back(one_result);
      }
      resp = {{"code", 0},
              {"msg", "自动巡检完成"},
              {"results", results},
              {"task_id", task.task_id}};
      {
        std::lock_guard<std::mutex> lock(g_result_mutex);
        g_inspect_results[task.task_id] = resp;
        g_inspect_time[task.task_id] = std::chrono::steady_clock::now();
      }
    }
  }).detach();
}

void stop_inspect_task_worker() {
  g_task_thread_exit = true;
  g_task_cv.notify_all();
}

std::string auto_inspect_async(
    const std::vector<std::pair<uint64_t, std::string>> &cameras,
    int timeout_sec, json &result) {
  std::string task_id = generate_uuid();
  // 任务入队
  {
    std::lock_guard<std::mutex> lock(g_task_mutex);
    g_task_queue.push(InspectTask{task_id, cameras, timeout_sec});
  }
  g_task_cv.notify_one();
  // 立即返回task_id
  result = {{"code", 0}, {"msg", "任务已提交"}, {"task_id", task_id}};
  return task_id;
}

// 查询指定任务ID的巡检结果
json get_inspect_result(const std::string &task_id) {
  std::lock_guard<std::mutex> lock(g_result_mutex);
  auto it = g_inspect_results.find(task_id);
  if (it != g_inspect_results.end()) return it->second;
  return json{{"code", 404}, {"msg", "未找到该巡检任务"}};
}

void start_inspect_result_cleaner() {
  if (g_cleaner_running.exchange(true)) return;
  std::thread([]() {
    while (true) {
      std::this_thread::sleep_for(std::chrono::minutes(10));
      auto now = std::chrono::steady_clock::now();
      std::lock_guard<std::mutex> lock(g_result_mutex);
      for (auto it = g_inspect_time.begin(); it != g_inspect_time.end();) {
        if (std::chrono::duration_cast<std::chrono::hours>(now - it->second)
                .count() >= 1) {
          g_inspect_results.erase(it->first);
          it = g_inspect_time.erase(it);
        } else {
          ++it;
        }
      }
    }
  }).detach();
}
