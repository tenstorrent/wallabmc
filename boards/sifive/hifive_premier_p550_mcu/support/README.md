# Quick installation

SiFive made an openocd cfg public [here](https://github.com/sifiveinc/hifive-premier-p550-tools/blob/master/mcu-firmware/stm32_openocd.cfg)

To program the this built zephyr image, do this:


```
wget https://raw.githubusercontent.com/sifiveinc/hifive-premier-p550-tools/refs/heads/master/mcu-firmware/stm32_openocd.cfg
openocd -f stm32_openocd.cfg -c 'init; halt; program zephyr.elf; reset; exit'
```

You can download the WallaBMC p550 zephyr.elf from a sucessful github actions run from [here](https://github.com/tenstorrent/wallabmc/actions)
Look for the `wallabmc-firmware-hifive_premier_p550_mcu` artifact (github doesn't provide a static link)

Or you can build it yourself from source here with:

```
west build -b hifive_premier_p550_mcu app
```

# Flash standard firmware from Sifive

If you want to go back to the original firmware, do this:

```
wget https://raw.githubusercontent.com/sifiveinc/hifive-premier-p550-tools/refs/heads/master/mcu-firmware/stm32_openocd.cfg
wget https://raw.githubusercontent.com/sifiveinc/hifive-premier-p550-tools/refs/heads/master/mcu-firmware/STM32F407VET6_BMC.elf
openocd -f stm32_openocd.cfg -c 'init; halt; program STM32F407VET6_BMC.elf; reset; exit'
```