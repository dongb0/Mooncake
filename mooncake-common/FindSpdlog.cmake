find_package(spdlog QUIET CONFIG)

if(TARGET spdlog::spdlog)
    set(SPDLOG_FOUND TRUE)
    set(SPDLOG_TARGET spdlog::spdlog)
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_SPDLOG QUIET spdlog)
    endif()

    find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h
        HINTS ${PC_SPDLOG_INCLUDEDIR} ${PC_SPDLOG_INCLUDE_DIRS}
        PATHS /usr/include /usr/local/include)

    find_library(SPDLOG_LIBRARY spdlog
        HINTS ${PC_SPDLOG_LIBDIR} ${PC_SPDLOG_LIBRARY_DIRS}
        PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/local/lib64)

    if(SPDLOG_INCLUDE_DIR AND SPDLOG_LIBRARY)
        set(SPDLOG_FOUND TRUE)
        add_library(spdlog::spdlog INTERFACE IMPORTED)
        target_include_directories(spdlog::spdlog INTERFACE ${SPDLOG_INCLUDE_DIR})
        target_link_libraries(spdlog::spdlog INTERFACE ${SPDLOG_LIBRARY})
        set(SPDLOG_TARGET spdlog::spdlog)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Spdlog DEFAULT_MSG SPDLOG_TARGET)
