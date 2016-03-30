#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h> 
#include <sys/socket.h> 

#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

#include <netinet/in.h> 
#include <net/if.h> 

#include <pthread.h>

#include <uart.h>
#include <util.h>
#include<dirent.h>
#define LOG_LEN 8
#define DAT_LEN 14

static pthread_t thread_uart;
static pthread_t thread_uart_processor;

//一帧长度64bytes
#define FRAME_DATA_LEN 52
#define FILENAME_LEN 18
//proto type
#define FRAME_TYPE_REQUEST   0XFF
#define FRAME_TYPE_INIT      0X00
#define FRAME_TYPE_HB        0X01
#define FRAME_TYPE_FILE      0X0F
#define FRAME_TYPE_ACK_FILE  0xEF
#define FRAME_TYPE_WR_ARR    0xA1
#define FRAME_TYPE_RD_ARR    0xA2
#define FRAME_TYPE_FND_FILE  0xF1
#define FRAME_TYPE_WR_FILE   0xF2
#define FRAME_TYPE_RD_FILE   0xF3
#define FRAME_TYPE_DEL_FILE  0xF4

#define BUF_SIZE 1024
#define BUF_SIZE_INCREMENT 1024

#define A0_LEN 2
#define A1_LEN 64
#define A2_LEN 64
#define A3_LEN 64
#define A4_LEN 4
#define A5_LEN 4

extern u8 g_ARR_A0[A0_LEN];		//心跳间隔，初始值
extern u8 g_ARR_A1[A1_LEN];	//心跳帧内容初始值
extern u8 g_ARR_A2[2][A2_LEN];	//CAN1，设备列表
extern u8 g_ARR_A3[2][A3_LEN];	//CAN2，设备列表
extern u8 g_ARR_A4[A4_LEN];		//CAN1, 设备控制参数初始值
extern u8 g_ARR_A5[A5_LEN];		//CAN2，设备控制参数初始值

extern pthread_mutex_t g_mutex;
extern pthread_cond_t g_cond;

u8 g_mac[6];

u8 g_filename[64];

int g_isFileAckReceived = 0;
int g_isFileAckTimeout = 0;

int g_uart_fd;
long g_lasttime = 0;

struct timeval g_HBtime;
int isSessionCreated = 0;
int isFileAckReceived = 0;
int isFileAckTimeout = 0;

int g_oreintation;


char logname[LOG_LEN],minlog[255];
char datname[DAT_LEN],mindat[255];
double recvfile=0,searfile=0;
char logpath[]="/etc/can_log";
char datpath[]="/etc/can_data";
double min=999999999999999;


static void print_arr(u8 array[], int len)
{
	int i;
	for(i = 0; i < len; i++)
	{
		DBG_PRINTF("%02x ", array[i]);
	}
	DBG_PRINTF("\n");
}

static void printMacStationArr()
{
	DBG_PRINTF("g_ARR_A0: ");
	print_arr(g_ARR_A0, A0_LEN);
	DBG_PRINTF("g_ARR_A1: ");
	print_arr(g_ARR_A1, A1_LEN);

	DBG_PRINTF("g_ARR_A4: ");
	print_arr(g_ARR_A4, A4_LEN);
	DBG_PRINTF("g_ARR_A5: ");
	print_arr(g_ARR_A5, A5_LEN);
}

int UART_Open(int *fd, char* port)
{
    *fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (*fd < 0)
	{
	    printf("Can't Open Serial Port");
	    return -1;
	}
	//判断串口的状态是否为阻塞状态  
	if(fcntl(*fd, F_SETFL, 0) < 0)
	{
	   printf("fcntl failed!/n");
	   return -1;
	}
	
	return 1;
}

void UART_Close(int fd)  
{  
	close(fd);  
} 

/*
  * 波特率9600
  * 8N1
  *
*/
int UART_Init(int fd)
{	
	//恢复串口状态为阻塞状态
	struct termios tOpt; 
	tcgetattr(fd, &tOpt);

	//一般会添加这两个选项
	tOpt.c_cflag |= CLOCAL | CREAD;
	
	//8N1
	tOpt.c_cflag &= ~PARENB;
	tOpt.c_cflag &= ~CSTOPB;
	tOpt.c_cflag &= ~CSIZE;
	tOpt.c_cflag |= CS8;

	//diable hardware flow-ctrl
	tOpt.c_cflag &= ~CRTSCTS;

	//使用原始模式通讯
	tOpt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

	//disable soft flow-ctrl
	tOpt.c_iflag &= ~(IXON | IXOFF | IXANY);  

	//原始模式输出
	tOpt.c_oflag &= ~OPOST;

	//baud rates
	cfsetispeed(&tOpt,B9600);
	cfsetospeed(&tOpt,B9600);

	//最少1个字节，50ms
	//tOpt.c_cc[VTIME] = 0;
	//tOpt.c_cc[VMIN] = 0;

	//tcflush(fd, TCIOFLUSH);
	
	tcsetattr(fd,TCSANOW,&tOpt);
	
	return 0;
}

