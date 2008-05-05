/*
 * Serial Bootloader for Atmel AVR Controllers.
 * Uses STK500 protocol (application note AVR068).
 * Copyright (C) 2006 Serge Vakulenko
 *
 * Based on:
 * - avrusb500 programmer sources, Copyright (c) 2005 Guido Socher
 * - ATmegaBOOT loader sources, Copyright (c) 2003 Jason P. Kyle
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software
 * Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write
 * to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Licence can be viewed at
 * http://www.fsf.org/licenses/gpl.txt
 */
#include "runtime/avr/io.h"
#include "stk500.h"

#define CONFIG_PARAM_BUILD_NUMBER_LOW   0
#define CONFIG_PARAM_BUILD_NUMBER_HIGH  1
#define CONFIG_PARAM_HW_VER             2
#define CONFIG_PARAM_SW_MAJOR           2 /* update here our own sw version */
#define CONFIG_PARAM_SW_MINOR           2
#define CONFIG_PARAM_VTARGET		50
#define CONFIG_PARAM_VADJUST		25
#define CONFIG_PARAM_OSC_PSCALE		2
#define CONFIG_PARAM_OSC_CMATCH		1

#define MSG_IDLE			0
#define MSG_WAIT_SEQNUM			1
#define MSG_WAIT_SIZE1			2
#define MSG_WAIT_SIZE2			3
#define MSG_WAIT_TOKEN			4
#define MSG_WAIT_MSG			5
#define MSG_WAIT_CKSUM			6

/*
 * Define various device id's
 */
#if defined __AVR_ATmega128__
#define SIG2		0x97
#define SIG3		0x02
#define PAGE_SIZE	0x80U	/* 128 words */

#elif defined __AVR_ATmega64__
#define SIG2		0x96
#define SIG3		0x02
#define PAGE_SIZE	0x80U	/* 128 words */

#elif defined __AVR_ATmega32__
#define SIG2		0x95
#define SIG3		0x02
#define PAGE_SIZE	0x40U	/* 64 words */

#elif defined __AVR_ATmega16__
#define SIG2		0x94
#define SIG3		0x03
#define PAGE_SIZE	0x40U	/* 64 words */

#elif defined __AVR_ATmega8__
#define SIG2		0x93
#define SIG3		0x07
#define PAGE_SIZE	0x20U	/* 32 words */

#elif defined __AVR_ATmega88__
#define SIG2		0x93
#define SIG3		0x0a
#define PAGE_SIZE	0x20U	/* 32 words */

#elif defined __AVR_ATmega168__
#define SIG2		0x94
#define SIG3		0x06
#define PAGE_SIZE	0x40U	/* 64 words */

#elif defined __AVR_ATmega162__
#define SIG2		0x94
#define SIG3		0x04
#define PAGE_SIZE	0x40U	/* 64 words */

#elif defined __AVR_ATmega163__
#error ATmega163 is not supported!

#elif defined __AVR_ATmega169__
#define SIG2		0x94
#define SIG3		0x05
#define PAGE_SIZE	0x40U	/* 64 words */

#elif defined __AVR_ATmega8515__
#define SIG2		0x93
#define SIG3		0x06
#define PAGE_SIZE	0x20U	/* 32 words */

#elif defined __AVR_ATmega8535__
#define SIG2		0x93
#define SIG3		0x08
#define PAGE_SIZE	0x20U	/* 32 words */
#endif

unsigned char msg_buf [295];
unsigned short nbytes;
unsigned short word0;
unsigned char chip_erased;
unsigned char param_sck_duration;
unsigned char param_reset_polarity;
unsigned char param_controller_init;
unsigned short poly_tab [16];

union {
	unsigned long dword;
	struct {
		unsigned short low;
		unsigned short high;
	} word;
	unsigned char byte [4];
} address;

void uart_init (void);
void uart_putchar (char c);
unsigned char uart_getchar (void);
unsigned short program_cmd (void);
void transmit_answer (unsigned char seqnum, unsigned short len);
void page_erase (unsigned long addr);
void page_write (void);
unsigned char read_byte (void);
unsigned short crc16 (unsigned short sum, unsigned char byte);

