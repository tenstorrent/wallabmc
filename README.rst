Simple stm32 based BMC for Atlantis
===================================

Prototyping with the Nucleo F767ZI board

Building
========

Install zephyr as per the upstream instructions, check out atlantis-bmc
in applications/ and then run:

west build -p always -b nucleo_f767zi applications/atlantis-bmc
west flash --runner openocd

Running
=======

The demo hardwires the IP to 192.0.2.1. Telnet to it, enter ctrl-] then "mode char"
to get it into char mode. You can then run commands such as power on, power off,
reset. LED0 is the power GPIO, LED1 is the reset GPIO.
