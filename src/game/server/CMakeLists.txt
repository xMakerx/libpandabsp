file (GLOB SRCS "*.cpp")
file (GLOB HDRS "*.h")
file (GLOB SHARED_SRCS "../shared/*.cpp")
file (GLOB SHARED_HDRS "../shared/*.h")

source_group("Header Files" FILES ${HDRS})
source_group("Source Files" FILES ${SRCS})
source_group("Header Files\\shared" FILES ${SHARED_HDRS})
source_group("Source Files\\shared" FILES ${SHARED_SRCS})

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type.")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DEBUG")
string(REPLACE "/DNDEBUG" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -wd4275 /Zi")

if (DEFINED LINK_ALL_STATIC)
    set(P3BUILT built_x64_static)
else()
    set(P3BUILT built_x64)
endif ()

set(SV_PANDA3D_DIR ..\\..\\..\\cio-panda3d\\${P3BUILT} CACHE PATH "Panda3D directory")
set(SV_BSP_COMMON_INCLUDE ..\\..\\..\\libpandabsp-compilers\\common CACHE PATH "BSP Common directory")
set(SV_VIF_INCLUDE ..\\..\\..\\keyvalue-parser\\include CACHE PATH "keyvalue-parser include directory")
set(SV_BULLET_DIR ..\\..\\..\\cio-panda3d\\thirdparty\\win-libs-vc14-x64\\bullet CACHE PATH "Bullet thirdparty dir")
set(SV_GNS_INCLUDE ..\\..\\..\\GameNetworkingSockets\\include CACHE PATH "Path to Valve GameNetworking Sockets include dir")
set(SV_GNS_LIB ..\\..\\..\\GameNetworkingSockets\\build\\src\\RelWithDebInfo\\GameNetworkingSockets CACHE FILEPATH "Path to Valve GameNetworking Sockets lib file")
set(SV_P3BSP_DIR ..\\.. CACHE PATH "libpandabsp dir")

include_directories(./ ./../shared ${SV_PANDA3D_DIR}/include ${SV_BSP_COMMON_INCLUDE}
				    ${SV_VIF_INCLUDE} ${SV_BULLET_DIR}/include ${SV_GNS_INCLUDE} ${SV_P3BSP_DIR}/engine)
link_directories(${SV_PANDA3D_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64 bits
    add_definitions(-DVERSION_64BIT)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32 bits
    add_definitions(-DVERSION_32BIT)
endif()

add_definitions(-DNOMINMAX -DBUILDING_SERVER_DLL -DWITHIN_PANDA -DWITHINPANDA -DSTDC_HEADERS -DCIO -DSERVER_DLL)
if (DEFINED LINK_ALL_STATIC)
    set(LIB_TYPE STATIC)
    set(OUT_EXT .lib)
    add_definitions(-DLINK_ALL_STATIC)
else ()
    set(LIB_TYPE SHARED)
    set(OUT_EXT .dll)
endif ()

add_library(server ${LIB_TYPE} ${SRCS} ${HDRS} ${UI_SRCS} ${UI_HDRS} ${SHARED_SRCS} ${SHARED_HDRS})

set(sv_suff ${OUT_EXT})
set(sv_out server)

set_target_properties(server PROPERTIES SUFFIX ${sv_suff})
set_target_properties(server PROPERTIES OUTPUT_NAME ${sv_out})

string(REGEX REPLACE "/" "\\\\" SV_WIN_PANDA3D_DIR ${SV_PANDA3D_DIR})

if (DEFINED LINK_ALL_STATIC)
    add_custom_command(TARGET server POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${sv_out}${sv_suff} ${SV_WIN_PANDA3D_DIR}\\lib\\${sv_out}${sv_suff})
else()
    add_custom_command(TARGET server POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${sv_out}${sv_suff} ${SV_WIN_PANDA3D_DIR}\\bin\\${sv_out}${sv_suff})
    add_custom_command(TARGET server POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${sv_out}.pdb ${SV_WIN_PANDA3D_DIR}\\bin\\${sv_out}.pdb)
    add_custom_command(TARGET server POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${sv_out}.lib ${SV_WIN_PANDA3D_DIR}\\lib\\${sv_out}.lib)
endif()

target_link_libraries(server libpanda.lib
                            libpandaexpress.lib
					        libp3dtool.lib
					        libp3dtoolconfig.lib
					        libp3direct.lib
                            bsp_common.lib
                            libvif.lib
							libpandabullet.lib
                            engine.lib
                            ${SV_GNS_LIB})

add_dependencies(server engine)