# m5stack-freertos-ota
Amazon FreeRTOS OTA demo for M5Stack

Require ESP-IDF development environment.



### Run

Check out this repo.

```
git clone https://github.com/fukuen/m5stack-freertos-ota --recursive
```

Set or copy Wi-Fi infos.


Build (Windows example)

```
cmake -DCMAKE_TOOLCHAIN_FILE=amazon-freertos/tools/cmake/toolchains/xtensa-esp32.cmake -GNinja -S . -B build
cd build
ninja
```

Flash and monitor

```
idf.py flash monitor
```
