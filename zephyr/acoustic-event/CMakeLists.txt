cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(acoustic_event)
target_sources(app PRIVATE src/main.c)

# USB-console builds can enter the Arduino bootloader without a button press.
if(CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT)
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common/arduino_bootloader_touch.c)
endif()
