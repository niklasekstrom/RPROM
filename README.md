# RPROM

![RPROM rendered](Docs/Images/rprom-rendered.jpg)

RPROM is a ROM emulator for 16 bit Amiga computers (A500, A600, A2000) based on
the RP2350B microcontroller.

The RPROM board has a 4 MB SPI flash memory. Each ROM image is 512 kB in size,
and therefore the 4 MB is divided into 8 *slots* of 512 kB each. The first slot
is dedicated to store code and configuration data for the RP2350B. The seven
remaining slots are used to store ROM images.

ROM images can be written to flash memory either via USB, or directly from the
Amiga using a program called `RPROM`. This `RPROM` program is also used to
switch which slot is the *active slot*.

This repository contains the [hardware](Hardware), [firmware](Firmware) and
[software](Software) for RPROM. The design is intended to be easily
understandable and hackable. For example, the firmware is built from a single,
roughly 300 lines long, [source code file](Firmware/rprom.c).

## Building

The hardware can be cheaply built by [JLCPCB](https://jlcpcb.com/).

## License

This design is free for non-comercial use. The designers take no responsibility
for if it doesn't work or if it causes damage to your equipment. If you want to
build boards to sell then you need to first ask for approval from the designers.
