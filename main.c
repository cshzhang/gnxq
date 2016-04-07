#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>


#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

#include <proto_manager.h>

#include <config.h>
#include <v2.h>
#include <can.h>
#include <uart.h>
#include <util.h>


#define BACKLOG 10
#define SERVER_PORT 9898
#define FD_SETCOUNT 32
#define BUF_SIZE 1024

#define A0_LEN 2
#define A1_LEN 64
#define A2_LEN 64
#define A3_LEN 64
#define A4_LEN 4
#define A5_LEN 4


static int running = 1;
static pthread_t g_thread_can1;
static pthread_t g_thread_can2;

PT_ProtoOpr pt_ProtoOpr;
static pthread_mutex_t multi_io_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static int g_iSend_seq = 1;
static int g_iAck_seq = 0;
static T_CanWrapper g_Can_Wrapper; 


typedef struct MultiIOInfo {
	fd_set tReadSocketSet0;	//备份
	fd_set tReadSocketSet1;	
	int count;				//记录客户端的数量
	int max_fd;
	//保存客户端的socket数组
	//index = 0 保存的是ServerSocketFd
	int iSocketFds[FD_SETCOUNT];
}T_MultiIOInfo;
T_MultiIOInfo tMultiIOInfo;

struct CanThreadArgs{
	int socket_can;
	int which;
};

/*
	全局数组
*/
u8 g_ARR_A0[A0_LEN];		//心跳间隔，初始值
u8 g_ARR_A1[A1_LEN];	//心跳帧内容初始值
u8 g_ARR_A2[2][A2_LEN];	//CAN1，设备列表
u8 g_ARR_A3[2][A3_LEN];	//CAN2，设备列表
u8 g_ARR_A4[A4_LEN];		//CAN1, 设备控制参数初始值
u8 g_ARR_A5[A5_LEN];		//CAN2，设备控制参数初始值

static int max_fd(int fds[], int length);
static void PRINT_MULTIIO(T_MultiIOInfo *pTMulitIOInfo);
static int tcp_listen(int *socketServerFd, struct sockaddr_in *socketServerAddr);
static void removeSocketFdByIndex(int index);
static int lock();
static int unlock();
static void send2AllClients(char *data, int data_len);

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


/*
	文件格式如下: 
	A0:
	00 3C

	A1:
	00 00 0A 1234567 TJZ
			
	A2:
	06 01 02 05 08 12 45
	00 01 01 02 02 02 01

	A3:
	07 01 02 03 08 12 45 56
	ff 01 01 01 02 02 01 00

	A4:
	00 01 02 20

	A5:
	00 01 02 20

	A6:
	...
	...
*/
void InitArrayFromMacStation()
{
	char data[1024];
	char *buf[8];	//目前有8行数据，指针数组，每一个数组元素都指向一行字符串
	FILE *fp;
	int index = 0;
	int line = 0;
	int i;

	//g_ARR_A0
	if((fp = fopen(FILE_MACSTATION, "a+")) == NULL)
	{
		perror("write2file");
		return -1;
	}

	while(fgets(data, 1024, fp) != NULL)
	{
		//空行，跳过
		if(data[0] == '\r' || data[0] == '\n')
			continue;
		for(i = 0; i < strlen(data); i++)
		{
			if(data[i] == '\r' || data[i] == '\n' || data[i] == '#')
			{
				data[i] = '\0';
				break;
			}
		}
		//printf("strlen[data]: %d, fgets: %s\n", strlen(data), data);
		//printf("index: %d, line: %d\n", index, line);
		if(line > 0)
		{
			buf[index] = malloc(strlen(data) + 1);
			strcpy(buf[index], data);
			str_trim(buf[index]);
			printf("buf[%d]: %s\n", index, buf[index]);

			line--;
			index++;
		}
		if(strcmp(data, "A0:") == 0 || strcmp(data, "a0:") == 0){
			line = 1;
		}
		if(strcmp(data, "A1:") == 0 || strcmp(data, "a1:") == 0){
			line = 1;
		}
		if(strcmp(data, "A2:") == 0 || strcmp(data, "a2:") == 0){
			line = 2;
		}
		if(strcmp(data, "A3:") == 0 || strcmp(data, "a3:") == 0){
			line = 2;
		}
		if(strcmp(data, "A4:") == 0 || strcmp(data, "a4:") == 0){
			line = 1;
		}
		if(strcmp(data, "A5:") == 0 || strcmp(data, "a5:") == 0){
			line = 1;
		}
	}

	fclose(fp);

	DBG_PRINTF("macstation.conf: \n");
	for(i = 0; i < 8; i++)
	{
		DBG_PRINTF("%s\n", buf[i]);
	}
	
	//心跳间隔数组
	hexStringToBytes(buf[0], g_ARR_A0);

	//心跳内容数组，数字+普通字符(ID+车站名)
	char res[64];
	str_slice(buf[1], 0, 6, res);
	//AS,SS赋值
	hexStringToBytes(res, g_ARR_A1);
	//ID,车站名
	str_slice(buf[1], 6, strlen(buf[1]), res);
	i = 0;
	while(res[i] != '\0')
	{
		g_ARR_A1[i + 3] = res[i];
		i++;
	}

	//二维数组
	hexStringToBytes(buf[2], g_ARR_A2[0]);
	hexStringToBytes(buf[3], g_ARR_A2[1]);
	//二维数组
	hexStringToBytes(buf[4], g_ARR_A3[0]);
	hexStringToBytes(buf[5], g_ARR_A3[1]);

	//两路can的控制参数
	hexStringToBytes(buf[6], g_ARR_A4);
	hexStringToBytes(buf[7], g_ARR_A5);

	/*u8 g_ARR_A0[2];		//心跳间隔，初始值
	u8 g_ARR_A1[64];	//心跳帧内容初始值
	u8 g_ARR_A2[2][64];	//CAN1，设备列表
	u8 g_ARR_A3[2][64];	//CAN2，设备列表
	u8 g_ARR_A4[4];		//CAN1, 设备控制参数初始值
	u8 g_ARR_A5[4];		//CAN2，设备控制参数初始值*/
	
}

