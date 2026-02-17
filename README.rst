.. image:: img/logo.png
   :alt: WallaBMC logo

========
WallaBMC
========

Overview
--------

WallaBMC is a simple BMC suitable for STM32 and similar class MCUs. It
is a Zephyr RTOS based application.

License
-------

Apache 2.0

Features
--------

* Blinky LEDs.
* IPv4 networking (static or DHCP).
* Redfish interface.
* HTTP/HTTP web interface.
* BMC console over serial or web.
* Persistent configuration.
* Host power control.
* Host console (coming soon).

Hardware support
----------------

* SiFive Hifive Premier P550 MCU.
* Nucleo F767ZI standalone board (no host CPU).

Building
--------

Prerequisites
~~~~~~~~~~~~~

Before getting started, make sure you have a proper Zephyr development
environment. Follow the official
`Zephyr getting started guide <https://docs.zephyrproject.org/latest/getting_started/index.html>`_.

Initialization
~~~~~~~~~~~~~~

The first step is to initialize the workspace folder (``workspace``)
where ``wallabmc`` and all Zephyr modules will be cloned. Run
the following command:

.. code-block:: sh

        # initialize workspace for wallabmc (main branch)
        west init -m https://github.com/tenstorrent/wallabmc --mr main workspace
        # update Zephyr modules
        cd workspace
        west update

Building
~~~~~~~~

To build the application, run the following command:

.. code-block:: sh

        cd wallabmc
        west build -b $BOARD app
        where $BOARD is the target board.

Supported boards:

* ``hifive_premier_p550_mcu``
* ``nucleo_f767zi``

Flashing
--------

.. code-block:: sh

        west flash --runner openocd

Alternatively, use the ``zephyr.elf`` file from the ``build/zephyr`` directory
and run the openocd command:

.. code-block::

        flash write_image erase zephyr.elf

Running
-------

When the system has booted, a slow-blinking status LED indicates the system
is running.

The Nucleo exposes an STM32 UART as a serial device over the USB port.
WallaBMC puts the BMC console on this serial device that displays boot
and log messages, and can be used to query and configure the device.

WallaBMC supports networking over ethernet and by default uses DHCP with the
hostname ``wallabmc`` to get an IP address.

WallaBMC opens an HTTP (and possibly HTTPS) port, which provides Redfish and
Web UI. The Web UI can also access the BMC console.

Settings and configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

* ``help`` command listing with hierarchical help (e.g., ``help config``).
* ``config`` shell command can be used to configure the BMC.
* ``power`` can power the host on and off. On the Nucleo board there is no
  host CPU so one of the LEDs is a stand-in for a host power GPIO.
