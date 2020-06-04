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

This module may _partially_ work with other third-party projects for
controlling other aspects of the device such the LED ilumination
(tested with
[liquidctl](https://github.com/jonasmalacofilho/liquidctl)). This is
because the hidraw endpoint of the device is connected even when the
module is working, for allowing this tools to work. It has been
observed that some operations like the initialization of the device
that the liquidctl project does, might give some issues with the
module (still investigating), so it is recommended only use this other
projects for accessing the device for doing operations that the module
does not support (seems to work fine for changing leds and that suff
anyway).

## License

Licensed under [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html).