int UART_Recv(int fd, struct uart_frame *frame)
{
	int len;
	DBG_PRINTF("\nUART_Recv()...\n");
	len = read(fd, frame, sizeof(struct uart_frame));
	if(len < 0){
		perror("UART_Recv()...");
	}
	
	DBG_PRINTF("frame-size: %d, frame: ", len);
	parseUartFrame(frame);
	g_oreintation = OREINTATION_R;
	
	return len;
}

int UART_Send(int fd, struct uart_frame *frame)
{
	char buf[1024];
	int len;

	DBG_PRINTF("UART_Send()...\n");
	DBG_PRINTF("frame-size: %d, frame: ", sizeof(struct uart_frame));
	parseUartFrame(frame);
	
	len = write(fd, frame, sizeof(struct uart_frame));

	if(len < 0){
		perror("write");
		return -1;
	}
	
	g_oreintation = OREINTATION_T;

	return len;
}

static int data2file(char *data, char *file_name)
{
	FILE *fp;
	int ret;
	
	if((fp = fopen(file_name, "w")) == NULL)
	{
		perror("write2file");
		return -1;
	}
	DBG_PRINTF("\ndata2file()...\n");

	ret = fputs(data, fp);
	if(ret < 0){
		perror("fputs()...\n");
		return -1;
	}

	fclose(fp);

	return 0;
}

static int file2buf(char *fileName, char **buf)
{
	FILE *fp;
	char ch;
	int len = 0;
	int max_size = BUF_SIZE;
	char *newbase = NULL;

	DBG_PRINTF("file2buf()...\n");

	*buf = malloc(max_size * sizeof(char));
	if(!(*buf)){
		perror("malloc");
		return -1;
	}
	
	if((fp = fopen(fileName, "r")) == NULL)
	{
		perror("fopen");
		return -1;
	}

	ch = fgetc(fp);

	while(!feof(fp))
	{
		(*buf)[len++] = ch;
		if(len >= max_size)		//增加内存
		{
			newbase = realloc(*buf, (max_size+BUF_SIZE_INCREMENT) * sizeof(char));
			if(!newbase){
				perror("realloc");
				fclose(fp);
				return -1;
			}
			max_size += BUF_SIZE_INCREMENT;
			DBG_PRINTF("realloc %d size\n", max_size);
			*buf = newbase;
		}
		ch = fgetc(fp);
	}

	fclose(fp);

	return len;
	
}

void makeDataForFrame(struct uart_frame *frame, u8 mac[6], long _time, 
	u8 frame_type, u8 data_length, u8 data[52])
{
	int i = 0;

	memset(frame->data, 0xff, 52);
	
	for(i = 0; i < 6; i++)
	{
		frame->mac[i] = mac[i];
	}
	frame->_time = _time;
	frame->frame_type = frame_type;
	frame->data_length = data_length;
	for(i = 0; i < data_length; i++)
	{
		frame->data[i] = data[i];
	}
}

/*
	返回数据内容的长度
*/
static int prepareHBContent(u8 data[52], u8 AS, u8 SS, u8 ID[8], u8 station[42])
{
	int i = 0;
	int cnt = 0;
	int IDLen = strlen(ID);
	int stationLen = strlen(station);
	data[0] = AS;
	cnt++;
	data[1] = SS;
	cnt++;
	for(i = 0; i < IDLen; i++)
	{
		data[cnt + i] = ID[i];
	}
	cnt += IDLen;
	for(i = 0; i < stationLen; i++)
	{
		data[cnt + i] = station[i];
	}
	cnt += stationLen;

	return cnt;
}

/*
	struct uart_frame{
		//头部 6+4+1+1 = 12bytes
		long _time;	//4个字节，以s为单位，传到上位机，有上位机负责转换成ms
		u8 frame_type;	//A0,A1,F0,F1,F2,F3,F4...
		u8 data_length;	//数据场的长度
		u8 mac[6];
		//数据域 52bytes
		
		u8 data[52];
	};
*/
static void putHeadInFrame(struct uart_frame *frame, 
				long _time, u8 frame_type, u8 data_length, u8 mac[6])
{
	int i;
	frame->_time = _time;
	frame->frame_type = frame_type;
	frame->data_length = data_length;
	for(i = 0; i < 6; i++)
	{
		frame->mac[i] = mac[i];
	}
}

