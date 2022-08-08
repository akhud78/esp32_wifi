# esp32_wifi
Wi-Fi component

## Links

- [Wi-Fi Station Example](https://github.com/espressif/esp-idf/tree/v4.4/examples/wifi/getting_started/station)
- [station_example_main.c](https://github.com/espressif/esp-idf/blob/v4.4/examples/wifi/getting_started/station/main/station_example_main.c)
- [About the example_connect() Function](https://github.com/espressif/esp-idf/tree/v4.4/examples/protocols#about-the-example_connect-function)
- [connect.c](https://github.com/espressif/esp-idf/blob/v4.4/examples/common_components/protocol_examples_common/connect.c)

## Setup
- Add [wi-fi component](https://github.com/akhud78/esp32_wifi) into `components` folder.
```
$ cd ~/esp/my_project/components
$ git submodule add https://github.com/akhud78/esp32_wifi wifi
```


## Configuration

- Set Wi-Fi Station Configuration
```
(Top) -> Component config -> WiFi Station Configuration
(myssid) WiFi SSID
(mypassword) WiFi Password
(5) Maximum retry
```
- WiFi Access Point Configuration
```
(Top) -> Component config -> WiFi Access Point Configuration
(myssid) WiFi SSID
(mypassword) WiFi Password
(1) WiFi Channel
(4) Maximal STA connections
```