/*
 * Load a byte from the program memory (flash).
 */
#define lpm(addr) ({					\
	register unsigned char t;			\
	asm volatile (					\
		"lpm" "\n"				\
		"mov    %0,r0"				\
		: "=r" (t)				\
		: "z" ((short)addr));			\
	t; })

#define elpm(addr) ({					\
	register unsigned char t;			\
	asm volatile (					\
		"elpm" "\n"				\
		"mov    %0,r0"				\
		: "=r" (t)				\
		: "z" ((short)addr));			\
	t; })

/*
 * Store a byte to the program memory (flash).
 */
#define spm(addr) 					\
	asm volatile (					\
		"spm" "\n"				\
		: : "z" ((short)addr))

#define load_r0r1(word) 				\
	asm volatile (					\
		"movw r0,%0" 				\
		: : "r" ((short)word) : "r0", "r1")

/*
 * Start here on reset.
 */
asm ("ldi r24, 0");
asm ("ldi r25, 0");

/*
 * Enter here from user program, witn non-zero argument.
 */
asm ("jmp main");

int main (int warmboot, char **dummy)
{
	unsigned char ch, msgparsestate, cksum, seqnum;
	unsigned short msglen, i;

	/* Disable interrupts */
	cli ();

	/* Clear zero register */
	asm volatile ("clr __zero_reg__");

	/* On cold boot, if memory is not empty - start from 0 */
	if (! warmboot && lpm(0) != 0xFF)
		((void (*) ()) 0) ();

	/* Disable watchdog */
	asm volatile ("wdr");
	WDTCR = 3 << WDE;
	WDTCR = 0;

	uart_init ();
	uart_putchar ('B');
	uart_putchar ('o');
	uart_putchar ('o');
	uart_putchar ('t');
	uart_putchar ('\r');
	uart_putchar ('\n');

	/* Initialize global variables */
	param_sck_duration = 0;
	param_reset_polarity = 0;
	param_controller_init = 0;
	address.dword = 0;
	chip_erased = 0;
	word0 = 0xFFFF;
	poly_tab [0] = 0x0000;
        poly_tab [1] = 0xCC01;
        poly_tab [2] = 0xD801;
        poly_tab [3] = 0x1400;
        poly_tab [4] = 0xF001;
        poly_tab [5] = 0x3C00;
        poly_tab [6] = 0x2800;
        poly_tab [7] = 0xE401;
        poly_tab [8] = 0xA001;
        poly_tab [9] = 0x6C00;
        poly_tab [10] = 0x7800;
        poly_tab [11] = 0xB401;
        poly_tab [12] = 0x5000;
        poly_tab [13] = 0x9C01;
        poly_tab [14] = 0x8801;
        poly_tab [15] = 0x4400;

	msgparsestate = MSG_IDLE;
	msglen = 0;
	seqnum = 0;
	i = 0;
	cksum = 0;
	while (1) {
		ch = uart_getchar ();
		/* parse message according to appl. note AVR068 table 3-1: */
		if (msgparsestate == MSG_IDLE && ch == MESSAGE_START) {
			msgparsestate = MSG_WAIT_SEQNUM;
			cksum = ch ^ 0;
			continue;
		}
		if (msgparsestate == MSG_WAIT_SEQNUM) {
			seqnum = ch;
			cksum ^= ch;
			msgparsestate = MSG_WAIT_SIZE1;
			continue;
		}
		if (msgparsestate == MSG_WAIT_SIZE1) {
			cksum ^= ch;
			msglen = ch << 8;
			msgparsestate = MSG_WAIT_SIZE2;
			continue;
		}
		if (msgparsestate == MSG_WAIT_SIZE2) {
			cksum ^= ch;
			msglen |= ch;
			msgparsestate = MSG_WAIT_TOKEN;
			continue;
		}
		if (msgparsestate == MSG_WAIT_TOKEN) {
			cksum ^= ch;
			if (ch == TOKEN) {
				msgparsestate = MSG_WAIT_MSG;
				i = 0;
			} else {
				msgparsestate = MSG_IDLE;
			}
			continue;
		}
		if (msgparsestate == MSG_WAIT_MSG && i < msglen && i < 280) {
			cksum ^= ch;
			msg_buf[i] = ch;
			i++;
			if (i == msglen) {
				msgparsestate = MSG_WAIT_CKSUM;
			}
			continue;
		}
		if (msgparsestate == MSG_WAIT_CKSUM) {
			if (ch == cksum && msglen > 0) {
				/* message correct, process it */
				msglen = program_cmd ();
			} else {
				msg_buf[0] = ANSWER_CKSUM_ERROR;
				msg_buf[1] = STATUS_CKSUM_ERROR;
				msglen = 2;
			}
			transmit_answer (seqnum, msglen);
			/* no continue here, set state=MSG_IDLE */
		}
		msgparsestate = MSG_IDLE;
		msglen = 0;
		seqnum = 0;
		i = 0;
	}
}