static void putContentInFrame(struct uart_frame *frame, u8 data[FRAME_DATA_LEN])
{
	int i;
	for(i = 0; i < FRAME_DATA_LEN; i++)
	{
		frame->data[i] = data[i];
	}
}

int parseUartFrameForLog(struct uart_frame *frame, char *buf, int buf_size)
{
	int len = 0;
	int i;
	char buf_time[BUF_SIZE];
	
	if(g_oreintation == OREINTATION_R){
		len += snprintf(buf+len, buf_size-len, "%c ", 'R');
	}else{
		len += snprintf(buf+len, buf_size-len, "%c ", 'T');
	}

	//格式化mac
	for(i = 0; i < 6; i++)
	{
		len += snprintf(buf+len, buf_size-len, "%02x", frame->mac[i]);
	}
	//格式化时间:  yyyy MM dd HH mm ss
	formatDateTime(frame->_time, buf_time, BUF_SIZE);
	len += snprintf(buf+len, buf_size-len, " %s ", buf_time);

	//格式化帧类型
	len += snprintf(buf+len, buf_size-len, "%02x ", frame->frame_type);

	//格式化数据场的长度
	len += snprintf(buf+len, buf_size-len, "%02x ", frame->data_length);

	//格式化数据
	for(i = 0; i < frame->data_length; i++)
	{
		len += snprintf(buf+len, buf_size - len, "%02x", frame->data[i]);
	}

	buf[len++] = '\n';
	
	return len;
}

int parseUartFrame(struct uart_frame *uartFrame)
{
	int len = 0;
	int i;
	char buf[1024] = {0};
	
	//格式化time
	len += snprintf(buf+len, 1024 - len, "%ld ", uartFrame->_time);
	
	//格式化frametype
	len += snprintf(buf+len, 1024 - len, "%02x ", uartFrame->frame_type);

	//格式化datalength
	len += snprintf(buf+len, 1024 - len, "%02x ", uartFrame->data_length);
	//格式化mac
	for(i = 0; i < 6; i++)
	{
		len += snprintf(buf+len, 1024-len, "%02x ", uartFrame->mac[i]);
	}

	//格式化数据
	for(i = 0; i < uartFrame->data_length; i++)
	{
		len += snprintf(buf+len, 1024 - len, "%02x ", uartFrame->data[i]);
	}

	buf[len++] = '\n';

	printf("%s\n", buf);

	return len;
	
}

static void handleRdArr(struct uart_frame *frame, int uart_fd)
{
	DBG_PRINTF("handleRdArr()...\n");
	int pos;
	int i,j,k;
	int A2_Len, A3_Len;
	u8 data_len = frame->data_length;
	u8 data[data_len];
	u8 content[FRAME_DATA_LEN];
	struct uart_frame frame_resp;
	for(i = 0; i < data_len; i++)
	{
		data[i] = frame->data[i];
	}
	memset(content, 0xff, FRAME_DATA_LEN);
	
	/*
		返回帧的内容: 数组名 + 维数 + 数组内容
		A0 01 000A
	*/
	switch(data[0])
	{
		case 0xA0:	//读A0，心跳间隔
		
			putHeadInFrame(&frame_resp, getSystemTimeInSecs(), 
				FRAME_TYPE_RD_ARR, 2+A0_LEN, g_mac);
			content[0] = 0xA0;	//数组名
			content[1] = 0x01;	//维数
			content[2] = g_ARR_A0[0];
			content[3] = g_ARR_A0[1];
			putContentInFrame(&frame_resp, content);
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			break;
		case 0xA1:	//读A1，心跳内容
		
			data_len = 2 + 3 + g_ARR_A1[2];	//A1 01 AS SS N D1 D2 D3...
			putHeadInFrame(&frame_resp, getSystemTimeInSecs(), 
				FRAME_TYPE_RD_ARR, data_len, g_mac);
			content[0] = 0xA1;	//数组名
			content[1] = 0x01;	//维数
			for(i = 0; i < 3 + g_ARR_A1[2]; i++)
			{
				content[2 + i] = g_ARR_A1[i];
			}
			putContentInFrame(&frame_resp, content);
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			break;
		case 0xA2:	//二维数组，CAN1设备列表
			/*
				注意，这里可能多余一帧，为简单起见先假设为一帧
			*/
			A2_Len = g_ARR_A2[0][0] * 2 + 2;
			putHeadInFrame(&frame_resp, getSystemTimeInSecs(), 
				FRAME_TYPE_RD_ARR, 2+A2_Len, g_mac);
			content[0] = 0xA2;	//数组名
			content[1] = 0x02;	//维数
			//将二维数组，赋值给一位数组content
			k = 2;
			for(i = 0; i < 2; i++){
				for(j = 0; j < A2_Len/2; j++){
					content[k++] = g_ARR_A2[i][j];
				}
			}
			putContentInFrame(&frame_resp, content);
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			break;
		case 0xA3:	//二维数组，CAN2设备列表
			
			A3_Len = g_ARR_A3[0][0] * 2 + 2;
			putHeadInFrame(&frame_resp, getSystemTimeInSecs(), 
				FRAME_TYPE_RD_ARR, 2+A3_Len, g_mac);
			content[0] = 0xA3;	//数组名
			content[1] = 0x02;	//维数
			//将二维数组，赋值给一位数组content
			k = 2;
			for(i = 0; i < 2; i++){
				for(j = 0; j < A3_Len/2; j++){
					content[k++] = g_ARR_A3[i][j];
				}
			}
			putContentInFrame(&frame_resp, content);
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			break;
		case 0xA4:	//CAN1控制帧
		
			putHeadInFrame(&frame_resp, getSystemTimeInSecs(), 
				FRAME_TYPE_RD_ARR, 2+A4_LEN, g_mac);
			content[0] = 0xA4;	//数组名
			content[1] = 0x01;	//维数
			content[2] = g_ARR_A4[0];
			content[3] = g_ARR_A4[1];
			content[4] = g_ARR_A4[2];
			content[5] = g_ARR_A4[3];
			putContentInFrame(&frame_resp, content);
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			break;
		case 0xA5:	//CAN2控制帧
		
			putHeadInFrame(&frame_resp, getSystemTimeInSecs(), 
				FRAME_TYPE_RD_ARR, 2+A5_LEN, g_mac);
			content[0] = 0xA5;	//数组名
			content[1] = 0x01;	//维数
			content[2] = g_ARR_A5[0];
			content[3] = g_ARR_A5[1];
			content[4] = g_ARR_A5[2];
			content[5] = g_ARR_A5[3];
			putContentInFrame(&frame_resp, content);
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			break;
		default:
			DBG_PRINTF("unkown array type\n");
			return;
	}
}

