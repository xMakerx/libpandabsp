cmake_minimum_required (VERSION 2.6)

project(libpandabsp)

add_subdirectory(./engine)
add_subdirectory(./game/client)
add_subdirectory(./game/server)

include_external_msproject(test_client ./test_client/test_client.vcxproj)
include_external_msproject(test_server ./test_server/test_server.vcxproj)
