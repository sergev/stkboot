/*
 * Serial Bootloader for Cronyx devices based on Atmel AVR Controllers.
 * Copyright (c) 2006, Serge Vakulenko
 *
 * Based on stk500boot.c
 * Copyright (c) 2003, Jason P. Kyle
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
#include <runtime/avr/io.h>

/* set the UART baud rate */
/*#define BAUD_RATE	115200*/
#define BAUD_RATE	38400

/*
 * SW_MAJOR and MINOR needs to be updated from time to time
 * to avoid warning message from AVR Studio.
 * Never allow AVR Studio to do an update !!!!
 */
#define HW_VER		0x40
#define SW_VER		0x72

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
#define SIG2		0x94
#define SIG3		0x02
#define PAGE_SIZE	0x40U	/* 64 words */

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

/* function prototypes */
void uart_init (void);
void uart_putchar (char);
unsigned char uart_getchar (void);

/* some variables */
unsigned char buf [256];
unsigned short address;
unsigned short nbytes;

/*
* Read a byte from the program space (flash).
*/
#define read_low64k(addr) ({				\
	register unsigned char t;			\
	asm volatile (					\
		"lpm" "\n"				\
		"mov    %0,r0"				\
		: "=r" (t)				\
		: "z" ((int)addr));			\
	t; })

#if defined __AVR_ATmega128__
#define read_high64k(addr) ({				\
	register unsigned char t;			\
	RAMPZ = 1;					\
	asm volatile (					\
		"elpm" "\n"				\
		"mov    %0,r0"				\
		: "=r" (t)				\
		: "z" ((int)addr));			\
	RAMPZ = 0;					\
	t; })
#endif

/*
 * main program starts here
 */
