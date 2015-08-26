StkBoot boot loader offers the possiblity to download
application software via the serial UART interface into the
internal flash memory of Atmel AVR ATmega microcontrollers,
without the need for an ISP adapter. The used protocol is
STK500 compatible.

Implemented protocol is secure: no code from flash memory can
be read until the chip is erased.

Memory size for atmega128 is 2024 bytes.

The sources could be downloaded by command:
```
  git clone https://github.com/sergev/stkboot.git
```

StkBoot is based on:
 * [avrusb500](http://tuxgraphics.org/electronics/200510/article05101.shtml) programmer sources, Copyright (c) 2005 Guido Socher
 * [ATmegaBOOT](http://www.chip45.com/index.pl?page=ATmegaBOOT&lang=en) loader sources, Copyright (c) 2003 Jason P. Kyle
