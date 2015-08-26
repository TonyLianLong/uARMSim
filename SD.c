#include "SD.h"
#include <avr/io.h>
#include <inttypes.h>
#include <stdio.h>
#include "types.h"
#define SD_CONFIG_SLOW() do{SPCR=(1<<SPE)|(1<<MSTR)|(1<<SPR0)|(1<SPR1);\
SPSR=(1<<SPI2X);\
}while(0);
//Enable SPI,Master,SPR0,SPR1
//Speedx2 with SPR0,SPR1 Fsck=Fosc/64
#define SD_CONFIG_FAST() do{SPCR=(1<<SPE)|(1<<MSTR);\
SPSR=(1<<SPI2X);\
}while(0);
//Enable SPI,Master
//Speedx2 without SPR0,SPR1 Fsck=Fosc/2
uint8_t CMD0[6]={0x40,0x00,0x00,0x00,0x00,0x95};
uint8_t CMD1[6]={0x41,0x00,0x00,0x00,0x00,0xFF};
uint8_t CMD9[6] = {0x40|9,0x00,0x00,0x00,0x00,0xFF};
uint8_t CMD24[6]={0x40|24,0x00,0x00,0x00,0x00,0xFF};
uint8_t CMD17[6] = {0x40|17,0x00,0x00,0x00,0x00,0xFF};
void spi_init(void)
{  
    DDRB|=1<<4;//CS片选
DDRB|=1<<5;//MOSI
DDRB&=~(1<<6);//MISO
DDRB|=1<<7;//SCK
SD_CONFIG_SLOW();
//Be slower when init
PORTB|=(1<<4)|(1<<5)|(1<<7);
}
uint8_t send(uint8_t dat)//发送数据
{   
    uint8_t dat1=0;
    SPDR=dat;
while(!(SPSR&(1<<7)));//等待数据发送完成
dat1=SPDR;
return dat1;
}
uint8_t send_cmd(uint8_t *CMD)//发送命令
{
   uint8_t i = 0,dat;
   PORTB|=(1<<4);//CS=0;
   send(0xff);//一定要有这一步，否则可能初始化不能成功
   PORTB&=~(1<<4);
   send(CMD[0]);
   send(CMD[1]);
   send(CMD[2]);
   send(CMD[3]);
   send(CMD[4]);
   send(CMD[5]);
   do
   {
    dat=send(0xff);//读回命令的响应
i++;
   }while((i<200)&&(dat==0xff));
   return dat;
}
UInt32 sdPrvGetBits(UInt8* data,UInt32 numBytesInArray,UInt32 startBit,UInt32 len){//for CID and CSD data..

	UInt32 bitWrite = 0;
	UInt32 numBitsInArray = numBytesInArray * 8;
	UInt32 ret = 0;

	do{

		UInt32 bit,byte;

		bit = numBitsInArray - startBit - 1;
		byte = bit / 8;
		bit = 7 - (bit % 8);

		ret |= ((data[byte] >> bit) & 1) << (bitWrite++);

		startBit++;
	}while(--len);

	return ret;
}
UInt32 sdPrvGetCardNumBlocks(UInt8* csd){

	UInt32 ver = sdPrvGetBits(csd,16,126,2);
	UInt32 mmcver = sdPrvGetBits(csd,16,122,4);
	//mmcver == 0 SD (In SD,it's reserved)
	//mmcver > 0 MMC
	UInt32 cardSz = 0;
	//printf("Ver:%lu MMCVer:%lu\n",ver,mmcver);

	if(ver == 0||((mmcver > 0)&&ver<=3)){
		UInt32 cSize = sdPrvGetBits(csd,16,62,12);
		UInt32 cSizeMult = sdPrvGetBits(csd,16,47,3);
		UInt32 readBlLen = sdPrvGetBits(csd,16,80,4);
		UInt32 blockLen,blockNr;
		UInt32 divTimes = 9;		//from bytes to blocks division

		blockLen = 1UL << readBlLen;
		blockNr = (cSize + 1) * (1UL << (cSizeMult + 2));

		/*
			 multiplying those two produces result in bytes, we need it in blocks
			 so we shift right 9 times. doing it after multiplication might fuck up
			 the 4GB card, so we do it before, but to avoid killing significant bits
			 we only cut the zero-valued bits, if at the end we end up with non-zero
			 "divTimes", divide after multiplication, and thus underuse the card a bit.
			 This will never happen in reality since 512 is 2^9, and we are
			 multiplying two numbers whose product is a multiple of 2^9, so they
			 togethr should have at least 9 lower zero bits.
		*/

		while(divTimes && !(blockLen & 1)){

			blockLen = blockLen >> 1;
			divTimes--;
		}
		while(divTimes && !(blockNr & 1)){

			blockNr = blockNr >> 1;
			divTimes--;
		}

		cardSz = (blockNr * blockLen) >> divTimes;
	}
	else if(ver == 1){
		cardSz = sdPrvGetBits(csd,16,48,22)/*num 512K blocks*/ << 10;
	}else{
		printf("Card version do not support:\nCSD Version:0x%lx MMC Version:0x%lx",ver,mmcver);
	}


	return cardSz;
}
int64_t MMC_init(){
  uint8_t n;//MMC卡的初始化可能要多次发送命令才会初始化成功
  uint16_t m;
  uint8_t csd[16];
  PORTB&=~(1<<4);
  for(m=0;m<0x0f;m++)
  {
     send(0xff);
  }//上电发送大于74个时钟 16*8
  do
  {
	  
     n=send_cmd(CMD0);//发送CMD0复位，响应R1=0x01
  m++;
  }while((m<200)&&(n!=0x01));
  if(m == 200){
	PORTB|=(1<<4);
	return -1;
  }
  m=0;
  do
  {
    n=send_cmd(CMD1);//发送CMD1激活卡,响应R1=0x00
    m++;
  }while((m<2000)&&(n!=0x00));
  PORTB|=(1<<4);
  if(m == 2000){
	  return -2;
  }
  PORTB&=~(1<<4);
  n = send_cmd(CMD9);
  if(n != 0x00){
		PORTB|=(1<<4);
		return -3;
  }
  for(n = 0;n<16;n++){
	csd[n] = send(0xFF);
  }
  PORTB|=(1<<4);
  SD_CONFIG_FAST();
  return sdPrvGetCardNumBlocks(csd);//初始化成功
}
/*
uint8_t write_byte(void)//
{  
    uint16_t i;
uint8_t dat;
    CMD24[0]=0x58;
CMD24[1]=0x00;
CMD24[2]=0x00;
CMD24[3]=0x00;
CMD24[4]=0x00;
CMD24[5]=0xff;
send_cmd(CMD24);//命令只要发送一次就可了当初始化成功的时候
send(0xff);//填充时钟
send(0xfe);//发送起始令牌
for(i=0;i<512;i++)
{
     send(buf[i]);
}
send(0xff);
send(0xff);//两个字节的校检字节
dat=send(0xff);
return dat;
}
void read_byte(void)//读数据块命令成功
{
    uint16_t i;
    CMD17[0]=0x40+17;
CMD17[1]=0x00;
CMD17[2]=0x00;
CMD17[3]=0x00;
CMD17[4]=0x00;
CMD17[5]=0xff;
send_cmd(CMD17);//发送读数据命令的时候，只要发一次，不能多发否则会出现各种问题
while(send(0xff)!=0xfe);//等待第一个数据令牌
for(i=0;i<512;i++)
{
     buf[i]=send(0xff);
}
send(0xff);
send(0xff);//两个字节的校检字节

}
*/
Boolean sdInit(SD* sd){
	int64_t mmc_init_return_value;
spi_init();
mmc_init_return_value = MMC_init();
if(mmc_init_return_value < 0){
	sd->numSec = 0;
	return false;
}
sd->numSec = mmc_init_return_value;
//sd->numSec = 512UL*1024*1024/512;//512Mb
sd->inited = 1;
printf("SD size:%lu\n",(sd->numSec)<<9);
return true;

#ifdef PRINT_DEBUG_TEXT
printf("Init Successfully\n");
#endif
return true;
} 
uint32_t sdGetNumSec(SD* sd){
	return sd->numSec;//512Mb
}
Boolean sdSecRead(SD* sd, UInt32 sec, void* buf){
	if(sec >= (sd->numSec)){
		printf("SD Read:Out of range Sector:%lu Sector number on SD card:%lu\n",sec,sd->numSec);
		return false;
		//Read sector is out of range
	}
	uint16_t i;
	CMD17[1]=((uint64_t)sec<<9UL)>>24UL;
	CMD17[2]=((uint64_t)sec<<9UL)>>16UL;
	CMD17[3]=((uint64_t)sec<<9UL)>>8UL;
	CMD17[4]=(uint64_t)sec<<9UL;//Sector to Address
	send_cmd(CMD17);//发送读数据命令的时候，只要发一次，不能多发否则会出现各种问题
	while(send(0xff)!=0xfe);//等待第一个数据令牌
	
	for(i=0;i<512;i++)
	{
		*(uint8_t *)buf++=send(0xff);
	}

	send(0xFF);
	send(0xFF);//两个字节的校检字节
	return true;
}
Boolean sdSecWrite(SD* sd, UInt32 sec, void* buf){
	if(sec >= (sd->numSec)){
		printf("SD Write:Out of range Sector:%lu Sector number on SD card:%lu\n",sec,sd->numSec);
		return false;
		//Read sector is out of range
	}
	    uint16_t i;
uint8_t dat;
CMD24[1]=((uint64_t)sec<<9UL)>>24UL;
	CMD24[2]=((uint64_t)sec<<9UL)>>16UL;
	CMD24[3]=((uint64_t)sec<<9UL)>>8UL;
	CMD24[4]=(uint64_t)sec<<9UL;//Sector to Address
send_cmd(CMD24);//命令只要发送一次就可了当初始化成功的时候
send(0xFF);//填充时钟
send(0xFE);//发送起始令牌
for(i=0;i<512;i++)
{
     send(*(uint8_t *)buf++);
}
send(0xff);
send(0xff);//两个字节的校检字节
dat=send(0xff);
if(dat == 0x05){
return true;
}else{
	return false;
}
}