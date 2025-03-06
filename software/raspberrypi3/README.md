# mender-shield | Raspberry Pi 3

The scripts provided in this directory are intended to demonstrate the usage of the mender-shield on Raspberry Pi 3 device.


## Getting started

The installation of the device should be done according to Mender documentation [Prepare a Raspberry Pi device](https://docs.mender.io/get-started/preparation/prepare-a-raspberry-pi-device) to properly setup and connect the Raspberry Pi to the Mender server.

The installation of the mender-shield is done using the 40 pins connector of the Raspberry Pi 3. Some spacers and screws are used to fix it on the device.

It is possible to verify the HAT is properly detected by running the following commands.

```
pi@raspberrypi:~ $ i2cdetect -y 0
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --

pi@raspberrypi:~ $ i2cdetect -y 1
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: 60 -- -- -- -- -- -- -- 68 69 -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --
```

Where:
* 0x50 is the address of the HAT ID EEPROM connected on i2c-0.
* 0x50 is the address of the User EEPROM connected on i2c-1.
* 0x60 is the address of the ATECC608B connected on i2c-1.
* 0x68 and 0x69 are the addresses of the LED drivers connected on i2c-1.

Once the installation has been performed, you have to copy the `mender-shield-script` provided in this directory to the Raspberry Pi 3 device. Ensure it is executable.
Then create symlinks `/etc/mender/scripts/Idle_Enter_00` and `/etc/mender/scripts/Sync_Enter_10` to reference the `mender-shield-script` itself. Please be sure to respect the paths on the device, the default location for the State scripts is '/etc/mender/scripts'. See relevant documentation [State scripts](https://docs.mender.io/artifact-creation/state-scripts).

Reboot the device.


## Current status

Only the following states are managed by the script:
* Idle: the Mender logo is displayed on the front in night mode.
* Sync: the Mender logo is displayed on the front in day mode.

Additionally, the back side of the shield is blue if the mender server is reachable, red otherwise.

This could be enhanced to do nice demonstration and visual effects when an update is in progress.
