cmake_minimum_required(VERSION 3.2)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK")
  endif()
endif()

project(VPKManager C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VPK Manager")
set(VITA_TITLEID "VPKM00001")
set(VITA_VERSION "01.20")

add_executable(${PROJECT_NAME}
  src/main.c
  src/ziputil.c
  src/sfo.c
  src/net_receiver.c
)

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  SceSysmodule_stub
  ScePgf_stub
  ScePvf_stub
  ScePromoterUtil_stub
  SceAppMgr_stub
  SceIofilemgr_stub
  SceNet_stub
  SceNetCtl_stub
  SceLibKernel_stub
  png
  jpeg
  z
  m
  c
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
  FILE pkg/sce_sys/icon0.png sce_sys/icon0.png
FILE pkg/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
FILE pkg/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
FILE pkg/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