/*
	从CAN总线上接收CAN包的线程体
	判断该线程体该使用哪一个数组g_ARR_A2，g_ARR_A3
*/
void * CanMsgListener(void *arg)
{
	struct CanThreadArgs *canThreadArgs = (struct CanThreadArgs *)arg;
	int socket_can = canThreadArgs->socket_can;
	int which = canThreadArgs->which;
	u8 *can_control;
	int i;
	int ret;
	
	while(1)
	{
		pthread_mutex_lock(&g_mutex);
		pthread_cond_wait(&g_cond, &g_mutex);

		DBG_PRINTF("wake up, CAN%d working now...\n", which);
		
		if(which == 1)
		{
			can_control = g_ARR_A4;
		}else if(which == 2)
		{
			can_control = g_ARR_A5;
		}else{
			DBG_PRINTF("unknown which-can...\n");
		}
		/*
			就是根据g_ARR_A4，g_ARR_A5的内容，构造CAN帧，呼叫CAN设备；
			并将CAN设备返回的数据存入对应的文件
		*/
		switch(can_control[0])
		{
			case MODE_STOP:	//停止模式
				
				break;
			case MODE_P2P:	//单点模式
				DBG_PRINTF("point-to-point mode...\n");
				handleP2PAndBroadcastMode(socket_can, which, can_control);
				break;
			case MODE_POLL:	//轮询模式
				DBG_PRINTF("polling mode...\n");
				break;
			case MODE_BROADCAST:	//广播
				DBG_PRINTF("broadcast mode...\n");
				handleP2PAndBroadcastMode(socket_can, which, can_control);
				break;
			default:
				break;
		}

		//复位
		can_control[0] = 0x00;
		
		pthread_mutex_unlock(&g_mutex);
	}
}


