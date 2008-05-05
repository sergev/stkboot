PROGRAM		= stkboot
CC		= avr-gcc410 -g -Wall -mmcu=$(MCU)
OBJCOPY		= avr-objcopy410
OBJDUMP		= avr-objdump410
CFLAGS		= -Os -I$(HOME)/Project/uos/sources \
		  -DKHZ=$(KHZ) -DBAUDRATE=$(BAUDRATE) -DBADDR=$(BADDR)
LDFLAGS		= -nostdlib -T$(MCU).x -Wl,-Map,$(PROGRAM).map,--section-start=.text=$(BADDR)
DIVISOR		= $(shell expr \( $(KHZ) \* 1000 / $(BAUDRATE) + 8 \) / 16 - 1)

# Set the target CPU, oscillator frequency, baud rate and boot reset address
all:
		$(MAKE) MCU=atmega128 KHZ=14746 BAUDRATE=115200 BADDR=0x1F800 compile
		$(MAKE) MCU=atmega128 KHZ=10000 BAUDRATE=38400 BADDR=0x1F800 compile

compile:	$(PROGRAM).c
		$(CC) $(CFLAGS) -c $(PROGRAM).c
		$(CC) $(LDFLAGS) -o $(PROGRAM).elf $(PROGRAM).o
		$(OBJDUMP) -h -S $(PROGRAM).elf > $(MCU)-$(DIVISOR).lst
		$(OBJCOPY) -j .text -j .data -O srec $(PROGRAM).elf $(MCU)-$(DIVISOR).sre
		@chmod -x $(MCU)-$(DIVISOR).sre
		@rm -f $(PROGRAM).o $(PROGRAM).elf

clean:
		rm -rf *~ *.o *.elf *.lst *.map *.sym *.lss *.eep
#		rm -rf *.hex *.sre *.bin
