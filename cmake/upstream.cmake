# 上游接入：libvpx（configure + make）→ <build>/pkg/prefix，探针链接静态 libvpx。

set(PKG_ROOT "${CMAKE_BINARY_DIR}/pkg")
if(NOT DEFINED PKG_PREFIX)
  set(PKG_PREFIX "${PKG_ROOT}/prefix")
  add_custom_target(pkg ALL
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pkg-build.sh" "${SRC_DIR}" "${PKG_ROOT}" "${RVV_MARCH}" "$<IF:$<BOOL:${RVV_KERNELS}>,1,0>"
    COMMENT "构建上游 libvpx（${SRC_DIR}）"
    VERBATIM)
  set(HAVE_PKG_TARGET TRUE)
endif()

add_executable(vpx_harness test/pkg_vpx.c)
if(HAVE_PKG_TARGET)
  add_dependencies(vpx_harness pkg)
endif()
target_include_directories(vpx_harness PRIVATE "${PKG_PREFIX}/include")
target_link_directories(vpx_harness PRIVATE "${PKG_PREFIX}/lib")
target_link_libraries(vpx_harness PRIVATE vpx pthread m)
target_compile_options(vpx_harness PRIVATE ${RVV_FLAGS})

add_test(NAME vpx_harness COMMAND vpx_harness)
