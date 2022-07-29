cmake_minimum_required( VERSION 2.8 )

PROJECT(shell)

include_directories(
${PROJECT_SOURCE_DIR}
)

add_executable(shell myshell.c)