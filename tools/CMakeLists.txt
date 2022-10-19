cmake_minimum_required(VERSION 3.5)
project(liboculus)

add_executable(oculus_client oculus_client.cpp)
target_link_libraries(oculus_client PRIVATE libg3logger liboculus)

