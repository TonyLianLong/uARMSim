#define F_CPU	24000000UL
#define BAUD	115200UL
#include <avr/wdt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/setbaud.h>
#include <util/delay.h>
#include <avr/boot.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#undef SPSR	//avr code defines it
#include "mem.h"
#include "RAM.h"
#include "SoC.h"
#include "SD.h"
#include "callout_RAM.h"
#define SD_CHECK 0
//Do SD Check

#define FAST_BOOT2
//Use FAST BOOT2

//unsigned char ramRead(UInt32 addr, UInt8* buf, UInt8 sz);
//extern void ramWrite(UInt32 addr, UInt8* buf, UInt8 sz);
//extern void __vector_13();	//we call it directly :)
//SRAMs don't need to be refreshed

volatile UInt32 gRtc;


static int readchar(void){

	#ifdef SIM
		return CHAR_NONE;
	#else
		if(UCSR0A & (1<<RXC0)){

			return UDR0;
		}
		else return CHAR_NONE;
	#endif
}

void writechar(int chr){
	#ifdef SIM


		*(unsigned char*)0x20 = chr;

	#else
		while(!(UCSR0A & (1<<UDRE0)));	//busy loop

		UDR0 = chr;
	#endif
}

//debug things
	static int uart_putchar(char c, _UNUSED_ FILE *stream){

		if(c == '\n') writechar('\r');
		writechar(c);

		return 0;
	}

	static int uart_getchar(_UNUSED_ FILE *stream){
		return _FDEV_EOF;
		//return readchar();
	}

int rootOps(void* userData, UInt32 sector, void* buf, UInt8 op){

	SD* sd = userData;

	switch(op){
		case BLK_OP_SIZE:

			if(sector == 0){	//num blocks

				if(sd->inited){

					 *(unsigned long*)buf = sdGetNumSec(sd);
				}
				else{

					*(unsigned long*)buf = 0;
				}
			}
			else if(sector == 1){	//block size

				*(unsigned long*)buf = SD_BLOCK_SIZE;
			}
			else return 0;
			return 1;

		case BLK_OP_READ:

			return sdSecRead(sd, sector, buf);


		case BLK_OP_WRITE:

			return sdSecWrite(sd, sector, buf);
	}
	return 0;
}

void init(){

	cli();

	//wdt
	{
		asm("cli");
		wdt_reset();
		wdt_disable();
	}

	//JTAG off
	{
		unsigned char c = MCUCR | 0x80;

		MCUCR = c;
		MCUCR = c;
	}

	//uart
	#ifndef SIM
	{
		//UART config
		UBRR0H = UBRRH_VALUE;
		UBRR0L = UBRRL_VALUE;
		UCSR0A = USE_2X ? (1 << U2X0) : 0;
		UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
		UCSR0B = (1 << RXEN0) | (1 << TXEN0);
	}
	#endif

	//timer (for ram)
	/*#ifndef SIM
	{
		OCR1A = 0x5AD2;		//match every 62ms
		TCCR1A = 0x00;
		TCCR1B = 0x0B;		//reset on match with OCR1A
		TIMSK1 = 2;		//interrupt when we match
	}
	#endif*/

	//UART IO
	{
		fdevopen(uart_putchar,uart_getchar);
	}

	//RAM PORT
	#ifndef SIM
	{
		DDRA = 0xFF;//Address
		DDRB |= 0b00001111;//Address
		DDRC = 0xFF;//Data
		DDRD |= 0b10011100;//CE Latch1 Latch0 WE
		PORTD |= (1<<3)|(1<<4);//LATCH1 LATCH0 High
	}
	#endif

	//RTC timer
	{

		gRtc = 0x4F667714UL;	//approx :)

		OCR3A = 46875UL;//46875UL;
		TCCR3A = 0x00;//1<<WGM21;
		TCCR3B = 0x0C;		//Fosc / 256
		//TCCR3B = (1<<CS22)|(1<<CS21)|(1<<CS20);	//Fsoc / 1024
		TIMSK3 = 2;		//interrupt when we match
	}

	//SD interface setup
	/*{

		DDRD |= 0x0C;	//LED_r, LED_w outputs
		DDRD &=~ 0x40;	//MISO (D6) in
		DDRB |= 0xC0;	//MOSI (B6), SCLK (B7) out

		PORTD &=~ 0x0C;	//LED_r, LED_w low
		PORTB &=~ 0xC0;	//MOSI & SCLK lo
	}*/

	//button
	{
		DDRD &=~(1<<5);	//BTN (D5) is input
		PORTD |= 1<<5;	//BTN (D5) has pullup
	}

	//RAM init & enable refresh
	/*#ifndef SIM
	{
		UInt8 t;


		_delay_us(200);	//as per init instructions
		for(t = 0; t < 8; t++) __vector_13();

		sei();	//enable refresh
	}
	#endif*/
	sei();
}

