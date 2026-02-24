include(FetchContent)

# 1. 尝试在系统中查找已安装的 spdlog
find_package(spdlog QUIET CONFIG)

if(NOT TARGET spdlog::spdlog)
    message(STATUS "spdlog not found locally, fetching from GitHub...")
    
    # 2. 如果本地没有，自动从 GitHub 下载
    FetchContent_Declare(
      spdlog
      GIT_REPOSITORY https://github.com/gabime/spdlog.git
      GIT_TAG        v1.12.0  # 建议锁定一个稳定版本
      GIT_SHALLOW    TRUE      # 只下载最新提交，加快速度
    )

    FetchContent_MakeAvailable(spdlog)
    
    # FetchContent 会自动创建 spdlog::spdlog 目标
    set(SPDLOG_TARGET spdlog::spdlog)
    set(SPDLOG_FOUND TRUE)
else()
    message(STATUS "Found spdlog locally.")
    set(SPDLOG_TARGET spdlog::spdlog)
    set(SPDLOG_FOUND TRUE)
endif()

# 保持你原有的 Handle 逻辑（可选）
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Spdlog DEFAULT_MSG SPDLOG_TARGET)
