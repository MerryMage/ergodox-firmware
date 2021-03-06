/*
 * MerryMage ErgoDox Firmware
 *
 * Copyright (c) 2015, MerryMage
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include "usb_keyboard_debug.h"
#include "print.h"
#include "mcp23018.h"
#include "time.h"
#include "led.h"
#include "translator.h"


uint8_t matrixscan[14];

/*
uint8_t rand(void) {
	static uint16_t lfsr = 0xACE1;

	unsigned lsb = lfsr & 1;
	lfsr >>= 1;
	lfsr ^= (-lsb) & 0xB400u;

	return lfsr;
}
*/

#define RIGHT_SCANLINE(COLno, COLPINx, COLPINy)    \
	do {                                           \
		DDR##COLPINx |= (1<<COLPINy);              \
		_delay_loop_1(128);                        \
		uint8_t f = ~PINF;                         \
		f = (f & 0b11) | ((f & 0b011110000) >> 2); \
		matrixscan[COLno] = f;                     \
		DDR##COLPINx &= ~(1<<COLPINy);             \
	} while(0);

extern uint8_t test[4];

int main(void);
int main(void)
{
	// CPU Prescaler
	// Set for 16 MHz clock
	CLKPR = 0x80;
	CLKPR = 0;

	// Power Reduction
	ADCSRA = 0;
	PRR0 = 0b00001101;
	PRR1 = 0b00011001;
	
	mcp23018_init();
	usb_init();
	time_init();
	led_init();

	// Configure pins for righthand:
	//          DDR=1   DDR=0
	// PORT=1   high    pull-up
	// PORT=0   low     floating
	// Input pins are configured as pull-up.
	// Output pins are configured as floating and are toggled to low via DDR to select columns.
	// Unused pins are configured as pull-up.
	// LED pins are configured as high.
	DDRB = 0b11100000; PORTB = 0b11110000; //B0-3         are columns 7-10  (output)  B5,6,7 are LEDa,b,c.
	DDRC = 0b00000000; PORTC = 0b10111111; //C6           is  column  13    (output)
	DDRD = 0b01000000; PORTD = 0b11110011; //D2,3         are columns 11,12 (output)  D6 is Teensy LED.
	DDRE = 0b00000000; PORTE = 0b11111111; //                               (unused)
	DDRF = 0b00000000; PORTF = 0b11111111; //F0,1,4,5,6,7 are R. rows 0-5   (input)
	// Note that at this point all LEDs are ON.

	// Note if this will wait forever if not connected to PC thus all LED solid on == failure to init.
	while (!usb_configured()) { idle_ms(1); }
	
	idle_ms(1000);
	print("Ready\n");
	// Turn off all LEDs
	led_on(0);

	// This is located here as a delay is required between TWI init and use.
	// Kick off left hand matrix scanning.
	mcp23018_begin();

	// Is LH connected?
	uint8_t LHconnected = 0;

#if STATISTICS
	// Statistics
	uint16_t counter = 0;
	uint8_t ms = milliseconds, diffms; uint16_t sleeptime = 0, waketime = 0;
	#define RECORD_TIME(VAR) diffms = milliseconds - ms; VAR += diffms; ms += diffms;
#else
	#define RECORD_TIME(VAR) /* nothing */
#endif

	while (1) {
		// Wait for left hand matrix scan
		if (mcp23018_poll()) {
			LHconnected = 1;
		} else {
			LHconnected = 0;
			idle_ms(1);
		}

		RECORD_TIME(sleeptime);

		// Scan right hand matrix
		RIGHT_SCANLINE(7,  B,0);
		RIGHT_SCANLINE(8,  B,1);
		RIGHT_SCANLINE(9,  B,2);
		RIGHT_SCANLINE(10, B,3);
		RIGHT_SCANLINE(11, D,2);
		RIGHT_SCANLINE(12, D,3);
		RIGHT_SCANLINE(13, C,6);

		// Debounce
		// TODO? Doesn't really seem necessary at a ~1.5ms sampling time.

		// Detect edges

		translate_tick(matrixscan);

		if (!keyboard_protocol) led_flash(led_getflash() | 0b0001); else led_flash(led_getflash() & 0b1110);
		if (!LHconnected) led_soft(led_getsoft() | 0b0001); else led_soft(led_getsoft() & 0b1110);

		led_tick();

#if STATISTICS
		// Print statistics
		counter++;
		if (waketime+sleeptime >= 10000) {
			uint32_t t = 1000*(uint32_t)(waketime+sleeptime);
			pdec16(counter/10);
			print(" scans/sec. ");
			pdec16(t/counter);
			print("us/scan. Sleep:");
			pdec8(100000*(uint32_t)sleeptime/t);
			if (LHconnected) print("%.\n"); else print("%. LH not connected.\n");
			counter = waketime = sleeptime = 0;
		}
#endif

		RECORD_TIME(waketime);
	}
}

void hang(const char* s) {
	led_soft(0);
	led_on(0);
	led_flash(0xFF);
	while (1) {
		print_P(s);
		idle_ms(100);
		led_tick();
	}
};



//-Os: 624 scans/sec. 1602us/scan. Sleep:79%.
//-O3: 667 scans/sec. 1497us/scan. Sleep:81%.
//-O3: 670 scans/sec. 1492us/scan. Sleep:80%.