# edgeservice

## 一、项目介绍

### 项目结构与核心特性

本项目采用 C++ 语言进行开发，提供了对边缘客户端服务 RESTful 和 RPC 两种风格接口的支持。以下是该项目所包含的具体接口信息。

## RESTful 接口

### 1. 同步截图上传校验接口

- **接口地址**：`http://example.com:18080/api/camera/capture_and_check`
- **请求方式**：POST
- **请求体示例**：

```json
{
    "camera_id": 0,
    "rtsp_url": "rtsp://example.com:8554/mymp4"
}
```

- **功能说明**：此接口用于执行同步的截图操作，将截取的图片进行上传，并对上传的图片进行校验。

- **返回内容示例**：

```json
{
    "code": "100000",
    "int_id": 188,
    "matched": false,
    "score": 0.03682351139848264,
    "status": "success",
    "user_id": "(未知 ID: 188)"
}
```

---

### 2. 异步上传校验接口

- **接口地址**：`http://example.com:18080/api/camera/async_capture_and_check`
- **请求方式**：POST
- **请求体示例**：

```json
{
    "timeout": 5,
    "cameras": [
        {
            "camera_id": 0,
            "rtsp_url": "rtsp://example.com:8554/mymp4"
        }
    ]
}
```

- **功能说明**：该接口支持异步方式进行截图、上传以及校验操作，在处理大量数据或耗时较长的任务时，可以提高系统的响应性能。

- **返回内容示例**：

```json
{
    "code": 0,
    "msg": "任务已提交",
    "task_id": "87E763B9-3FBB-4E18-B6FC-BB17295B3C53"
}
```

---

### 3. 异步校验结果查询接口

- **接口地址**：`http://example.com:18080/api/camera/async_capture_and_check_result?task_id=87E763B9-3FBB-4E18-B6FC-BB17295B3C53`
- **请求方式**：GET
- **功能说明**：当使用异步上传校验接口发起任务后，可通过向此接口传入任务 ID，查询异步上传校验任务的执行结果。

- **返回内容示例**：

```json
{
    "code": 0,
    "msg": "自动巡检完成",
    "results": [
        {
            "camera_id": 0,
            "code": "100000",
            "int_id": 188,
            "matched": false,
            "score": 0.03682351139848264,
            "status": "success",
            "user_id": "(未知 ID: 188)"
        }
    ],
    "task_id": "87E763B9-3FBB-4E18-B6FC-BB17295B3C53"
}
```

---

### 4. 播放 URL 生成接口

- **接口地址**：`http://example.com:18080/api/camera/gen_play_url`
- **请求方式**：POST
- **请求体示例**：

```json
{
    "camera_id": 1,
    "play_host": "example.com",
    "play_port": 8080
}
```

- **功能说明**：通过调用此接口，能够生成用于播放的 URL，方便用户获取播放资源。

- **返回内容示例**：

```json
{
    "play_url": "http://example.com:8080/live/1"
}
```

---

## RPC 接口

具体参照 edgeservice 项目根目录下 `proto/edgeservice.proto` 文件，接口内容与 RESTful 一致，此处不赘述。

---

## 二、源码编译

### 克隆代码仓库

若要获取项目源码并编译，可按以下步骤操作：

1. 克隆项目源码并拉取依赖的子模块：

```bash
git clone --recurse-submodules git@github.com:rtcdance/edgeservice.git
```

### 编译

2. 进入项目目录：

```bash
cd edgeservice
```

3. 执行构建脚本进行编译：

```bash
sh build.sh
```

---

## 三、启动服务

进入安装目录并启动项目：

```bash
cd install
./edgeservice
```

以上步骤完成后，项目即可正常运行。