int main(int argc, char **argv)
{
	int i,ret;

	int HBCount = 0;

	struct sockaddr_in socketServerAddr;
	struct sockaddr_in socketClientAddr;
	int socketServerFd;
	int socketClientFd;

	int addrLen;
	char recvbuf[BUF_SIZE] = {0};  
	char sendbuf[BUF_SIZE] = {0};  
	int recv_len;
	int serverframe_type;
	char frame_data[BUF_SIZE] = {0};
	int v2_framelenth;

	struct sockaddr_can ptSockaddr_can;
	struct CanThreadArgs canThreadArgs;

	int uart_fd;

	//初始化T_MultiIOInfo
	tMultiIOInfo.count = 0;
	memset(tMultiIOInfo.iSocketFds, 0, FD_SETCOUNT * sizeof(int));

	//协议初始化
	ProtoInit();
	DBG_PRINTF("ProtoInit ok\n");
	pt_ProtoOpr = GetProtoOpr("V2");

	//解析"MacStation.conf"文件，内容存入数组
	InitArrayFromMacStation();
	printMacStationArr();


	if(g_ARR_A2[1][0] == 0xff && g_ARR_A3[1][0] == 0xff){
		g_Can_Wrapper.flag = 0;
	}else if(g_ARR_A2[1][0] == 0xff){
		g_Can_Wrapper.flag = 1;
	}else if(g_ARR_A3[1][0] == 0xff){
		g_Can_Wrapper.flag = 2;
	}
	
	//输出话CAN, socket, bind
	ret = can_init(&g_Can_Wrapper, &ptSockaddr_can);
	if(ret < 0)
	{
		perror("can_init");
		return -1;
	}
	DBG_PRINTF("can_init ok\n");

	//开启异步线程，处理CAN数据
	if(g_Can_Wrapper.flag != 0){
		canThreadArgs.socket_can = g_Can_Wrapper.socket_can;
		canThreadArgs.which = g_Can_Wrapper.flag;
		if(g_Can_Wrapper.flag == 1){
			pthread_create(&g_thread_can1, NULL, &CanMsgListener, (void *)&canThreadArgs);
		}else{
			pthread_create(&g_thread_can2, NULL, &CanMsgListener, (void *)&canThreadArgs);
		}
		DBG_PRINTF("create thread for CAN%d...\n", canThreadArgs.which);
	}else{
		canThreadArgs.socket_can = g_Can_Wrapper.socket_can1;
		canThreadArgs.which = 1;
		pthread_create(&g_thread_can1, NULL, &CanMsgListener, (void *)&canThreadArgs);
		DBG_PRINTF("create thread for CAN1...\n");
		canThreadArgs.socket_can = g_Can_Wrapper.socket_can2;
		canThreadArgs.which = 2;
		pthread_create(&g_thread_can2, NULL, &CanMsgListener, (void *)&canThreadArgs);
		DBG_PRINTF("create thread for CAN2...\n");
	}

	//UART 初始化
	if(UART_Open(&uart_fd, UART_PORT) < 0)	//打开串口
	{
		perror("UART_Open");
		return -1;
	}

	if(UART_Init(uart_fd) < 0)	//9600, 8N1
	{
		perror("UART_Init");
		return -1;
	}
	DBG_PRINTF("UART_Init ok\n");

	startUARTMsgListener(uart_fd);
	
	DBG_PRINTF("startUARTMsgListener ok\n");

	if(createSession(uart_fd) < 0){
		DBG_PRINTF("create session error...");
		return -1;
	}
	
	/* 
		socket -> bind -> listen
	*/
	ret = tcp_listen(&socketServerFd, &socketServerAddr);
	if(ret == -1)
	{
		perror("tcp_listen");
		return -1;
	}
	
	//多路复用IO模型
	FD_ZERO(&tMultiIOInfo.tReadSocketSet0);
	FD_SET(socketServerFd, &tMultiIOInfo.tReadSocketSet0);
	tMultiIOInfo.iSocketFds[0] = socketServerFd;
	tMultiIOInfo.max_fd = max_fd(tMultiIOInfo.iSocketFds, FD_SETCOUNT);
	
	while(running)
	{
		//PRINT_MULTIIO(&tMultiIOInfo);
		
		FD_ZERO(&tMultiIOInfo.tReadSocketSet1);
		//恢复备份的fdset
		tMultiIOInfo.tReadSocketSet1 = tMultiIOInfo.tReadSocketSet0;

		//等待总控机的连接，以及v2.0数据帧
		ret = select(tMultiIOInfo.max_fd + 1, &tMultiIOInfo.tReadSocketSet1, 
			NULL, NULL, NULL);
		if(ret < 0)
		{
			perror("select");
		}else
		{
			//是否有client连接
			if(FD_ISSET(tMultiIOInfo.iSocketFds[0], &tMultiIOInfo.tReadSocketSet1) && 
				tMultiIOInfo.count < FD_SETCOUNT - 1)
			{
				addrLen = sizeof(struct sockaddr);
				socketClientFd = accept(tMultiIOInfo.iSocketFds[0], 
						(struct sockaddr *)&socketClientAddr, &addrLen);
				DBG_PRINTF("client connect, client ip: %s\n", 
						inet_ntoa(socketClientAddr.sin_addr));
				if(-1 == socketClientFd)
				{
					perror("accept");
				}else
				{
					lock();
					for(i = 1; i < FD_SETCOUNT; i++)
					{
						//找一个fd=0 的位置，添加进去
						if(tMultiIOInfo.iSocketFds[i] == 0)
						{
							tMultiIOInfo.iSocketFds[i] = socketClientFd;
							FD_SET(socketClientFd, &tMultiIOInfo.tReadSocketSet0);
							tMultiIOInfo.count++;
							tMultiIOInfo.max_fd = max_fd(tMultiIOInfo.iSocketFds, FD_SETCOUNT);
							break;
						}
					}
					unlock();
				}
				
			}

			for(i = 1; i < FD_SETCOUNT; i++)
			{
				//有数据可读
				if(FD_ISSET(tMultiIOInfo.iSocketFds[i], 
						&tMultiIOInfo.tReadSocketSet1))
				{
					memset(recvbuf, 0, BUF_SIZE);
					if((recv_len = recv(tMultiIOInfo.iSocketFds[i], 
							recvbuf, BUF_SIZE, 0)) <= 0)
					{
						DBG_PRINTF("client disconnect, fd: %d\n", tMultiIOInfo.iSocketFds[i]);
						lock();
						removeSocketFdByIndex(i);
						unlock();
						continue;
					}
					
					//解析上位机发来的数据
					int send_seq ;
					serverframe_type = pt_ProtoOpr->GetFrameType(recvbuf, recv_len);
					
					//收到请求帧，则回送应答帧
					if(serverframe_type == FRAME_TYPE_CA)
					{
						//数据全FF
						memset(frame_data, 0xff, BUF_SIZE);

						//构造CR帧，返回给服务器
						v2_framelenth = pt_ProtoOpr->MakeFrame(0, 
												0, 
												FRAME_TYPE_CR, 
												frame_data, 
												10, 
												sendbuf);
						if(v2_framelenth == -1)
						{
							DBG_PRINTF("make-frame error\n");
							continue;
						}
						
						//发送CR应答帧
						if(send(tMultiIOInfo.iSocketFds[i], sendbuf,
								v2_framelenth, 0) <= 0)
						{
							DBG_PRINTF("client disconnect, fd: %d\n", tMultiIOInfo.iSocketFds[i]);
							lock();
							removeSocketFdByIndex(i);
							unlock();
							continue;
						}
						
					}
					
					//收到心跳帧
					if(serverframe_type == FRAME_TYPE_HB)
					{
						//TODO..
						DBG_PRINTF("recv heart beat...%d\n", HBCount++);
						send_seq = pt_ProtoOpr->GetFrameSendSeq(recvbuf, recv_len);
						g_iAck_seq = send_seq;
					}

					//收到控制命令
					//TODO
					
				}
			}

		}
		
	}
	
	return 0;
}

