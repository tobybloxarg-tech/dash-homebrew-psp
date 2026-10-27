cmake_minimum_required(VERSION 3.11)
project(dash_homebrew C)
add_executable(${PROJECT_NAME} main.c)
target_link_libraries(${PROJECT_NAME} PRIVATE pspgu pspge pspdisplay pspctrl)
create_pbp_file(
    TARGET ${PROJECT_NAME}
    ICON_PATH NULL
    BACKGROUND_PATH NULL
    PREVIEW_PATH NULL
    TITLE "Dash Homebrew"
    VERSION 01.00
)
