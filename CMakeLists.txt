cmake_minimum_required(VERSION 3.20)
project(TotallyNormalKeyboard LANGUAGES CXX)

# Use C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Source files
set(SRC
    src/tnk.cpp
)

# Executable
add_executable(tnk ${SRC})

# Link pthread
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(tnk PRIVATE Threads::Threads)

# Extra warnings for GCC/Clang
target_compile_options(tnk PRIVATE -Wall -Wextra -pthread)

# Install target binary to /usr/local/sbin
install(TARGETS tnk DESTINATION sbin)

# Install helper scripts to /usr/local/sbin
install(PROGRAMS
    src/usb-gadget.sh
    DESTINATION sbin
)

# --- Service file installation ---
set(SERVICE_NAME usbgadget.service)
set(SERVICE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/usbgadget.service.in)
set(SERVICE_DESTINATION /usr/lib/systemd/system)  # system-wide path

# Configure service file (replace placeholders)
configure_file(${SERVICE_SRC} ${CMAKE_CURRENT_BINARY_DIR}/${SERVICE_NAME} @ONLY)

# Install the configured service file
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${SERVICE_NAME} DESTINATION ${SERVICE_DESTINATION})
