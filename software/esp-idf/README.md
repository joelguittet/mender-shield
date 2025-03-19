# mender-shield | esp-idf

[![CI Badge](https://github.com/joelguittet/mender-shield/workflows/ci/badge.svg)](https://github.com/joelguittet/mender-shield/actions)
[![Issues Badge](https://img.shields.io/github/issues/joelguittet/mender-shield)](https://github.com/joelguittet/mender-shield/issues)
[![License Badge](https://img.shields.io/github/license/joelguittet/mender-shield)](https://github.com/joelguittet/mender-shield/blob/master/LICENSE)

[Mender MCU client](https://github.com/joelguittet/mender-mcu-client) is an open source over-the-air (OTA) library updater for MCU devices. This demonstration project is used to demonstrate usage of mender-shield on ESP32 hardware.


## Getting started

This project is used with the [Wemos D1 r32](../../.github/docs/Wemos-D1-R32.pdf) ESP32-based evaluation board with 4MB flash. The installation of the mender-shield is done using the Arduino connector of the evaluation board.

The project is built using ESP-IDF framework. There is no other dependencies. Important note: the project has been tested with ESP-IDF v5.4.x successfully.

This project is developed under [VSCode](https://code.visualstudio.com) and using [ESP-IDF Extension](https://github.com/espressif/vscode-esp-idf-extension). If you need support to use the development environment, you can refer to the "ESP-IDF Extension help" section below.

Building and flashing the firmware on the device is done like for [mender-esp32-example](https://github.com/joelguittet/mender-esp32-example) that demonstrates the usage of mender-mcu-client on ESP32 MCU to perform upgrade with dual OTA partitions and rollback capability using ESP-IDF framework. Please refer to this example for more information about mender support in ESP-IDF environment.

To start using Mender, we recommend that you begin with the Getting started section in [the Mender documentation](https://docs.mender.io/).

### Open the project

Clone the project and retrieve submodules using `git submodule update --init --recursive`.

Then open the project with VSCode.

### Configuration of the application

The example application should first be configured to set at least:
- `CONFIG_MENDER_SERVER_TENANT_TOKEN` to set the Tenant Token of your account on "https://hosted.mender.io" server;
- `CONFIG_EXAMPLE_WIFI_SSID` and `CONFIG_EXAMPLE_WIFI_PASSWORD` to connect the device to your own WiFi access point.

You may want to customize few interesting settings:
- `CONFIG_MENDER_SERVER_HOST` if using your own Mender server instance. Tenant Token is not required in this case.
- `CONFIG_MENDER_CLIENT_AUTHENTICATION_POLL_INTERVAL` is the interval to retry authentication on the mender server.
- `CONFIG_MENDER_CLIENT_UPDATE_POLL_INTERVAL` is the interval to check for new deployments.
- `CONFIG_MENDER_CLIENT_INVENTORY_REFRESH_INTERVAL` is the interval to publish inventory data.
- `CONFIG_MENDER_CLIENT_CONFIGURE_REFRESH_INTERVAL` is the interval to refresh device configuration.

Other settings are available in the Kconfig. You can also refer to the mender-mcu-client API and configuration keys.


## Current status

The following states are handled by the application:
* Idle: the Mender logo is displayed on the front in night mode.
* Sync: the Mender logo is displayed on the front in day mode.
* Download: the Mender logo is blinking slowly.
* ArtifactInstall: the Mender logo is blinking fastly.

Additionally, the back side of the shield is blue when the Wi-Fi interface is connected, green if disconnected, red in case of errors.


## ESP-IDF Extension help

### Building

Using VSCode, first open the directory of the project.

Then go to Command Palette (Ctrl+Shift+P) and execute the `ESP-IDF: Set Espressif device target` command to select select an Espressif target. Choose `esp32`, then select `Custom board`.

Next execute the `ESP-IDF: SDK Configuration editor` command (Ctrl+E then G) to generate sdkconfig and optionally modify project settings. After all changes are made, click save and close this window.

Now build the project with the `ESP-IDF: Build your project` command (Ctrl+E then B). Progress is displayed in the terminal.

Binary is generated in the build directory.

You may optionally want to display the size of the executable with `ESP-IDF: Size analysis of the binaries` command (Ctrl+E then S).

### Flashing

First select the wanted serial port corresponding to the target with the `ESP-IDF: Select port to use` command (Ctrl+E then P).

Then invoke the `ESP-IDF: Flash your project` command (Ctrl+E then F) to flash the target. Choose `UART` option.

### Monitoring

Logs are displayed using the `ESP-IDF: Monitor your device` command (Ctrl+E then M).


## License

Copyright joelguittet and mender-mcu-client contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
