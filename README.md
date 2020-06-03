# krknmon
### NZXT Kraken 4th generation AIO cooler (X53, X63, X73) HWMon driver

**This module is made as an attempt to learn about developing modules
for the kernel, and it is likely unstable. You've been warned.**

Said that, now you can continue reading.

krknmon is a kernel module that defines an HWMon device for monitoring
a NZXT Kraken X53/X63/X73 All-In-One cooler device. This module
exposes the pump speed of the device, the liquid temperature and
(TODO) allows to modify the speed of the pump. Since it uses the hwmon
interface, these values are available directly form the _sysfs_
filesystem or from hardware monitoring programs such as _lm_sensors_.

This module has been tested on Linux kernel versions 5.4/5.6 using a
NZXT Kraken X63 device. It _should_ work on other devices of the same
generation, such as X53 and X73, but this is far from being
guaranteed. This module is **not** compatible with older devices (X52,
X62, X72).

Note also that, this device uses the same USB interface for both
reporting the status and setting aesthetics like color effects and
that stuff, when this module is loaded, you won't be able to use any
other program to communicate with it (for example, [liquidctl](https://github.com/jonasmalacofilho/liquidctl)).

## License

Licensed under [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html).
