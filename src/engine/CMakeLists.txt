project(libpandabsp)

file (GLOB SRCS "*.cpp")
file (GLOB HEADERS "*.h")
file (GLOB KD_SRCS "kdtree/*.cpp")
file (GLOB KD_HDRS "kdtree/*.h")
file (GLOB PP_HDRS "postprocess/*.h")
file (GLOB PP_SRCS "postprocess/*.cpp")

source_group("Header Files" FILES ${HEADERS})
source_group("Source Files" FILES ${SRCS})
source_group("Source Files\\kdtree" FILES ${KD_SRCS})
source_group("Header Files\\kdtree" FILES ${KD_HDRS})
source_group("Source Files\\postprocess" FILES ${PP_SRCS})
source_group("Header Files\\postprocess" FILES ${PP_HDRS})

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

set(EMBREE_DIR ../../embree-3.5.0.x64.vc14.windows CACHE PATH "Embree install directory")
set(PANDA3D_DIR ..\\..\\cio-panda3d\\${P3BUILT} CACHE PATH "Panda3D directory")
set(BSP_COMMON_INCLUDE ..\\..\\libpandabsp-compilers\\common CACHE PATH "BSP Common directory")
set(VIF_INCLUDE ..\\..\\keyvalue-parser\\include CACHE PATH "keyvalue-parser include directory")
set(BULLET_DIR ..\\..\\cio-panda3d\\thirdparty\\win-libs-vc14-x64\\bullet CACHE PATH "Bullet thirdparty dir")

include_directories(./ ${PANDA3D_DIR}/include ${BSP_COMMON_INCLUDE}
				    ${VIF_INCLUDE} ${BULLET_DIR}/include)
link_directories(${PANDA3D_DIR}/lib)
link_directories(${EMBREE_DIR}/lib)
include_directories(${EMBREE_DIR}/include)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64 bits
    add_definitions(-DVERSION_64BIT)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    # 32 bits
    add_definitions(-DVERSION_32BIT)
endif()

add_definitions(-DNOMINMAX -DBUILDING_LIBPANDABSP -DWITHIN_PANDA -DWITHINPANDA -DSTDC_HEADERS -DCIO)
if (DEFINED LINK_ALL_STATIC)
    set(LIB_TYPE STATIC)
    set(OUT_EXT .lib)
    add_definitions(-DLINK_ALL_STATIC)
else ()
    set(LIB_TYPE SHARED)
    set(OUT_EXT .dll)
endif ()

add_library(engine ${LIB_TYPE} ${SRCS} ${HEADERS} ${KD_SRCS} ${KD_HDRS}
			${PP_SRCS} ${PP_HDRS})

set(bsp_suff ${OUT_EXT})
set(bsp_out engine)

set_target_properties(engine PROPERTIES SUFFIX ${bsp_suff})
set_target_properties(engine PROPERTIES OUTPUT_NAME ${bsp_out})

string(REGEX REPLACE "/" "\\\\" WIN_PANDA3D_DIR ${PANDA3D_DIR})

#add_custom_command(TARGET libpandabsp PRE_BUILD COMMAND interrogate.bat)

if (DEFINED LINK_ALL_STATIC)
    add_custom_command(TARGET engine POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${bsp_out}${bsp_suff} ${WIN_PANDA3D_DIR}\\lib\\${bsp_out}${bsp_suff})
else()
    add_custom_command(TARGET engine POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${bsp_out}${bsp_suff} ${WIN_PANDA3D_DIR}\\bin\\${bsp_out}${bsp_suff})
    add_custom_command(TARGET engine POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${bsp_out}.pdb ${WIN_PANDA3D_DIR}\\bin\\${bsp_out}.pdb)
    add_custom_command(TARGET engine POST_BUILD COMMAND copy /Y ${CMAKE_BUILD_TYPE}\\${bsp_out}.lib ${WIN_PANDA3D_DIR}\\lib\\${bsp_out}.lib)
endif()

target_link_libraries(engine libpanda.lib
                            libpandaexpress.lib
					        libp3dtool.lib
					        libp3dtoolconfig.lib
					        libp3direct.lib
						    libp3framework.lib
                            libpandaegg.lib
                            embree3.lib
                            tbb.lib
                            tbbmalloc.lib
							libpandagl.lib
                            bsp_common.lib
                            libvif.lib
							libpandabullet.lib)