static int tcp_listen(int *socketServerFd, struct sockaddr_in *pt_socketServerAddr)
{
	int ret = 0;
	*socketServerFd = socket(AF_INET, SOCK_STREAM, 0);
	
	pt_socketServerAddr->sin_family 	 = AF_INET;
	pt_socketServerAddr->sin_port		 = htons(SERVER_PORT);	/* host to net, short */
	pt_socketServerAddr->sin_addr.s_addr = INADDR_ANY;
	memset(pt_socketServerAddr->sin_zero, 0, 8);

	ret = bind(*socketServerFd, (const struct sockaddr *)pt_socketServerAddr, sizeof(struct sockaddr));
	if (-1 == ret)
	{
		printf("bind error!\n");
		return -1;
	}

	ret = listen(*socketServerFd, BACKLOG);
	if (-1 == ret)
	{
		printf("listen error!\n");
		return -1;
	}

	return 0;
}



static int max_fd(int fds[], int length)
{
	int max = 0;
	int i;
	for(i = 0; i < length; i++)
	{
		if(max < fds[i])
		{
			max = fds[i];
		}
	}

	return max;
}


static void removeSocketFdByIndex(int index)
{
	FD_CLR(tMultiIOInfo.iSocketFds[index], &tMultiIOInfo.tReadSocketSet0);
	close(tMultiIOInfo.iSocketFds[index]);
	tMultiIOInfo.iSocketFds[index] = 0;
	tMultiIOInfo.count--;
	tMultiIOInfo.max_fd = max_fd(tMultiIOInfo.iSocketFds, FD_SETCOUNT);
}

static void PRINT_MULTIIO(T_MultiIOInfo *pTMulitIOInfo)
{
	int i = 0;
	int len = 0;
	char buf[BUF_SIZE] = {0};

	DBG_PRINTF("\n\n----------MultiIOInfo-----------\n");
	DBG_PRINTF("client numbers: %d\n", pTMulitIOInfo->count);
	DBG_PRINTF("max fd: %d\n", pTMulitIOInfo->max_fd);
	

	len = snprintf(buf, BUF_SIZE, "%s", "iSocketFds: ");
	for(i = 0; i < FD_SETCOUNT; i++)
	{
		len += snprintf(buf + len, BUF_SIZE - len, 
			"%d ", pTMulitIOInfo->iSocketFds[i]);
	}
	DBG_PRINTF("%s", buf);
	
	DBG_PRINTF("\n----------MultiIOInfo-----------\n\n");
}

static void send2AllClients(char *data, int data_len)
{
	int i;
	for(i = 1; i < FD_SETCOUNT; i++)
	{
		if(tMultiIOInfo.iSocketFds[i] != 0)
		{
			DBG_PRINTF("send frame to client, client-fd: %d\n", tMultiIOInfo.iSocketFds[i]);
			if(send(tMultiIOInfo.iSocketFds[i], data, data_len, 0) <= 0){
				perror("send");
				removeSocketFdByIndex(i);
			}
		}
	}
}

static int lock()
{ 
	return pthread_mutex_lock(&multi_io_lock);
}

static int unlock()
{
	return pthread_mutex_unlock(&multi_io_lock);
}