/*
 * Transmit an answer back to the programmer software,
 * Message is in msg_buf, seqnum is the seqnum of the last message.
 * From the programmer software, len=1..275 according to avr068.
 */
void transmit_answer (unsigned char seqnum, unsigned short len)
{
	unsigned char cksum;
	unsigned char ch;
	unsigned short i;

	if (len > 285 || len < 1) {
		/* software error */
		len = 2;
		/* msg_buf[0]: not changed */
		msg_buf[1] = STATUS_CMD_FAILED;
	}
	uart_putchar (MESSAGE_START);	/* 0x1B */
	cksum = MESSAGE_START ^ 0;
	uart_putchar (seqnum);
	cksum ^= seqnum;
	ch = (len >> 8) & 0xFF;
	uart_putchar (ch);
	cksum ^= ch;
	ch = len & 0xFF;
	uart_putchar (ch);
	cksum ^= ch;
	uart_putchar (TOKEN);	/* 0x0E */
	cksum ^= TOKEN;
	for (i = 0; i < len; i++) {
		uart_putchar (msg_buf[i]);
		cksum ^= msg_buf[i];
	}
	uart_putchar (cksum);
}

unsigned short program_cmd ()
{
	if (msg_buf[0] == CMD_SIGN_ON) {
		/* prepare answer: */
		msg_buf[0] = CMD_SIGN_ON;	/* 0x01 */
		msg_buf[1] = STATUS_CMD_OK;	/* 0x00 */
		msg_buf[2] = 8;	/* len */
		msg_buf[3] = 'A';
		msg_buf[4] = 'V';
		msg_buf[5] = 'R';
		msg_buf[6] = 'I';
		msg_buf[7] = 'S';
		msg_buf[8] = 'P';
		msg_buf[9] = '_';
		msg_buf[10] = '2';
		msg_buf[11] = 0;
		/* note: this copied also the null termination */
		return 11;

	} else if (msg_buf[0] == CMD_SET_PARAMETER) {
		/* Not implemented:
		 * PARAM_VTARGET
		 * PARAM_VADJUST
		 * PARAM_OSC_PSCALE
		 * PARAM_OSC_CMATCH */
		if (msg_buf[1] == PARAM_SCK_DURATION) {
			param_sck_duration = msg_buf[2];
		} else if (msg_buf[1] == PARAM_RESET_POLARITY) {
			param_reset_polarity = msg_buf[2];
		} else if (msg_buf[1] == PARAM_CONTROLLER_INIT) {
			param_controller_init = msg_buf[2];
		}
ok:		msg_buf[1] = STATUS_CMD_OK;
		return 2;

	} else if (msg_buf[0] == CMD_GET_PARAMETER) {
		unsigned char n;

		if (msg_buf[1] == PARAM_BUILD_NUMBER_LOW)
			n = CONFIG_PARAM_BUILD_NUMBER_LOW;
		else if (msg_buf[1] == PARAM_BUILD_NUMBER_HIGH)
			n = CONFIG_PARAM_BUILD_NUMBER_HIGH;
		else if (msg_buf[1] == PARAM_HW_VER)
			n = CONFIG_PARAM_HW_VER;
		else if (msg_buf[1] == PARAM_SW_MAJOR)
			n = CONFIG_PARAM_SW_MAJOR;
		else if (msg_buf[1] == PARAM_SW_MINOR)
			n = CONFIG_PARAM_SW_MINOR;
		else if (msg_buf[1] == PARAM_SCK_DURATION)
			n = param_sck_duration;
		else if (msg_buf[1] == PARAM_RESET_POLARITY)
			n = param_reset_polarity;
		else if (msg_buf[1] == PARAM_CONTROLLER_INIT)
			n = param_controller_init;
#if 1
		else if (msg_buf[1] == PARAM_VTARGET)
			n = CONFIG_PARAM_VTARGET;
		else if (msg_buf[1] == PARAM_VADJUST)
			n = CONFIG_PARAM_VADJUST;
		else if (msg_buf[1] == PARAM_OSC_PSCALE)
			n = CONFIG_PARAM_OSC_PSCALE;
		else if (msg_buf[1] == PARAM_OSC_CMATCH)
			n = CONFIG_PARAM_OSC_CMATCH;
#endif
		else {
			/* command not understood */
			goto failed;
		}
		msg_buf[1] = STATUS_CMD_OK;
		msg_buf[2] = n;
		return 3;

	} else if (msg_buf[0] == CMD_FIRMWARE_UPGRADE) {
		/* firmare upgrade is not supported this way */
failed:		msg_buf[1] = STATUS_CMD_FAILED;
		return 2;

	} else if (msg_buf[0] == CMD_CHIP_ERASE_ISP) {
		unsigned long addr;

		for (addr=0; addr<BADDR; addr+=PAGE_SIZE)
			page_erase (addr);
		chip_erased = 1;
		word0 = 0xFFFF;
		goto ok;

	} else if (msg_buf[0] == CMD_PROGRAM_EEPROM_ISP) {
		goto failed;

	} else if (msg_buf[0] == CMD_READ_EEPROM_ISP) {
		goto failed;

	} else if (msg_buf[0] == CMD_PROGRAM_LOCK_ISP ||
	    msg_buf[0] == CMD_PROGRAM_FUSE_ISP) {
		/* Do nothing. */
		msg_buf[1] = STATUS_CMD_OK;
		msg_buf[2] = STATUS_CMD_OK;
		return 3;

	} else if (msg_buf[0] == CMD_READ_OSCCAL_ISP ||
	    msg_buf[0] == CMD_READ_SIGNATURE_ISP ||
	    msg_buf[0] == CMD_READ_LOCK_ISP) {
		goto failed;

	} else if (msg_buf[0] == CMD_READ_FUSE_ISP) {
		if (msg_buf[2] == 0x30) {
			/* Get device signature */
			if (msg_buf[4] == 0) {
				msg_buf[2] = 0x1E;
			} else if (msg_buf[4] == 1) {
				msg_buf[2] = SIG2;
			} else if (msg_buf[4] == 2) {
				msg_buf[2] = SIG3;
			} else {
				msg_buf[2] = 0;
			}
			msg_buf[1] = STATUS_CMD_OK;
			msg_buf[3] = STATUS_CMD_OK;
			return 4;

		} else if (msg_buf[2] == 0xAC) {
			/* Write lock bits - do nothing */
			msg_buf[1] = STATUS_CMD_OK;
			msg_buf[2] = 0;
			msg_buf[3] = STATUS_CMD_OK;
			return 4;
		} else {
			goto failed;
		}

	} else if (msg_buf[0] == CMD_SPI_MULTI) {
		/* 0: CMD_SPI_MULTI
		 * 1: NumTx
		 * 2: NumRx
		 * 3: RxStartAddr counting from zero
		 * 4+: TxData (len in NumTx)
		 * example: 0x1d 0x04 0x04 0x00   0x30 0x00 0x00 0x00 */
		goto failed;

	} else if (msg_buf[0] == CMD_ENTER_PROGMODE_ISP) {
		goto ok;

	} else if (msg_buf[0] == CMD_LEAVE_PROGMODE_ISP) {
		unsigned short i;

		if (word0 != 0xFFFF) {
			/* Write word0 to address 0. */
			address.dword = 0;
			nbytes = PAGE_SIZE;
#if defined __AVR_ATmega128__
			RAMPZ = 0;
#endif
			for (i=2; i<PAGE_SIZE; ++i) {
				msg_buf [10 + i] = lpm (i);
			}
			msg_buf[10] = word0;
			msg_buf[11] = word0 >> 8;
			page_write ();
		}
		goto ok;

	} else if (msg_buf[0] == CMD_LOAD_ADDRESS) {
		address.byte[3] = msg_buf[1];
		address.byte[2] = msg_buf[2];
		address.byte[1] = msg_buf[3];
		address.byte[0] = msg_buf[4];
		address.dword <<= 1;
		goto ok;

	} else if (msg_buf[0] == CMD_READ_FLASH_ISP ||
	    msg_buf[0] == (CMD_READ_FLASH_ISP | 0x80)) {
		unsigned short i, sum;
		unsigned char byte;

		if (! chip_erased) {
			/* Reading memory is permitted only after chip erase. */
			goto failed;
		}
		/* msg_buf[1] and msg_buf[2] NumBytes msg_buf[3] cmd */
		nbytes = (unsigned short) msg_buf[1] << 8 | msg_buf[2];
		if (nbytes > 280) {
			/* limit answer len, prevent overflow: */
			nbytes = 280;
		}
#if defined __AVR_ATmega128__
		if (address.word.high != 0)
			RAMPZ = 1;
		else
			RAMPZ = 0;
#endif
		sum = 0;
		for (i=0; i<nbytes; ++i) {
			byte = read_byte ();
			msg_buf [i + 2] = byte;
			sum = crc16 (sum, byte);
			sum = crc16 (sum, byte >> 4);
			++address.dword;
		}
		if (msg_buf[0] == (CMD_READ_FLASH_ISP | 0x80)) {
			/* Nonstandard command: get memory checksum.
			 * Use CRC-16 (x16 + x5 + x2 + 1). */
			msg_buf[1] = STATUS_CMD_OK;
			msg_buf[2] = sum >> 8;
			msg_buf[3] = sum;
			return 4;
		} else {
			msg_buf[1] = STATUS_CMD_OK;
			msg_buf[nbytes + 2] = STATUS_CMD_OK;
			return nbytes + 3;
		}

	} else if (msg_buf[0] == CMD_PROGRAM_FLASH_ISP) {
		/* msg_buf[0] CMD_PROGRAM_FLASH_ISP
		 * msg_buf[1] NumBytes H
		 * msg_buf[2] NumBytes L
		 * msg_buf[3] mode
		 * msg_buf[4] delay
		 * msg_buf[5] cmd1 (Load Page, Write Program Memory)
		 * msg_buf[6] cmd2 (Write Program Memory Page)
		 * msg_buf[7] cmd3 (Read Program Memory)
		 * msg_buf[8] poll1 (value to poll)
		 * msg_buf[9] poll2
		 * msg_buf[n+10] Data */
		nbytes = (unsigned short) msg_buf[1] << 8 | msg_buf[2];
		if (nbytes > 280) {
			/* corrupted message */
			goto failed;
		}
		if (address.dword == 0) {
			/* Do not program address 0 right now,
			 * just remember it. We will store it
			 on LEAVE_PROGMODE command.  */
			word0 = *(short*) (msg_buf + 10);
			msg_buf[10] = 0xFF;
			msg_buf[11] = 0xFF;
		}
		page_write ();
		address.dword += nbytes;
		goto ok;

	}
	/* we should not come here */
	msg_buf[1] = STATUS_CMD_UNKNOWN;
	return 2;
}

