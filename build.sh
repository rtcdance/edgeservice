#!/bin/bash

# 项目构建脚本（静态链接 FFmpeg 版本，修复 404 问题）
set -eu
IFS=$'\n\t'

# 颜色输出定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # 无颜色

# 日志函数
log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

cleanup() {
    local exit_code=$?
    if [ $exit_code -ne 0 ]; then
        log_error "构建过程中发生错误，部分文件可能未正确生成"
    fi
    return $exit_code
}
trap cleanup EXIT

# 参数处理
BUILD_TYPE="Release"
BUILD_DIR="build"
INSTALL_DIR="$(pwd)/install"
TOOLCHAIN_FILE="./3rdparty/vcpkg/scripts/buildsystems/vcpkg.cmake"

# 自动适配架构的 FFmpeg 静态下载链接
ARCH=$(uname -m)
case "$ARCH" in
x86_64 | amd64)
    FFMPEG_STATIC_URL="https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-amd64-static.tar.xz"
    ;;
aarch64 | arm64)
    # 最新的 ARM64 静态编译包
    FFMPEG_STATIC_URL="https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linuxarm64-gpl.tar.xz"
    ;;
armv7l | armhf)
    # 32位 ARM 架构
    FFMPEG_STATIC_URL="https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-armhf-static.tar.xz"
    ;;
*)
    log_error "不支持的架构: $ARCH，仅支持 x86_64/ARM64/ARMv7l"
    exit 1
    ;;
esac

show_help() {
    echo "用法: $0 [选项]"
    echo "选项:"
    echo "  --debug         构建Debug版本"
    echo "  --clean         清理构建目录"
    echo "  --help          显示此帮助信息"
    exit 0
}

for arg in "$@"; do
    case $arg in
    --debug)
        BUILD_TYPE="Debug"
        shift
        ;;
    --clean)
        log_warn "清理构建目录: $BUILD_DIR"
        rm -rf "$BUILD_DIR"
        log_info "清理完成"
        exit 0
        ;;
    --help)
        show_help
        ;;
    *)
        log_error "未知参数: $arg"
        show_help
        ;;
    esac
done

# 创建安装目录
mkdir -p "$INSTALL_DIR"
log_info "安装目录: $INSTALL_DIR"

# 检查vcpkg工具链文件
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    log_error "vcpkg工具链文件不存在: $TOOLCHAIN_FILE"
    log_info "请确保vcpkg已正确初始化"
    exit 1
fi

# 进入 vcpkg 目录并初始化
VCPKG_DIR="3rdparty/vcpkg"
if [ -d "$VCPKG_DIR" ]; then
    log_info "进入 vcpkg 目录: $VCPKG_DIR"
    cd "$VCPKG_DIR"

    # 仅在必要时执行bootstrap（避免重复执行）
    if [ ! -f "vcpkg" ]; then
        log_info "执行 vcpkg 初始化脚本"
        sh bootstrap-vcpkg.sh
    else
        log_info "vcpkg 已初始化，跳过bootstrap"
    fi

    # 检查gRPC是否已安装，避免重复安装
    if ! ./vcpkg list | grep -q "grpc"; then
        log_info "使用 vcpkg 安装 gRPC"
        ./vcpkg install grpc
    else
        log_info "gRPC 已安装，跳过安装"
    fi
    cd -
else
    log_error "vcpkg 目录不存在: $VCPKG_DIR"
    exit 1
fi

# 构建项目
log_info "开始构建项目 (${BUILD_TYPE}模式)"

log_info "配置CMake..."
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 执行构建
log_info "执行构建..."
cmake --build "$BUILD_DIR" -- -j$(nproc)

# 执行安装
log_info "执行安装..."
cmake --install "$BUILD_DIR"

# 安装静态链接的 FFmpeg
log_info "开始安装静态 FFmpeg 到 ${INSTALL_DIR}/${BUILD_TYPE}"
FFMPEG_TARGET_DIR="${INSTALL_DIR}/${BUILD_TYPE}"
mkdir -p "$FFMPEG_TARGET_DIR"

# 自动下载预编译的静态 FFmpeg
log_info "下载静态 FFmpeg 二进制文件 (架构: $ARCH)..."
TEMP_DIR=$(mktemp -d)
ORIGINAL_DIR=$(pwd)
cd "$TEMP_DIR" || exit 1

# 下载并解压
log_info "下载 URL: $FFMPEG_STATIC_URL"
wget -q --show-progress "$FFMPEG_STATIC_URL" -O ffmpeg-static.tar.xz
if [ $? -ne 0 ]; then
    log_error "FFmpeg 下载失败，尝试备用链接..."

    # 针对不同架构的备用链接
    case "$ARCH" in
    aarch64 | arm64)
        FFMPEG_STATIC_URL="https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linuxarm64-gpl.tar.xz"
        ;;
    *)
        log_error "无可用的备用链接，请手动下载静态 FFmpeg"
        exit 1
        ;;
    esac

    wget -q --show-progress "$FFMPEG_STATIC_URL" -O ffmpeg-static.tar.xz
    if [ $? -ne 0 ]; then
        log_error "所有备用链接均失败，请检查网络或手动安装 FFmpeg"
        exit 1
    fi
fi

tar -xJf ffmpeg-static.tar.xz

# 查找静态二进制文件（适配不同压缩包结构）
FFMPEG_BIN=$(find . -type f -name "ffmpeg" | head -n 1)

if [ -z "$FFMPEG_BIN" ]; then
    # 尝试在 bin 目录中查找
    FFMPEG_BIN=$(find . -type d -name "bin" -print0 | xargs -0 -I {} find {} -type f -name "ffmpeg" | head -n 1)

    if [ -z "$FFMPEG_BIN" ]; then
        log_error "解压后未找到 FFmpeg 二进制文件"
        find . -type f # 显示所有文件用于调试
        exit 1
    fi
fi

cp "$FFMPEG_BIN" "$FFMPEG_TARGET_DIR/ffmpeg"
log_info "静态 FFmpeg 已复制到 $FFMPEG_TARGET_DIR"

cd "$ORIGINAL_DIR" || exit 1
rm -rf "$TEMP_DIR"

# 设置执行权限
chmod +x "$FFMPEG_TARGET_DIR/ffmpeg"

# 验证安装
log_info "验证 FFmpeg 安装..."
"$FFMPEG_TARGET_DIR/ffmpeg" -version >/dev/null 2>&1
if [ $? -eq 0 ]; then
    log_info "FFmpeg 安装成功"
else
    log_error "FFmpeg 无法正常运行，请检查依赖或手动安装"
    exit 1
fi

# 构建完成
log_info "==========================="
log_info "项目构建完成，可执行文件已安装到 ${INSTALL_DIR}/${BUILD_TYPE}"
log_info "==========================="