static void handleWrArr(struct uart_frame *frame, int uart_fd)
{
	DBG_PRINTF("handleWrArr()...\n");
	int pos;
	int i;
	u8 data_len = frame->data_length;
	u8 data[data_len];
	for(i = 0; i < data_len; i++)
	{
		data[i] = frame->data[i];
	}
	u8 index = data[1];
	u8 wr_len = data[2];

	i = 3;
	
	switch(data[0])	//数组名
	{
	case 0xA0:	//心跳间隔
		for(pos = index; pos < wr_len; pos++)
		{
			g_ARR_A0[pos] = data[i++];
		}
		break;
	case 0xA1:	//AS SS ID 车站名
		//目前只支持写入AS SS
		if(index + wr_len > 2)
			break;
		for(pos = index; pos < wr_len; pos++)
		{
			g_ARR_A1[pos] = data[i++];
		}
		break;
	case 0xA2:	
		/*
			不如重新发送文件，再控制程序重新解析一遍文件来更新全局数组
		*/
		break;
	case 0xA3:
		/*
			同上
		*/
		break;
	case 0xA4:	//CAN1控制参数
		pthread_mutex_lock(&g_mutex);
		for(pos = index; pos < wr_len; pos++)
		{
			g_ARR_A4[pos] = data[i++];
		}
		//唤醒CAN的工作线程
		pthread_cond_broadcast(&g_cond);
		pthread_mutex_unlock(&g_mutex);
		break;
	case 0xA5:
		pthread_mutex_lock(&g_mutex);
		for(pos = index; pos < wr_len; pos++)
		{
			g_ARR_A5[pos] = data[i++];
		}
		//唤醒CAN的工作线程
		pthread_cond_broadcast(&g_cond);
		pthread_mutex_unlock(&g_mutex);
		break;
	default:
		break;
	}

	//打印数组
	//printMacStationArr();
}

static void replyFileAck(int uart_fd)
{
	DBG_PRINTF("replyFileAck()...\n");
	struct uart_frame file_ack;
	u8 content[FRAME_DATA_LEN];
	memset(content, 0xff, FRAME_DATA_LEN);
	content[0] = 'O';
	content[1] = 'K';

	file_ack._time = getSystemTimeInSecs();
	memcpy(file_ack.mac, g_mac, 6);
	file_ack.frame_type = FRAME_TYPE_WR_FILE;
	file_ack.data_length = 2;
	putContentInFrame(&file_ack, content);

	UART_Send(uart_fd, &file_ack);
}

static void waitFileAckFromServer()
{
	int cnt;
	while(isFileAckReceived == 0)
	{
		//DBG_PRINTF("waiting 200ms...\n");
		usleep(200 * 1000);	//200ms
		cnt++;
		//等20s
		if(cnt == 100) {
			isFileAckTimeout = 1;
			break;
		}else{
			isFileAckTimeout = 0;
		}
	}
	
	isFileAckReceived = 0;
}

