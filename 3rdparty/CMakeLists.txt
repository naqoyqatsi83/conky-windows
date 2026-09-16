# Vc version 1.4.4
add_subdirectory(Vc)

add_subdirectory(toluapp)
add_subdirectory(spdlog)

if(BUILD_NVIDIA_NVML)
  add_subdirectory(nvml)
endif(BUILD_NVIDIA_NVML)

if(BUILD_LUA_CAIRO AND OS_WINDOWS)
  add_subdirectory(cairo)
endif(BUILD_LUA_CAIRO AND OS_WINDOWS)

if(BUILD_CURL AND OS_WINDOWS)
  add_subdirectory(curl)
endif(BUILD_CURL AND OS_WINDOWS)
