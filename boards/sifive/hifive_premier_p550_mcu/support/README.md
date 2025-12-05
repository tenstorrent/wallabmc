SiFive made this cfg public here:
  https://github.com/sifiveinc/hifive-premier-p550-tools/blob/master/mcu-firmware/stm32_openocd.cfg

So to program the this built zephyr image, do this:
  wget https://raw.githubusercontent.com/sifiveinc/hifive-premier-p550-tools/refs/heads/master/mcu-firmware/stm32_openocd.cfg
  openocd -f stm32_openocd.cfg -c 'init; halt; program zephyr.elf; reset; exit'

If you want to go back to the original firmware, do this:
  wget https://raw.githubusercontent.com/sifiveinc/hifive-premier-p550-tools/refs/heads/master/mcu-firmware/STM32F407VET6_BMC.elf
  openocd -f stm32_openocd.cfg -c 'init; halt; program STM32F407VET6_BMC.elf; reset; exit'