static void handleRdFile(struct uart_frame *frame, int uart_fd)
{
	DBG_PRINTF("handleRdFile()...\n");
	char *buf = NULL;
	int pos;
	int i;
	int len;
	u8 data_len = frame->data_length;
	char file_name[data_len+1];
	char absolute_path[64];
	int howMany;
	u8 content[FRAME_DATA_LEN];
	struct uart_frame frame_resp;
	
	for(i = 0; i < data_len; i++)
	{
		file_name[i] = frame->data[i];
	}
	file_name[i] = '\0';
	
	//根据文件后缀名，判断server读取哪个目录下的文件
	if(strstr(file_name, ".log")){
		DBG_PRINTF("read log file...\n");
		strcpy(absolute_path, CANLOG_PATH);
	}else if(strstr(file_name, ".cfg")){
		DBG_PRINTF("read config file...\n");
		strcpy(absolute_path, CANCFG_PATH);
	}else if(strstr(file_name, ".dat")){
		DBG_PRINTF("read data file...\n");
		strcpy(absolute_path, CANDATA_PATH);
	}else {
		DBG_PRINTF("Error file name\n");
		return;
	}
	
	strcat(absolute_path, file_name);
	DBG_PRINTF("file name: %s\n", absolute_path);

	len = file2buf(absolute_path, &buf);
	DBG_PRINTF("file length : %d\n", len);
	if(len < 0){
		DBG_PRINTF("file2buf error.\n");
		return;
	}
	buf[len] = '\0';
	DBG_PRINTF("file content:\n");
	DBG_PRINTF("%s\n", buf);

	//一帧的内容长度52
	frame_resp._time = getSystemTimeInSecs();
	for(i = 0; i < 6; i++) frame_resp.mac[i] = g_mac[i];
	frame_resp.frame_type = FRAME_TYPE_FILE;

	i = 0;
	len = 0;
	while(buf[i] != '\0')
	{
		frame_resp.data[len++] = buf[i];
		if(len == FRAME_DATA_LEN){	//一帧填满
			frame_resp.data_length = FRAME_DATA_LEN;
			UART_Send(uart_fd, &frame_resp);
			//日志
			writeFrame2log(&frame_resp);
			len = 0;			
			//usleep(2000 * 1000);	//10 ms
			waitFileAckFromServer();
			if(isFileAckTimeout){
				DBG_PRINTF("send file time out...\n");
				isFileAckTimeout = 0;
				return;
			}
		}
		i++;
	}
	// 1.整帧结束，再发一空帧，让服务器知道，文件已经发送结束
	if(i % FRAME_DATA_LEN == 0){
		memset(content, 0xff, FRAME_DATA_LEN);
		frame_resp.data_length = 0;
		putContentInFrame(&frame_resp, content);
		UART_Send(uart_fd, &frame_resp);
	}else {
		// 2.不完整帧结束，就把最后一帧发出去
		frame_resp.data_length = len;
		UART_Send(uart_fd, &frame_resp);
	}
	//日志
	writeFrame2log(&frame_resp);
	
	waitFileAckFromServer();
	if(isFileAckTimeout){
		DBG_PRINTF("send file time out...\n");
		isFileAckTimeout = 0;
		return;
	}
	//释放buf指向的内存区域
	free(buf);
}

static void handleDelFile(struct uart_frame *frame, int uart_fd)
{
	int i;
	int len;
	u8 data_len = frame->data_length;
	char file_name[data_len+1];
	char abs_path[64];

	for(i = 0; i < data_len; i++)
	{
		file_name[i] = frame->data[i];
	}
	file_name[i] = '\0';
	if(strstr(file_name, ".log")){
		DBG_PRINTF("del log file...\n");
		strcpy(abs_path, CANLOG_PATH);
	}else if(strstr(file_name, ".cfg")){
		DBG_PRINTF("del config file...\n");
		strcpy(abs_path, CANCFG_PATH);
	}else if(strstr(file_name, ".dat")){
		DBG_PRINTF("del data file...\n");
		strcpy(abs_path, CANDATA_PATH);
	}else {
		DBG_PRINTF("Error file name\n");
		return;
	}
	strcat(abs_path, file_name);

	DBG_PRINTF("file name: %s\n", abs_path);

	if(remove(abs_path) == 0){
		DBG_PRINTF("Removed %s\n", abs_path);
		//TODO
		//发送确定帧给服务器
	}else{
		DBG_PRINTF("Removed error, no such file\n");
		//TODO
		//发送通知帧给服务器
	}
}

