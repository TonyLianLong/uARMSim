#include "SoC.h"


#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#include <termios.h>

unsigned char* readFile(const char* name, UInt32* lenP){

	long len = 0;
	unsigned char *r = NULL;
	int i;
	FILE* f;

	f = fopen(name, "r");
	if(!f){
		perror("cannot open file");
		return NULL;
	}

	i = fseek(f, 0, SEEK_END);
	if(i){
		return NULL;
		perror("cannot seek to end");
	}

	len = ftell(f);
	if(len < 0){
		perror("cannot get position");
		return NULL;
	}

	i = fseek(f, 0, SEEK_SET);
	if(i){
		return NULL;
		perror("cannot seek to start");
	}


	r = malloc(len);
	if(!r){
		perror("cannot alloc memory");
		return NULL;
	}

	if(len != (long)fread(r, 1, len, f)){
		perror("canot read file");
		free(r);
		return NULL;
	}

	*lenP = len;
	return r;
}



static int ctlCSeen = 0;

static int readchar(void){

	struct timeval tv;
	fd_set set;
	char c;
	int i, ret = CHAR_NONE;

	if(ctlCSeen){
		ctlCSeen = 0;
		return 0x03;
	}

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&set);
	FD_SET(0, &set);

	i = select(1, &set, NULL, NULL, &tv);
	if(i == 1 && 1 == read(0, &c, 1)){

		ret = c;
	}

	return ret;
}

static void writechar(int chr){

	if(!(chr & 0xFF00)){

		printf("%c", chr);
	}
	else{
		printf("<<~~ EC_0x%x ~~>>", chr);
	}
	fflush(stdout);
}

void ctl_cHandler(_UNUSED_ int v){	//handle SIGTERM

//	exit(-1);
	ctlCSeen = 1;
}

int rootOps(void* userData, UInt32 sector, void* buf, UInt8 op){

	FILE* root = userData;
	int i;

	switch(op){
		case BLK_OP_SIZE:

			if(sector == 0){	//num blocks

				if(root){

					i = fseeko(root, 0, SEEK_END);
					if(i) return false;

					 *(unsigned long*)buf = (off_t)ftello(root) / (off_t)BLK_DEV_BLK_SZ;
				}
				else{

					*(unsigned long*)buf = 0;
				}
			}
			else if(sector == 1){	//block size

				*(unsigned long*)buf = BLK_DEV_BLK_SZ;
			}
			else return 0;
			return 1;

		case BLK_OP_READ:

			i = fseeko(root, (off_t)sector * (off_t)BLK_DEV_BLK_SZ, SEEK_SET);
			if(i) return false;
#if BYTE_ORDER == LITTLE_ENDIAN
			return fread(buf, 1, BLK_DEV_BLK_SZ, root) == BLK_DEV_BLK_SZ;
#endif /* BYTE_ORDER == LITTLE_ENDIAN */
#if BYTE_ORDER == BIG_ENDIAN
			{
				UInt32 *ptr;
				ptr = malloc(BLK_DEV_BLK_SZ);
				if (ptr == NULL) {
					return (false);
				}
				if (fread(ptr, 1, BLK_DEV_BLK_SZ, root) != BLK_DEV_BLK_SZ) {
					free(ptr);
					return (false);
				}
				for (i = 0 ; i < (int)(BLK_DEV_BLK_SZ / sizeof(ptr[0])) ; i++) {
					((UInt32 *)buf)[i] = le32toh(ptr[i]);
				}
				free(ptr);
				return (true);
			}
#endif /* BYTE_ORDER == BIG_ENDIAN */

		case BLK_OP_WRITE:

			i = fseeko(root, (off_t)sector * (off_t)BLK_DEV_BLK_SZ, SEEK_SET);
			if(i) return false;
#if BYTE_ORDER == LITTLE_ENDIAN
			return fwrite(buf, 1, BLK_DEV_BLK_SZ, root) == BLK_DEV_BLK_SZ;
#endif /* BYTE_ORDER == LITTLE_ENDIAN */
#if BYTE_ORDER == BIG_ENDIAN
			{
				UInt32 *ptr;
				ptr = malloc(BLK_DEV_BLK_SZ);
				if (ptr == NULL) {
					return (false);
				}
				for (i = 0 ; i < (int)(BLK_DEV_BLK_SZ / sizeof(ptr[0])) ; i++) {
					ptr[i] = htole32(((UInt32 *)buf)[i]);
				}
				if (fwrite(ptr, 1, BLK_DEV_BLK_SZ, root) != BLK_DEV_BLK_SZ) {
					free(ptr);
					return (false);
				}
				free(ptr);
				return (true);
			}
#endif /* BYTE_ORDER == BIG_ENDIAN */
	}
	return 0;
}

