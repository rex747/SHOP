cmake_minimum_required(VERSION 3.20)
project(TerminalKioskClient VERSION 1.0 LANGUAGES CXX)

# Требование стандарта C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Указываем, что мы используем vcpkg (дублируем для надежности)
set(CMAKE_TOOLCHAIN_FILE "C:/Users/dpc/Desktop/SHOP/server/client/packages/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

# Поиск пакета SQLite3, установленного через vcpkg
find_package(SQLite3 REQUIRED)

# Добавляем исполняемый файл и все ваши исходники
add_executable(kiosk_client WIN32
    main.cpp
    database_local.cpp
    # Добавьте сюда остальные ваши .cpp файлы, если они есть
)

# Привязываем библиотеку SQLite3 к нашему исполняемому файлу
# Это автоматически добавит и include-директории, и линковку .lib файла
target_link_libraries(kiosk_client PRIVATE SQLite::SQLite3)

# Дополнительные опции компилятора для Windows
target_compile_options(kiosk_client PRIVATE /W4 /permissive-)