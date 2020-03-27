file (GLOB SRCS "*.cpp")
file (GLOB HDRS "*.h")
file (GLOB UI_SRCS "ui/*.cpp")
file (GLOB UI_HDRS "ui/*.h")
file (GLOB SHARED_SRCS "../shared/*.cpp")
file (GLOB SHARED_HDRS "../shared/*.h")

source_group("Header Files" FILES ${HDRS})
source_group("Source Files" FILES ${SRCS})
source_group("Header Files\\shared" FILES ${SHARED_HDRS})
source_group("Source Files\\shared" FILES ${SHARED_SRCS})
source_group("Header Files\\ui" FILES ${UI_HDRS})
source_group("Source Files\\ui" FILES ${UI_SRCS})

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

set(C_PANDA3D_DIR ..\\..\\..\\cio-panda3d\\${P3BUILT} CACHE PATH "Panda3D directory")
set(C_BSP_COMMON_INCLUDE ..\\..\\..\\libpandabsp-compilers\\common CACHE PATH "BSP Common directory")
set(C_VIF_INCLUDE ..\\..\\..\\keyvalue-parser\\include CACHE PATH "keyvalue-parser include directory")
set(C_BULLET_DIR ..\\..\\..\\cio-panda3d\\thirdparty\\win-libs-vc14-x64\\bullet CACHE PATH "Bullet thirdparty dir")
set(C_GNS_INCLUDE ..\\..\\..\\GameNetworkingSockets\\include CACHE PATH "Path to Valve GameNetworking Sockets include dir")
set(C_GNS_LIB ..\\..\\..\\GameNetworkingSockets\\build\\src\\RelWithDebInfo\\GameNetworkingSockets CACHE FILEPATH "Path to Valve GameNetworking Sockets lib file")
set(C_FREETYPE_DIR ..\\..\\..\\cio-panda3d\\thirdparty\\win-libs-vc14-x64\\freetype CACHE PATH "Freetype thirdparty dir")
set(C_P3BSP_DIR ..\\.. CACHE PATH "libpandabsp dir")

include_directories(./ ./../shared ${C_PANDA3D_DIR}/include ${C_BSP_COMMON_INCLUDE}
				    ${C_VIF_INCLUDE} ${C_BULLET_DIR}/include ${C_GNS_INCLUDE} ${C_FREETYPE_DIR}/include/freetype2 ${C_P3BSP_DIR}/engine)
link_directories(${C_PANDA3D_DIR}/lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64 bits
    add_definitions(-DVERSION_64BIT)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32 bits
    add_definitions(-DVERSION_32BIT)
endif()

add_definitions(-DNOMINMAX -DBUILDING_CLIENT_DLL -DWITHIN_PANDA -DWITHINPANDA -DSTDC_HEADERS -DCIO -DCLIENT_DLL)
if (DEFINED LINK_ALL_STATIC)
    set(LIB_TYPE STATIC)
    set(OUT_EXT .lib)
    add_definitions(-DLINK_ALL_STATIC)
else ()
    set(LIB_TYPE SHARED)
    set(OUT_EXT .dll)
endif ()

add_library(client ${LIB_TYPE} ${SRCS} ${HDRS} ${UI_SRCS} ${UI_HDRS} ${SHARED_SRCS} ${SHARED_HDRS})

set(cl_suff ${OUT_EXT})
set(cl_out client)

set_target_properties(client PROPERTIES SUFFIX ${cl_suff})
set_target_properties(client PROPERTIES OUTPUT_NAME ${cl_out})

string(REGEX REPLACE "/" "\\\\" C_WIN_PANDA3D_DIR ${C_PANDA3D_DIR})

if (DEFINED LINK_ALL_STATIC)
    add_custom_command(TARGET client POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${cl_out}${cl_suff} ${C_WIN_PANDA3D_DIR}\\lib\\${cl_out}${cl_suff})
else()
    add_custom_command(TARGET client POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${cl_out}${cl_suff} ${C_WIN_PANDA3D_DIR}\\bin\\${cl_out}${cl_suff})
    add_custom_command(TARGET client POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${cl_out}.pdb ${C_WIN_PANDA3D_DIR}\\bin\\${cl_out}.pdb)
    add_custom_command(TARGET client POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${cl_out}.lib ${C_WIN_PANDA3D_DIR}\\lib\\${cl_out}.lib)
endif()

target_link_libraries(client libpanda.lib
                            libpandaexpress.lib
					        libp3dtool.lib
					        libp3dtoolconfig.lib
					        libp3direct.lib
                            bsp_common.lib
                            libvif.lib
							libpandabullet.lib
                            engine.lib
                            ${C_GNS_LIB})

add_dependencies(client engine)