/*
 * Read byte, pointed to by address.
 */
unsigned char read_byte ()
{
	/* Can handle odd and even nbytes okay */
	if (address.word.high == 0) {
		if (address.word.low == 0)
			return (unsigned char) word0;

		if (address.word.low == 1)
			return (unsigned char) (word0 >> 8);
	}
#if defined __AVR_ATmega128__
	return elpm (address.word.low);
#else
	return lpm (address.word.low);
#endif
}

unsigned short crc16 (unsigned short sum, unsigned char nibble)
{
	/* compute checksum of lower four bits of byte */
	return ((sum >> 4) & 0x0FFF) ^
		poly_tab [sum & 0xF] ^
		poly_tab [nibble & 0xF];
}

#ifndef SPMCR
#define SPMCR SPMCSR
#endif

/*
 * Erase page.
 */
void page_erase (unsigned long addr)
{
	/* Wait for previous spm to complete */
	while (SPMCR & (1 << SPMEN))
		continue;

#if defined __AVR_ATmega128__
	if ((short) (addr >> 16) != 0)
		RAMPZ = 1;
	else
		RAMPZ = 0;
#endif
	/* Erase page */
	SPMCR = (1 << PGERS) | (1 << SPMEN);
	spm (addr);
	while (SPMCR & (1 << SPMEN))
		continue;

	/* Re-enable RWW section */
	SPMCR = (1 << RWWSRE) | (1 << SPMEN);
	spm (addr);
	while (SPMCR & (1 << SPMEN))
		continue;
}