static void handle(struct uart_frame *frame, int uart_fd)
{
	int i;
	u8 frame_type = frame->frame_type;
	u8 data_len = frame->data_length;
	u8 data[data_len];
	
	struct uart_frame fileframe;
	u8 content[FRAME_DATA_LEN];
	memset(content, 0xff, FRAME_DATA_LEN);
	for(i = 0; i < data_len; i++)
	{
		data[i] = frame->data[i];
	}

	//服务器对发过来的文件大小需要做限制，不能大于4K
	static char file_data[4096];			//这段内存在静态存储区
	static char *pt_FileData = file_data;
	static int totle_len = 0;
	int len=0;
	char abs_path[64];

	DBG_PRINTF("\nhandle()...\n");
	DBG_PRINTF("recv_frame type: %02X\n", frame_type);
	
	switch(frame_type)
	{
		case FRAME_TYPE_INIT:
			DBG_PRINTF("init session success...\n");
			isSessionCreated = 1;
			break;
		case FRAME_TYPE_FILE:
			//接收服务器发过来的文件帧，文件名在 g_filename 变量中
			// 注意 : 服务器发来的文件大小需要有限制，4K
			
			// 1.向server回复ACK,type为: FRAME_TYPE_ACK_FILE
			replyFileAck(uart_fd);
			memcpy(pt_FileData, data, data_len);

			pt_FileData += data_len;
			totle_len += data_len;

			if(data_len < FRAME_DATA_LEN){
				pt_FileData++;
				*pt_FileData = '\0';
				
				//根据文件后缀名判断文件写入哪个目录下
				if(strstr(g_filename, ".log")){
					DBG_PRINTF("write log file...\n");
					strcpy(abs_path, CANLOG_PATH);
				}else if(strstr(g_filename, ".cfg")){
					DBG_PRINTF("write config file...\n");
					strcpy(abs_path, CANCFG_PATH);
				}else if(strstr(g_filename, ".dat")){
					DBG_PRINTF("write data file...\n");
					strcpy(abs_path, CANDATA_PATH);
				}else {
					DBG_PRINTF("Error file name\n");
					return;
				}
				
				strcat(abs_path, g_filename);
				DBG_PRINTF("file name: %s\n", abs_path);
				data2file(file_data, abs_path);
				pt_FileData = file_data;
				totle_len = 0;	
			}
			break;
		case FRAME_TYPE_WR_ARR:
			handleWrArr(frame, uart_fd);
			break;
		case FRAME_TYPE_RD_ARR:
			handleRdArr(frame, uart_fd);
			break;
		case FRAME_TYPE_FND_FILE:
			//将查找到的文件，返回；
			//帧写入日志文件		
			
			len=findfile(data);
			printf("findfile success!\n");
			putHeadInFrame(&fileframe, getSystemTimeInSecs(), 
				FRAME_TYPE_FND_FILE, len, g_mac);
			printf("data[%d]=%c\n",data_len-1,data[data_len-1]);
			if('g'==data[data_len-1])
				{
					for(i=0;i<strlen(minlog);i++)
						{
						content[i]=minlog[i];
						}
				}
			else{
				for(i=0;i<strlen(mindat);i++)
						{
						content[i]=mindat[i];
						}
				}
			putContentInFrame(&fileframe, content);
			UART_Send(uart_fd, &fileframe);
			//日志
			writeFrame2log(&fileframe);
			break;
		case FRAME_TYPE_WR_FILE:
			//得到文件名，存入一个全局变量 : g_filename
			for(i = 0; i < data_len; i++)
			{
				g_filename[i] = data[i];
			}
			g_filename[i] = '\0';
			DBG_PRINTF("file name from server: %s\n", g_filename);
			break;
		case FRAME_TYPE_RD_FILE:
			handleRdFile(frame, uart_fd);
			break;
		case FRAME_TYPE_DEL_FILE:
			handleDelFile(frame, uart_fd);
			break;
	}
}

/*
	初始化和服务器的连接
*/
int createSession(int uart_fd)
{
	int ret;
	struct uart_frame initFrame;
	struct uart_frame Recv_Frame;
	
	char FrameContent[52];
	char InitAck[8];
	memset(g_mac, 0x00, 6);
	ret = getMacAddr(g_mac, "eth0");
	if(ret < 0){
		DBG_PRINTF("get mac-address error\n");
	}
	
	//保证除了内容之外，用0xff填充
	memset(FrameContent, 0xff, FRAME_DATA_LEN);
	putHeadInFrame(&initFrame, getSystemTimeInSecs(), 
		FRAME_TYPE_INIT, 0, g_mac);
	putContentInFrame(&initFrame, FrameContent);

	if(UART_Send(uart_fd, &initFrame) < 0)
	{
		DBG_PRINTF("UART_Send error\n");
		return -1;
	}
	writeFrame2log(&initFrame);

	return 0;
}

