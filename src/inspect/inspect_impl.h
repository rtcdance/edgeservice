#pragma once
#include <string>
#include <vector>

#include "3rdparty/include/json/include/nlohmann/json.hpp"

// 单摄像头截图+AI校验
nlohmann::json capture_and_check(uint64_t camera_id,
                                 const std::string &rtsp_url);

// 支持多任务并发巡检
std::string auto_inspect_async(
    const std::vector<std::pair<uint64_t, std::string>> &cameras,
    int timeout_sec, nlohmann::json &result);
nlohmann::json get_inspect_result(const std::string &task_id);

// 保存和查询巡检结果
void save_inspect_result(const nlohmann::json &result);
nlohmann::json get_last_inspect_result();

// 启动定期清理线程
void start_inspect_result_cleaner();

// 启动/停止异步任务后台线程
void start_inspect_task_worker();
void stop_inspect_task_worker();