ISR(TIMER3_COMPA_vect){

	static UInt8 tik = 0;

	if(tik) gRtc++;
	tik ^= 1;
}

void ramRead(UInt32 addr, UInt8* buf, UInt8 sz){
	asm("cli");//Disable interrupts
	while(sz--){
		DDRC = 0x0;
		//Input
		PORTD &= ~(1<<7);
		//CE Low
		PORTD |= 1<<2;
		//WE High
		PORTD &= ~(1<<3);
		//LATCH0 Low
		PORTA = addr;
		//address bit 0-8
		PORTB &= ~0xF;
		PORTB |= (addr >> 8) & 0xF;
		//address bit 8-12
		PORTD |= 1<<3;
		//LATCH0 High
		PORTD &= ~(1<<4);
		//LATCH1 Low
		PORTA = addr>>12;
		//address bit 12-20
		PORTB &= ~0xF;
		PORTB |= (addr >> 20) & 0xF;
		//address bit 20-24
		PORTD |= 1<<4;
		//LATCH1 High
		/**((volatile unsigned char*)0xD0) = addr >> 16;
		*((volatile unsigned char*)0xD1) = addr >> 8;
		*((volatile unsigned char*)0xD2) = addr;
		*buf++ = *((volatile unsigned char*)0xD3);*/
		
		*buf++ = PINC;
		//Data
		PORTD |= 1<<7;
		//CE High
		addr++;
	}
	asm("sei");//Enable interrupts
}

void ramWrite(UInt32 addr, const UInt8* buf, UInt8 sz){
	asm("cli");//Disable interrupts
	while(sz--){
		DDRC = 0xFF;
		//Output
		PORTC = *buf++;
		//Data
		PORTD &= ~(1<<3);
		//LATCH0 Low
		PORTA = addr;//address bit 0-8
		PORTB &= ~0xF;//Clear bit 0-4 in PORTB
		PORTB |= ((addr >> 8) & 0xF);//address bit 8-12
		PORTD |= (1<<3);
		//LATCH0 High
		PORTD &= ~(1<<4);
		//LATCH1 Low
		PORTA = addr>>12;
		//address bit 12-20
		PORTB &= ~0xF;
		PORTB |= (addr >> 20) & 0xF;
		//address bit 20-24
		PORTD |= 1<<4;
		//LATCH1 High
		PORTD &= (~((1<<2)|(1<<7)));
		//WE CE Low
		PORTD |= (1<<2)|(1<<7);
		//CE High
		/**((volatile unsigned char*)0xD0) = addr >> 16;
		*((volatile unsigned char*)0xD1) = addr >> 8;
		*((volatile unsigned char*)0xD2) = addr;

		*((volatile unsigned char*)0xD3) = *buf++;*/
		addr++;
	}
	asm("sei");//Enable interrupts
}
#ifdef SIM
	UInt8 simRamRead(UInt32 addr, UInt8* buf, UInt8 sz){

		while(sz--){

			*((volatile unsigned char*)0xD0) = addr >> 16;
			*((volatile unsigned char*)0xD1) = addr >> 8;
			*((volatile unsigned char*)0xD2) = addr;

			*buf++ = *((volatile unsigned char*)0xD3);
		}
	}

	void simRamWrite(UInt32 addr, const UInt8* buf, UInt8 sz){

		while(sz--){

			*((volatile unsigned char*)0xD0) = addr >> 16;
			*((volatile unsigned char*)0xD1) = addr >> 8;
			*((volatile unsigned char*)0xD2) = addr;

			*((volatile unsigned char*)0xD3) = *buf++;
		}
	}
	#define ramRead simRamRead
	#define ramWrite simRamWrite
#endif

Boolean coRamAccess(_UNUSED_ CalloutRam* ram, UInt32 addr, UInt8 size, Boolean write, void* bufP){

	UInt8* b = bufP;

	if(write) ramWrite(addr, b, size);
	else ramRead(addr, b, size);

	return true;
}

