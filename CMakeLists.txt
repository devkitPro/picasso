cmake_minimum_required(VERSION 3.5.0)
project(picasso VERSION 2.6.2)

add_library(picasso_assembler
    source/FileClass.h
    source/maestro_opcodes.h
    source/picasso_assembler.cpp
    source/types.h
)
target_include_directories(picasso_assembler
    PUBLIC
        source
)

add_executable(picasso
    source/picasso_frontend.cpp
)
target_compile_definitions(picasso
    PRIVATE
        PACKAGE_STRING="${CMAKE_PROJECT_NAME} ${PROJECT_VERSION}"
)
target_link_libraries(picasso
    PRIVATE
        picasso_assembler
)
