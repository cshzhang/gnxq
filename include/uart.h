#ifndef _UART_H
#define _UART_H

//һ֡���ȹ̶�Ϊ64���ֽ�
//ע�⣬�ṹ���б�����˳���ܸı䣬��_time�ֶο�ʼ����
struct uart_frame{
	//ͷ�� 6+4+1+1 = 12bytes
	long _time;	//4���ֽڣ���sΪ��λ��������λ��������λ������ת����ms
	u8 frame_type;	//A0,A1,F0,F1,F2,F3,F4...
	u8 data_length;	//���ݳ��ĳ���
	u8 mac[6];
	//������ 52bytes
	
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