static SoC soc;
int main(){

	SD sd;
	init();
	sei();
	if(!sdInit(&sd)) err_str("sd init failed");
	printf("SD Init Successfully\n");
	#if SD_CHECK == 1
		#define E(x)	do{printf(x); while(1);}while(0)

		UInt32 p, numSec;
		Boolean ret;
		UInt16 i;
		UInt8 buf[512],data;
		printf("Performing some basic tests\n");
		numSec = sdGetNumSec(&sd);
		printf(" - card is %ld sectors (%ld MB)\n", numSec, numSec >> 11UL);
		
		ret = sdSecRead(&sd, 0, buf);
		if(!ret) E("card read fails\n");

		/*for(i = 0; i < 512; i++){

			if(i & 0x0F) printf(" ");
			else printf("\n%04X ", i);
		}
		printf("\n");

		//if(numSec > 32UL * 1024UL) numSec = 512;
		//Check full card
		
		for(p = 0; p < numSec; p++){

			ret = sdSecRead(&sd, p, buf);
			if(!ret) E("card read fails\n");

			printf("\r reading %ld/%ld", p, numSec);
			for(i = 0; i < 512; i += 16) ramWrite((p << 9) + i, buf + i, 16);
		}
		printf("\n");*/
		for(p = 1; p < numSec; p+=4096){
			printf("Sector:%lu\n",p);
			ret = sdSecRead(&sd, p, buf);
			if(!ret) E("card read fails\n");
			for(i = 0;i < 512;i++){
				*(buf+i) = 0x11;
			}
			((uint32_t *)buf)[0] = p;//~*(buf+i);
			ret = sdSecWrite(&sd, p, buf);
			if(!ret) E("card write fails\n");
			/*for(i = 0;i < 512;i+=16){
				ramRead(i,&data,1);
				if(data != buf[i]){
					printf("mismatch on %llu,data in memory is 0x%x,data in SD card is 0x%x\n",(uint64_t)p*512+i,data,buf[i]);
					while(1);
				}
			}*/
		}

		printf("Finished!\n");
		while(1);
	#endif


	socInit(&soc, socRamModeCallout, coRamAccess, readchar, writechar, rootOps, &sd);

	if(!(PIND & (1<<5))){	//hack for faster boot in case we know all variables & button is pressed
		#ifndef FAST_BOOT2
			printf("Faster boot\n");
			UInt32 i, s = 786464UL;
			UInt32 d = 0xA0E00000;
			UInt16 j;
			UInt8* b = (UInt8*)soc.blkDevBuf;

			for(i = 0; i < 4096; i++){
				sdSecRead(&sd, s++, b);
				for(j = 0; j < 512; j += 32, d+= 32){

					ramWrite(d, b + j, 32);
				}
			}
			soc.cpu.regs[15] = 0xA0E00000UL+512UL;
			printf("Faster boot start\n");
		#else
			uint64_t i;
			/*struct ArmCpu cpu;
			cpu.extra_regs = soc.cpu.extra_regs;//backup pointer
			cpu.regs = soc.cpu.regs;
			cpu.coproc = soc.cpu.coproc;
			cpu.userdata = soc.cpu.userdata;*/
			printf("Faster boot 2\n");
			UInt8* buf = (UInt8*)soc.blkDevBuf;
			UInt32 s = (uint64_t)(0x18204000)/512;//1 sector 512 bytes
			sdSecRead(&sd, s++, buf);
			if(*buf == FAST_BOOT2_MAGIC_NUMBER){
				//Magic Number
				for(i = 0; i < RAM_SIZE/512; i++){
					sdSecRead(&sd, s++, buf);
					/*for(j = 0; j < 512; j += 32){
						ramWrite(i<<9 + j,buf + j, 32);
					}*/
					ramWrite(i<<9,buf,512);
				}
				sdSecRead(&sd, s, buf);//registers
				for(i = 0;i<16;i++){
					soc.cpu.regs[i] = buf[i];
				}
				/*soc.cpu = *(struct ArmCpu *)(buf+16);//Get CPU's information
				soc.cpu.extra_regs = cpu.extra_regs;
				soc.cpu.regs = cpu.regs;
				soc.cpu.coproc = cpu.coproc;
				soc.cpu.userdata = cpu.userdata; */
				printf("Faster boot start\n");
			}else{
				printf("Magic number not match,try to boot in a normal way.\n");
			}
		#endif
	}
	#ifdef GDB_SUPPORT
		socRun(&soc,0);
	#else
		socRun(&soc);
	#endif
	while(1);

	return 0;
}

void err_str(const char* str){

	char c;

	while((c = *str++) != 0) writechar(c);
}

UInt32 rtcCurTime(void){

	UInt32 t;

	do{

		t = gRtc;

	}while(t != gRtc);

	return t;
}

void* emu_alloc(_UNUSED_ UInt32 size){

	err_str("No allocations in avr mode please!");

	return 0;
}