//macdate.log
void writeFrame2log(struct uart_frame *frame)
{
	time_t timer;
	struct tm *tm;
	char filename[64] = {0};
	char absolute_path[64];
	char buf[BUF_SIZE] = {0};
	int len = 0;
	int i;
	int year, day, mon;
	
	FILE *fp;

	memset(filename, 0x00, 64);
	memset(absolute_path, 0, 64);
	memset(buf, 0x00, BUF_SIZE);

	// 1.构造文件名: "aabbccddeeff20140322.log"
	timer = time(NULL);  
	tm = localtime(&timer);
	
	day = tm->tm_mday;
	mon = tm->tm_mon + 1;
	year = tm->tm_year + 1900;

	for(i = 0; i < 6; i++){
		len += snprintf(filename + len, 64 - len, "%02x", g_mac[i]);
	}
	len += snprintf(filename + len, 64 - len, "%04d", year);
	len += snprintf(filename + len, 64 - len, "%02d", mon);
	len += snprintf(filename + len, 64 - len, "%02d", day);
	len += snprintf(filename + len, 64 - len, "%s", ".log");
	filename[len++] = '\0';
	
	// 2.打开文件，写入帧内容
	/**
	  *	"a+":  按文本文件打开，读、追加写                                                                                                         
	  *
	  */
	strcat(absolute_path, CANLOG_PATH);
	strcat(absolute_path, filename);

	DBG_PRINTF("writeFrame2log()...: %s\n", absolute_path);
	
	if((fp = fopen(absolute_path, "a+")) == NULL)
	{
		perror("write2file");
		return -1;
	}

	parseUartFrameForLog(frame, buf, BUF_SIZE);
	
	fputs(buf, fp);

	fclose(fp);
}

static void getIDAndStationFromA2(u8 *ID, u8 *station)
{
	int len = g_ARR_A1[2];	//ID+station 的长度，目前ID固定为5个字节
	int i;

	//ID  5个字节   "12345"
	for(i = 0; i < 5; i++){
		ID[i] = g_ARR_A1[3 + i];
	}
	ID[i] = '\0';
	//station
	for(i = 0; i < len - 5; i++){
		station[i] = g_ARR_A1[8 + i];
	}
	station[i] = '\0';
}

void *UARTMsgProcessor(void *arg)
{
	struct uart_frame *uart_frame = (struct uart_frame *)arg;
	// 服务器发送请求帧，来请求MAC地址
	if(uart_frame->frame_type == FRAME_TYPE_REQUEST) {
		createSession(g_uart_fd);
	}else{
		//服务器发来的一切交互帧，都得写入日志文件: mac+date.log
		writeFrame2log(uart_frame);
		
		handle(uart_frame, g_uart_fd);
	}
}

void *UARTMsgListener(void *arg)
{
	int UART_Fd = (int)arg;
	int len, ret, i, data_len;
	int seconds = 60;

	u8 FrameContent[FRAME_DATA_LEN] = {0};
	
	fd_set fd_read;

	char *tmp_file_data;

	struct uart_frame HB_Frame;
	struct uart_frame Recv_Frame;

	u8 AS,SS;
	u8 ID[16];
	u8 station[16];

	while(1)
	{
		FD_ZERO(&fd_read);
		FD_SET(UART_Fd, &fd_read); 
		seconds = g_ARR_A0[1] | (g_ARR_A0[0] << 8);
		
		//时间的边界检查
		if(seconds > 3600 || seconds < 3){
			seconds = 60;
		}
		g_HBtime.tv_sec = seconds;
    	g_HBtime.tv_usec = 0;

		getIDAndStationFromA2(ID, station);
		
		ret = select(UART_Fd+1, &fd_read, NULL, NULL, &g_HBtime);
		
		if(isSessionCreated && ret == 0)	//超时
		{
			//发心跳
			memset(FrameContent, 0xff, FRAME_DATA_LEN);
			AS = g_ARR_A1[0];
			SS = g_ARR_A1[1];
			data_len = prepareHBContent(FrameContent, AS, SS, ID, station);
			putHeadInFrame(&HB_Frame, getSystemTimeInSecs(), 
				FRAME_TYPE_HB, data_len, g_mac);
			putContentInFrame(&HB_Frame, FrameContent);
			
			if(UART_Send(UART_Fd, &HB_Frame) < 0)
			{
				DBG_PRINTF("UART_Send error\n");
			}

			//心跳帧，存入macdate.log
			//大于一天，或者状态发生异常，将帧存入日志文件
			if((HB_Frame._time - g_lasttime) > 24 * 60 * 60 
				|| AS != 0xA5 
				|| SS != 0xA5){
				g_lasttime = HB_Frame._time;

				writeFrame2log(&HB_Frame);
			}
		}else if(ret > 0)
		{
			//串口有数据，读取出来
			if(ret = UART_Recv(UART_Fd, &Recv_Frame) < 0)
			{
				DBG_PRINTF("UART_Recv error\n");
			}
			
			if(Recv_Frame.frame_type == FRAME_TYPE_RD_FILE 
				&& Recv_Frame.data[0] == 'O'
				&& Recv_Frame.data[1] == 'K'){
					DBG_PRINTF("receive file ack from server...\n");
					isFileAckReceived = 1;
			}else{
				pthread_create(&thread_uart_processor, NULL, &UARTMsgProcessor, (void *)&Recv_Frame);
				pthread_detach(thread_uart_processor);
			}
		}
	}
	return NULL;
}

