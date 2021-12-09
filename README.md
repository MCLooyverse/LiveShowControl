# Live Show Control #

This is a program for programmatically controlling as many aspects of a live
show as are practical.  Designed specifically for the live performances of
[We, Montana!](https://wemontana.org) Skits Works, but with a view toward being
as general as possible.


## Requirements ##

- C header `yaml-cpp/yaml.h` from package [yaml-cpp](https://github.com/jbeder/yaml-cpp)

For audio control:
- Executable `play` from package [sox](http://sox.sourceforge.net/)

For DMX control:
- Module `dmx_usb` from repository [dmx_usb_module](https://github.com/lowlander/dmx_usb_module) (See Notes)

### Notes on Requirements ###

When compiling from `dmx_usb_module`, note that its Makefile does not escape
the working directory well and can cause errors, so if you're compiling from a
path with spaces, apostrophes, etc. you'll want to move somewhere else, or
change the Makefile yourself.
