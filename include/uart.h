#ifndef _UART_H
#define _UART_H

//一帧长度固定为64个字节
//注意，结构体中变量的顺序不能改变，从_time字段开始发送
struct uart_frame{
	//头部 6+4+1+1 = 12bytes
	long _time;	//4个字节，以s为单位，传到上位机，有上位机负责转换成ms
	u8 frame_type;	//A0,A1,F0,F1,F2,F3,F4...
	u8 data_length;	//数据场的长度
	u8 mac[6];
	//数据域 52bytes
	
	u8 data[52];
};
#define LOG_LEN 8
#define DAT_LEN 14

int UART_Init(int fd);
int UART_Open(int *fd, char* port);
void UART_Close(int fd);
int UART_Init(int fd);
void startUARTMsgListener(int UART_Fd);

int findfile(char start[255]);
void readfilename(char path[12]);
void getfilename(char name[255],int sign);

void judgefile(char data[255]);
#define OREINTATION_R 0
#define OREINTATION_T 1


#endif