void startUARTMsgListener(int UART_Fd)
{
	g_uart_fd = UART_Fd;
	pthread_create(&thread_uart, NULL, &UARTMsgListener, (void *)UART_Fd);
}


int chartoint(int begin, int end,char c[14])
{
	int value=0,i=0;
	for(i=begin;i<end;i++)
	{
		value=value*10+c[i]-48;
	}
	return value;
}

double filenametosecond(char t[14],int data_len)
{
	struct tm ti;
	ti.tm_year=chartoint(0,4,t)-1900;
	ti.tm_mon=chartoint(4,6,t);
	ti.tm_mday=chartoint(6,8,t);
	ti.tm_hour=0;
	ti.tm_min=0;
	ti.tm_sec=0;
	if(data_len>LOG_LEN)
	{
		ti.tm_hour=chartoint(8,10,t);
		ti.tm_min=chartoint(10,12,t);
		ti.tm_sec=chartoint(12,14,t);
		//return mktime(&ti)-2649600;
	}
	return mktime(&ti)-2649600;
}
void getfilename(char name[255],int data_len)
{
	int i=0,j=0;
	for(i=0;i<strlen(name);i++)
		{
			if('.'==name[i])
				{
					break;	
				}
		}
printf(".=%d\n",i);
	if('l'==name[i+1])
	{
		for(j=0;j<data_len;j++)
			{
			 logname[j]=name[i-data_len+j];
			}
	}
	else{
			for(j=0;j<data_len;j++)
			{
			 	datname[j]=name[i-data_len+j];
			}
	}
}
void readfilename(char path[12],char goal[4])
{
	struct dirent *dp;
 	DIR   *dfd;
 	if( (dfd = opendir(path)) == NULL )
	   {
	          printf("open dir failed! dir: %s", path);
	          return;
	     }
if('g'==goal[3])//log
{
	for(dp = readdir(dfd); NULL!=dp; dp = readdir(dfd))
	     {   
	    if(strstr(dp->d_name,goal)!=NULL)
	        	  {
	         printf("%s\n",dp->d_name);
				getfilename(dp->d_name,LOG_LEN);
				
printf("recvfile=%lf\n",searfile);
				searfile=filenametosecond(logname,LOG_LEN);
				printf("searfile=%lf\n",searfile);
				
				if(fabs(recvfile-searfile)<min)
					{
						min=fabs(recvfile-searfile);
printf("min=%d\n",min);	
						int i=0;	
						for(i=0;i<strlen(dp->d_name);i++)
							{
								minlog[i]=dp->d_name[i];
							}									
					}
	           }
	      }
}
else{//dat

for(dp = readdir(dfd); NULL!=dp; dp = readdir(dfd))
	     {   
	    if(strstr(dp->d_name,goal)!=NULL)
	        	  {
	         printf("%s\n",dp->d_name);
				getfilename(dp->d_name,DAT_LEN);
				
printf("recvfile=%lf\n",recvfile);
				searfile=filenametosecond(datname,DAT_LEN);
				printf("searfile=%lf\n",searfile);
				if(fabs(recvfile-searfile)<=min)
					{
						min=fabs(recvfile-searfile);
printf("min=%d\n",min);	
						int i=0;	
						for(i=0;i<strlen(dp->d_name);i++)
							{
								mindat[i]=dp->d_name[i];
							}									
					}
	           }
	      }
}

}

int findfile(char start[255])
{
	int er=0,j=0;
	recvfile=filenametosecond(start,DAT_LEN);
	printf("start=%c\n",start[LOG_LEN+1]);
	if('l'==start[LOG_LEN+1])//log
		{
			readfilename(logpath,".log");
			printf("lasted:%s\n",minlog);
			min=999999999999999;
			return strlen(minlog);
		}
		else//dat
		{
			readfilename(datpath,".dat");
				//mindat[DAT_LEN]='\0';
			printf("lasted:%s\n",mindat);
				min=999999999999999;
				return strlen(mindat);
		}
		
							
}

