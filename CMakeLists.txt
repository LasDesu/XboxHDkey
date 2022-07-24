cmake_minimum_required(VERSION 3.0.0)
project(xboxhdkey VERSION 0.1.0)

include(CTest)
enable_testing()

add_executable(xboxhdkey apt.c sgio.c sysfs.c BootEEPROM.c BootHddKey.c rc4.c sha1.c main.c)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