/*
 * Program page, pointed to by address.
 * Use data from msg_buf [10..nbytes+10].
 */
void page_write ()
{
	unsigned short i, addr;

	/* Wait for previous spm to complete */
	while (SPMCR & (1 << SPMEN))
		continue;

#if defined __AVR_ATmega128__
	if (address.word.high != 0)
		RAMPZ = 1;
	else
		RAMPZ = 0;
#endif
	addr = address.word.low;
	for (i=0; i<nbytes; i+=2, addr+=2) {
		/* Write word, zero register is clobbered */
		load_r0r1 (*(short*) (msg_buf + 10 + i));
		SPMCR = (1 << SPMEN);
		spm (addr);
		while (SPMCR & (1 << SPMEN))
			continue;

		/* Clear zero register */
		asm volatile ("clr __zero_reg__");
	}
	/* Write page */
	SPMCR = (1 << PGWRT) | (1 << SPMEN);
	spm (address.word.low);
	while (SPMCR & (1 << SPMEN))
		continue;

	/* Re-enable RWW section */
	SPMCR = (1 << RWWSRE) | (1 << SPMEN);
	spm (address.word.low);
	while (SPMCR & (1 << SPMEN))
		continue;
}

#ifndef UBRRL
#define UBRRL UBRR0L
#endif
#ifndef UBRRH
#define	UBRRH UBRR0H
#endif
#ifndef UCSRA
#define	UCSRA UCSR0A
#endif
#ifndef UCSRB
#define UCSRB UCSR0B
#endif
#ifndef UCSRC
#define	UCSRC UCSR0C
#endif

void uart_init (void)
{
	unsigned short divisor;

	divisor = (KHZ * 1000L / BAUDRATE + 8) / 16L - 1;
	UBRRL = (unsigned char) divisor;
	UBRRH = divisor >> 8;
	UCSRA = 0x00;

	/* format: asynchronous, 8data, no parity, 1stop bit */
	UCSRC = (3 << UCSZ0);

	/* enable tx/rx and no interrupt on tx/rx */
	UCSRB = (1 << RXEN) | (1 << TXEN);
}

/*
 * send one character to the rs232
 */
void uart_putchar (char c)
{
	/* wait for empty transmit buffer */
	while (! (UCSRA & (1 << UDRE)))
		continue;
	UDR = c;
}

/*
 * get a byte from rs232
 * this function does a blocking read
 */
unsigned char uart_getchar (void)
{
	while (! (UCSRA & (1 << RXC)))
		continue;
	return (UDR);
}
