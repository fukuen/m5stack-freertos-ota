cmake_minimum_required(VERSION 3.13)

project(freertos-ex)

add_executable(freertos-ex src/main.c
 amazon-freertos/demos/demo_runner/aws_demo.c
 amazon-freertos/demos/demo_runner/aws_demo_network_addr.c
 amazon-freertos/demos/demo_runner/aws_demo_version.c
 amazon-freertos/demos/demo_runner/iot_demo_freertos.c
 src/iot_runner.c
 src/iot_application.c
 amazon-freertos/demos/network_manager/aws_iot_network_manager.c
 amazon-freertos/demos/ble/iot_ble_numericComparison.c
 )
target_include_directories(freertos-ex
  PUBLIC
  include
  amazon-freertos/libraries/freertos_plus/standard/pkcs11/include
  amazon-freertos/libraries/3rdparty/pkcs11
  amazon-freertos/libraries/freertos_plus/standard/utils/include
  amazon-freertos/demos/network_manager
  amazon-freertos/libraries/abstractions/wifi/include
  amazon-freertos/libraries/c_sdk/standard/https/include
  amazon-freertos/libraries/c_sdk/aws/shadow/include
)

# Tell IDF build to link against this target.
set(IDF_PROJECT_EXECUTABLE freertos-ex)

# Add amazon freertos as a subdirectory. AFR_BOARD tells which board to target.
set(AFR_BOARD espressif.esp32_wrover_kit CACHE INTERNAL "")
add_subdirectory("amazon-freertos")

# Link against the mqtt library so that we can use it. Dependencies
# linked.
target_link_libraries(freertos-ex PRIVATE AFR::mqtt AFR::wifi AFR::ble AFR::dev_mode_key_provisioning AFR::https AFR::shadow AFR::pkcs11 AFR::pkcs11_implementation AFR::ota)