int main (void)
{
	unsigned char ch, ch2, highmem;
	unsigned short w;

	/* Disable interrupts */
	cli ();

	/* Clear zero register */
	asm volatile ("eor __zero_reg__, __zero_reg__");

	uart_init ();
	uart_putchar ('B');
	uart_putchar ('o');
	uart_putchar ('o');
	uart_putchar ('t');
	uart_putchar ('\r');
	uart_putchar ('\n');
	uart_putchar ('\0');

	/* forever loop */
	for (;;) {
		/* get character from UART */
		ch = uart_getchar ();

		/* Hello is anyone home ? */
		if (ch == ' ') {
			uart_putchar ('A');
		}
		/* Request programmer ID.
		 * Not using PROGMEM string due to boot block in m128 being
		 * beyond 64kB boundry.
		 * Would need to selectively manipulate RAMPZ, and it's only
		 * 9 characters anyway so who cares. */
		else if (ch == 'S') {
			if (uart_getchar () != ' ' || uart_getchar () != ' ') {
failed:				uart_putchar ('F');
				continue;
			}
			uart_putchar ('A');
			uart_putchar ('A');
			uart_putchar ('V');
			uart_putchar ('R');
			uart_putchar ('N');
			uart_putchar ('O');
			uart_putchar ('C');
			uart_putchar ('D');
			uart_putchar ('A');
		}
		/* AVR ISP/STK500 board requests */
		else if (ch == 'q') {
			ch2 = uart_getchar ();
			if (uart_getchar () != ' ' || uart_getchar () != ' ')
				goto failed;
			uart_putchar ('A');

			if (ch2 == 0x7a) {
				/* Hardware version */
				uart_putchar (HW_VER);
			}
			else if (ch2 == 0x7b) {
				/* Software version */
				uart_putchar (SW_VER);
			}
			else if (ch2 == 0x62) {
				/* Baud rate */
				uart_putchar (- 115200 / BAUD_RATE);
			}
			else if (ch2 == 0x88) {
				/* Flash page size low */
				uart_putchar (PAGE_SIZE);
			}
			else if (ch2 == 0x89) {
				/* Flash page size high */
				uart_putchar (PAGE_SIZE >> 8);
			}
			else if (ch2 == 0xa7) {
				/* Get device signature byte 0 */
				uart_putchar (0x1E);
			}
			else if (ch2 == 0xa8) {
				/* Get device signature byte 1 */
				uart_putchar (SIG2);
			}
			else if (ch2 == 0xa9) {
				/* Get device signature byte 2 */
				uart_putchar (SIG3);
			}
			else {
				/* Covers various unnecessary responses
				 * we don't care about */
				uart_putchar (0);
			}
			uart_putchar ('A');
		}
		/* Write memory */
		else if (ch == 'W') {
			if (uart_getchar () != 0xB0) {
				/* Only flash memory is implemented */
				uart_putchar ('F');
				continue;
			}
			nbytes = uart_getchar () + 1;

			uart_getchar ();		/* Ignore high address byte. */
			address = uart_getchar () << 8;
			address |= uart_getchar ();
			if (uart_getchar () != ' ' || uart_getchar () != ' ')
				goto failed;
			uart_putchar ('A');

			if (uart_getchar () != 'h')
				goto failed;
			for (w = 0; w < nbytes; w++) {
				/* Store data in buffer. */
				buf[w] = uart_getchar ();
			}
			if (uart_getchar () != ' ' || uart_getchar () != ' ')
				goto failed;
			uart_putchar ('A');

			/* Write to FLASH one page at a time */
#ifdef __AVR_ATmega128__
			if (address > 0x7FFF)
				RAMPZ = 1;
			else
				RAMPZ = 0;
#endif
			/* address * 2 -> byte location */
			address = address << 1;

			asm volatile (
				"clr	r17		\n\t"	/* page_word_count */
				"lds	r30,address	\n\t"	/* Address of FLASH
								 * location (in bytes) */
				"lds	r31,address+1	\n\t"
				"ldi	r28,lo8(buf)	\n\t"	/* Start of buffer array
								 * in RAM */
				"ldi	r29,hi8(buf)	\n\t"
				"lds	r24,nbytes	\n\t"	/* Length of data to be
								 * written (in bytes) */
				"lds	r25,nbytes+1	\n"
			"nbytes_loop:			\n\t"	/* Main loop, repeat for
								 * number of words in block */
				"cpi	r17,0x00	\n\t"	/* If page_word_count=0
								 * then erase page */
				"brne	no_page_erase	\n"
			"wait_spm1:			\n\t"
				"lds	r16,%0		\n\t"	/* Wait for previous spm
								 * to complete */
				"andi	r16,1           \n\t"
				"cpi	r16,1           \n\t"
				"breq	wait_spm1       \n\t"
				"ldi	r16,0x03	\n\t"	/* Erase page pointed to
								 * by Z */
				"sts	%0,r16		\n\t"
				"spm			\n\t"
#ifdef __AVR_ATmega163__
				".word 0xFFFF		\n\t"
				"nop			\n"
#endif
			"wait_spm2:			\n\t"
				"lds	r16,%0		\n\t"	/* Wait for previous spm
								 * to complete */
				"andi	r16,1           \n\t"
				"cpi	r16,1           \n\t"
				"breq	wait_spm2       \n\t"

				"ldi	r16,0x11	\n\t"	/* Re-enable RWW section */
				"sts	%0,r16		\n\t"
				"spm			\n\t"
#ifdef __AVR_ATmega163__
				".word 0xFFFF		\n\t"
				"nop			\n"
#endif
			"no_page_erase:			\n\t"
				"ld	r0,Y+		\n\t"	/* Write 2 bytes into
								 * page buffer */
				"ld	r1,Y+		\n"

			"wait_spm3:			\n\t"
				"lds	r16,%0		\n\t"	/* Wait for previous spm
								 * to complete */
				"andi	r16,1           \n\t"
				"cpi	r16,1           \n\t"
				"breq	wait_spm3       \n\t"
				"ldi	r16,0x01	\n\t"	/* Load r0,r1 into FLASH
								 * page buffer */
				"sts	%0,r16		\n\t"
				"spm			\n\t"

				"inc	r17		\n\t"	/* page_word_count++ */
				"cpi r17,%1	        \n\t"
				"brlo	same_page	\n"	/* Still same page in
								 * FLASH */
			"write_page:			\n\t"
				"clr	r17		\n"	/* New page, write
								 * current one first */
			"wait_spm4:			\n\t"
				"lds	r16,%0		\n\t"	/* Wait for previous spm
								 * to complete */
				"andi	r16,1           \n\t"
				"cpi	r16,1           \n\t"
				"breq	wait_spm4       \n\t"
#ifdef __AVR_ATmega163__
				"andi	r30,0x80	\n\t"	/* m163 requires Z6:Z1
								 * to be zero during
								 * page write */
#endif
				"ldi	r16,0x05	\n\t"	/* Write page pointed to
								 * by Z */
				"sts	%0,r16		\n\t"
				"spm			\n\t"
#ifdef __AVR_ATmega163__
				".word 0xFFFF		\n\t"
				"nop			\n\t"
				"ori	r30,0x7E	\n"	/* recover Z6:Z1 state
								 * after page write (had
								 * to be zero during write) */
#endif
			"wait_spm5:			\n\t"
				"lds	r16,%0		\n\t"	/* Wait for previous spm
								 * to complete */
				"andi	r16,1           \n\t"
				"cpi	r16,1           \n\t"
				"breq	wait_spm5       \n\t"
				"ldi	r16,0x11	\n\t"	/* Re-enable RWW section */
				"sts	%0,r16		\n\t"
				"spm			\n\t"
#ifdef __AVR_ATmega163__
				".word 0xFFFF		\n\t"
				"nop			\n"
#endif
			"same_page:			\n\t"
				"adiw	r30,2		\n\t"	/* Next word in FLASH */
				"sbiw	r24,2		\n\t"	/* nbytes-2 */
				"breq	final_write	\n\t"	/* Finished */
				"rjmp	nbytes_loop	\n"
			"final_write:			\n\t"
				"cpi	r17,0		\n\t"
				"breq	block_done	\n\t"
				"adiw	r24,2		\n\t"	/* nbytes+2, fool above
								 * check on length after
								 * short page write */
				"rjmp	write_page	\n"
			"block_done:			\n\t"
				"clr	__zero_reg__	\n\t"	/* restore zero register */
#if defined __AVR_ATmega168__
				: "=m" (SPMCSR)
#else
				: "=m" (SPMCR)
#endif
				: "M" (PAGE_SIZE)
				: "r0", "r16", "r17", "r24", "r25",
				"r28", "r29", "r30", "r31"
			);
		}
		/* Read memory block mode, nbytes is big endian.  */
		else if (ch == 'R') {
			if (uart_getchar () != 0xB0) {
				/* Only flash memory is implemented */
				uart_putchar ('F');
				continue;
			}
			nbytes = uart_getchar () + 1;

			uart_getchar ();		/* Ignore high address byte. */
			address = uart_getchar () << 8;
			address |= uart_getchar ();
			if (uart_getchar () != ' ' || uart_getchar () != ' ')
				goto failed;
			uart_putchar ('A');

#if defined __AVR_ATmega128__
			if (address > 0x7FFF)
				highmem = 1;
			else
				highmem = 0;
#endif
			/* address * 2 -> byte location */
			address = address << 1;

			for (w = 0; w < nbytes; w++) {
				/* Can handle odd and even nbytess okay */
				if (! highmem)
					uart_putchar (read_low64k (address));
#if defined __AVR_ATmega128__
				else
					uart_putchar (read_high64k (address));
#endif
				++address;
			}
			uart_putchar (0);		/* Dummy checksum */
			uart_putchar ('A');
		}
	}
	/* end of forever loop */
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
#ifndef TXEN
#define	TXEN TXEN0
#endif
#ifndef RXEN
#define RXEN RXEN0
#endif
#ifndef UCSRA
#define UCSRA UCSR0A
#endif
#ifndef UDRE
#define UDRE UDRE0
#endif
#ifndef UDR
#define UDR UDR0
#endif
#ifndef RXC
#define RXC RXC0
#endif

/* Initialize UART(s) depending on KHZ defined */
void uart_init (void)
{
	unsigned short divisor;

	divisor = (KHZ * 1000L / BAUD_RATE  + 8) / 16L - 1;
	UBRRL = (unsigned char) divisor;
	UBRRH = divisor >> 8;
	UCSRA = 0x00;
	UCSRC = 0x06;
	UCSRB = _BV (TXEN) | _BV (RXEN);
}

void uart_putchar (char ch)
{
	while (! (UCSRA & _BV (UDRE)))
		continue;
	UDR = ch;
}

unsigned char uart_getchar (void)
{
	while (! (UCSRA & _BV (RXC)))
		continue;
	return UDR;
}
/* end of file megaboot.c */