SoC soc;
uint8_t *RAM_addr = NULL;//RAM alloc pointer
FILE* fast_boot_file = NULL;
void alarmhandler(){
	#ifdef GDB_SUPPORT
		printf("\r\nGDB Support - may in debug mode\r\n");
	#endif
	printf("\r\nWrite Magic Number\r\n");
	uint8_t fast_boot2_sdbuf[16+sizeof(struct SoC)];
	uint32_t i,j;
	for(i = 1;i<512;i++){//Only make 1-511 bytes zero(number 0 byte is for magic number)
		*(fast_boot2_sdbuf+i) = 0;
	}
	*fast_boot2_sdbuf = FAST_BOOT2_MAGIC_NUMBER;//Magic Number
	fwrite(fast_boot2_sdbuf,1,512,fast_boot_file);
	//Magic Number
	printf("\r\nWrite RAM Data\r\n");
	for(i = 0; i < RAM_SIZE/512; i++){
		for(j = 0;j<512;j++){
			*(fast_boot2_sdbuf+j) = RAM_addr[(i<<9)+j];
		}
		fwrite(fast_boot2_sdbuf,1,512,fast_boot_file);
	}
	printf("\r\nWrite Regs\r\n");
	for(i = 0;i<16;i++){
		fast_boot2_sdbuf[i] = soc.cpu.regs[i];
	}
	printf("CPU structure size:%u\r\n",sizeof(struct SoC));
	printf("\r\nWrite SoC Structure\r\n");
	*(struct ArmCpu *)(fast_boot2_sdbuf+16) = soc.cpu;//Get CPU's information
	//Don't use it in debug mode because they will have different Soc structure
	fwrite(fast_boot2_sdbuf,1,sizeof(struct SoC),fast_boot_file);//registers
	printf("Copy data finished.\r\n");
	fclose(fast_boot_file);
	while(1);
}
int main(int argc, char** argv){

	struct termios cfg, old;
	FILE* root = NULL;
	#ifdef GDB_SUPPORT
		int gdbPort = 0;
	#endif

	if(argc != 4 && argc != 3){
		fprintf(stderr,"usage: %s path_to_disk path_to_fast_boot2_file [gdbPort]\n", argv[0]);
		return -1;
	}

	//setup the terminal
	{
		int ret;

		ret = tcgetattr(0, &old);
		cfg = old;
		if(ret) perror("cannot get term attrs");

		cfmakeraw(&cfg);

		ret = tcsetattr(0, TCSANOW, &cfg);
		if(ret) perror("cannot set term attrs");
	}

	root = fopen(argv[1], "r+b");
	if(!root){
		fprintf(stderr,"Failed to open root device\n");
		exit(-1);
	}
	fast_boot_file = fopen(argv[2], "w+b");
	if(!fast_boot_file){
		fprintf(stderr,"Failed to open fast boot2 file\n");
		exit(-2);
	}
	if(argc >= 4){
		#ifdef GDB_SUPPORT
			gdbPort = atoi(argv[3]);
		#else
			printf("No GDB support.\n");
		#endif
	}
	socInit(&soc, socRamModeAlloc, NULL, readchar, writechar, rootOps, root);
	signal(SIGINT, &ctl_cHandler);
	signal(SIGALRM, alarmhandler);
	alarm(9);
	#warning Modify fast boot2 file in 9 seconds
	#ifdef GDB_SUPPORT
		socRun(&soc, gdbPort);
	#else
		socRun(&soc);
	#endif
	fclose(root);
	fclose(fast_boot_file);
	tcsetattr(0, TCSANOW, &old);

	return 0;
}


//////// runtime things

void* emu_alloc(UInt32 size){
	RAM_addr = calloc(size,1);
	return RAM_addr;
}

void emu_free(void* ptr){

	free(ptr);
}

UInt32 rtcCurTime(void){

	struct timeval tv;

	gettimeofday(&tv, NULL);

	return tv.tv_sec;
}

void err_str(const char* str){

	fprintf(stderr, "%s", str);
}
