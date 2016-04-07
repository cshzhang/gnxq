#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include <can.h>
#include <util.h>

#define BUF_SIZE 1024

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

extern u8 g_mac[6];

typedef struct linked_can_frame
{
	u8 can_id;
	u8 dev_type;				//A B C
	u8 frame_number;			//21 22 23 24 25 ...
	u8 can_dlc;
	u8 data[8];
	
	struct linked_can_frame *next;
}T_LinkedCanFrame, *PT_LinkedCanFrame;

static PT_LinkedCanFrame g_PT_canFrameHead;

//插入到链表尾部
void AddCanFrame2LinkedList(PT_LinkedCanFrame ptCanFrame)
{
	PT_LinkedCanFrame ptTmp;

	if(!g_PT_canFrameHead)
	{
		g_PT_canFrameHead = ptCanFrame;
		ptCanFrame->next = NULL;
	}else
	{
		ptTmp = g_PT_canFrameHead;

		while(ptTmp->next)
		{
			ptTmp = (PT_LinkedCanFrame)ptTmp->next;
		}

		ptTmp->next = ptCanFrame;
		ptCanFrame->next = NULL;
	}
}

void Linkedlist_clear()
{
	PT_LinkedCanFrame ptTmp;
	
	while(g_PT_canFrameHead->next)
	{
		ptTmp = g_PT_canFrameHead->next;
		g_PT_canFrameHead->next = ptTmp->next;
		free(ptTmp);
		ptTmp = NULL;
	}
}

//返回: can_id的帧数目
int GetCanFramesById(u8 can_id, T_LinkedCanFrame *canFrames)
{
	int cnt = 0;
	int j = 0;
	PT_LinkedCanFrame ptTmp = g_PT_canFrameHead;
	while(ptTmp)
	{
		if(ptTmp->can_id == can_id)
		{
			canFrames[cnt].can_id = can_id;
			canFrames[cnt].dev_type = ptTmp->dev_type;
			canFrames[cnt].frame_number = ptTmp->frame_number;
			canFrames[cnt].can_dlc = ptTmp->can_dlc;
			for(j = 0; j < canFrames[cnt].can_dlc; j++)
			{
				canFrames[cnt].data[j] = ptTmp->data[j];
			}

			cnt++;
		}
		ptTmp = (PT_LinkedCanFrame)ptTmp->next;
	}
	
	return cnt;
}

/*
 *flag: 标志位，0:两路CAN都启动; 1:启动CAN1; 2:启动CAN2
*/
int can_init(struct CanWrapper *canWrapper, struct sockaddr_can *ptSockaddr_can)
{
	struct ifreq ifr;

	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;

	if(canWrapper->flag == 0)
	{
		DBG_PRINTF("CAN1 && CAN2 init...\n");
		
		if((canWrapper->socket_can1 = socket(family, type, proto)) < 0){
			perror("socket1");
			return -1;
		}
		if((canWrapper->socket_can2 = socket(family, type, proto)) < 0){
			perror("socket2");
			return -1;
		}
		ptSockaddr_can->can_family = family;
		strcpy(ifr.ifr_name, CAN1_DEV_NAME);
		ioctl(canWrapper->socket_can1, SIOCGIFINDEX, &ifr);
		ptSockaddr_can->can_ifindex = ifr.ifr_ifindex;
		//绑定
		if(bind(canWrapper->socket_can1, (struct sockaddr *)ptSockaddr_can, sizeof(struct sockaddr_can)) < 0)
		{
			perror("bind can1");
			return -1;
		}

		strcpy(ifr.ifr_name, CAN2_DEV_NAME);
		ioctl(canWrapper->socket_can2, SIOCGIFINDEX, &ifr);
		ptSockaddr_can->can_ifindex = ifr.ifr_ifindex;
		//绑定
		if(bind(canWrapper->socket_can2, (struct sockaddr *)ptSockaddr_can, sizeof(struct sockaddr_can)) < 0)
		{
			perror("bind can2");
			return -1;
		}

		return 0;
	}

	/*
	*	1路can初始化
	*/
	DBG_PRINTF("CAN%d init...\n", canWrapper->flag);
	if(canWrapper->flag == 1)
	{
		strcpy(ifr.ifr_name, CAN1_DEV_NAME);
	}
	else if(canWrapper->flag == 2)
	{
		strcpy(ifr.ifr_name, CAN2_DEV_NAME);
	}
	if((canWrapper->socket_can = socket(family, type, proto)) < 0){
		perror("socket");
		return -1;
	}
	ptSockaddr_can->can_family = family;
	ioctl(canWrapper->socket_can, SIOCGIFINDEX, &ifr);
	ptSockaddr_can->can_ifindex = ifr.ifr_ifindex;
	//绑定
	if(bind(canWrapper->socket_can, (struct sockaddr *)ptSockaddr_can, sizeof(struct sockaddr_can)) < 0)
	{
		perror("bind one-can");
		return -1;
	}
	
	return 0;
}

/*
	将struct can_frame转换成可读字符串，放入buf
	@buf_len: buf的长度
	@return 解析出来字符串的长度
*/
int parseCanFrame(struct can_frame *canFrame, char *buf, int buf_len)
{
	int len = 0;
	int i;
	//格式化id
	if(canFrame->can_id & CAN_EFF_FLAG){
		//扩展帧
		len = snprintf(buf, buf_len, "<0x%08x> ", canFrame->can_id & CAN_EFF_MASK);
	}
	else{
		//标准帧
		len = snprintf(buf, buf_len, "<0x%03x> ", canFrame->can_id & CAN_SFF_MASK);
	}

	//格式化DLC
	len += snprintf(buf+len, buf_len - len, "[%d] ", canFrame->can_dlc);

	//格式化数据
	for(i = 0; i < canFrame->can_dlc; i++)
	{
		len += snprintf(buf+len, buf_len - len, "%02x ", canFrame->data[i]);
	}

	buf[len++] = '\n';

	return len;
	
}


/* 
		   id         dlc      data
	buf : <0x123> [8]   11 22 33 44 55 66 00 04
*/
void getIdFrombuf(char *buf, char *id)
{
	for(; *buf != '<'; buf++);
	buf++;
	for(; *buf != '>'; buf++)
	{
		*id = *buf;
		id++;
	}
	*id = '\0';
}

int can_send(struct can_frame *frame, int socket_can)
{
	//DBG_PRINTF("can_send()...\n");
	int nBytes;
	nBytes = write(socket_can, frame, sizeof(struct can_frame));

	return nBytes;
}

int can_recv(struct can_frame *frame, int socket_can)
{
	int nBytes;
	//DBG_PRINTF("can_recv()...\n");
	nBytes = read(socket_can, frame, sizeof(struct can_frame));
	return nBytes;
}

u8 findDevTypeById(u8 can_id, int which)
{
	int i;
	if(which == 1){
		for(i = 1; i <= g_ARR_A2[0][0]; i++){
			if(can_id == g_ARR_A2[0][i]){
				return g_ARR_A2[1][i];
			}
		}
	}

	if(which == 2){
		for(i = 1; i <= g_ARR_A3[0][0]; i++){
			if(can_id == g_ARR_A3[0][i]){
				return g_ARR_A3[1][i];
			}
		}
	}

	return 0;
}

/*
	将一个can设备返回的多帧数据按要求格式化打印
	格式化通用数据
*/
void formatCommonData(T_LinkedCanFrame *canFrames, int length, char *buf, int buf_len)
{
	int i = 0, j = 0;
	int len;
	int cnt = 0;
	u8 complete = 0xff;	//默认是完整的
	if(length > 0)
	{
		len = snprintf(buf, buf_len, "%02x ", canFrames[0].can_id);
		len += snprintf(buf+len, buf_len - len, "%02x ", canFrames[0].dev_type);
		for(i = 0; i < length; i++)
		{
			len += snprintf(buf+len, buf_len - len, "%02x ", canFrames[i].frame_number);
			if(canFrames[i].frame_number == 0x00){
				cnt++;
			}
			for(j = 0; j < canFrames[i].can_dlc; j++)
			{
				len += snprintf(buf+len, buf_len - len, "%02x ", canFrames[i].data[j]);
			}
		}

		if(cnt > 0 && cnt != length){
			complete = 0x0f;	//不完整
		}
		if(cnt == length){
			complete = 0x00;	//未收到
		}
		len += snprintf(buf+len, buf_len - len, "%02x ", complete);

		buf[len++] = '\n';
	}
}


static int CommonData2file(char *data, char *file_name)
{

	FILE *fp;
	
	if((fp = fopen(file_name, "a+")) == NULL)
	{
		perror("write2file");
		return -1;
	}
	
	fputs(data, fp);

	fclose(fp);

	return 0;
}

/*
	fn: 波形数据类别, 一个can设备可能有多种波形数据；fn: 0x01~0x06
*/
static void waveData2file(u8 can_id, u8 fn)
{
	char file_name[BUF_SIZE];
	char data[BUF_SIZE];
	PT_LinkedCanFrame ptTmp;
	FILE *fp;
	int cnt = 1;
	int len = 0;
	int i;

	memset(file_name, 0, BUF_SIZE);
	
	GenerateMacCanidFnDateTimeFileName(file_name, BUF_SIZE,
		can_id, fn, g_mac, ".dat");
	
	if((fp = fopen(file_name, "a+")) == NULL)
	{
		perror("write2file");
		return;
	}

	ptTmp = g_PT_canFrameHead;
	while(ptTmp->next)
	{
		memset(data, 0, BUF_SIZE);
		len = 0;

		// 现在CAN设备有脏数据
		// 如果can设备工作正常，这个判断可以去掉
		if(ptTmp->data[0] == 0x00 && ptTmp->data[1] == 0x00){
			ptTmp = ptTmp->next;
			continue;
		}

		len = snprintf(data, BUF_SIZE - len, "%04x ", cnt++);
		len += snprintf(data + len, BUF_SIZE - len, "%02x ", can_id);
		len += snprintf(data + len, BUF_SIZE - len, "%02x ", ptTmp->dev_type);
		for(i = 0; i < ptTmp->can_dlc; i++)
		{
			len += snprintf(data+len, BUF_SIZE - len, "%02x ", ptTmp->data[i]);
		}
		data[len++] = '\n';
		
		fputs(data, fp);

		ptTmp = ptTmp->next;
	}
	
	fclose(fp);

	DBG_PRINTF("Completed, waveData2file()...%s\n", file_name);

}

void handleP2PAndBroadcastMode(int socket_can, int which, u8 *can_control)
{
	int ret,len;
	T_LinkedCanFrame *linkedCanFrame;
	T_LinkedCanFrame canFrames[128];
	struct can_frame frame;
	struct can_frame frame_recv;
	unsigned int head;
	u8 dev_type;
	u8 can_id;
	u8 mode;

	int i;
	int high,low = 0;
	int isWaveFinished = 0;

	u8 cmd_type;
	u8 poll_time;

	char buf[1024];
	char file_name[128] = {0};

	struct timeval time;
	fd_set fd_read;

	//can设备列表
	u8 (*dev_list)[A3_LEN];

	//波形数据长度
	static int waveDataLen = 0;
	//波形类型
	static u8 waveDataType;

	mode = can_control[0];
	can_id = can_control[1];
	cmd_type = can_control[2];
	poll_time = can_control[3];

	/*
		每个can_id对应一个dev_type，对应表存储在全局二维数组中；
		如果是广播模式，dev_type就填 0x1F;
	*/
	
	if(mode == MODE_P2P){	//p2p mode，查找dev_type
		dev_type = findDevTypeById(can_id, which);
	}else if(mode == MODE_BROADCAST){	//广播模式，dev_type为0x1F
		dev_type = 0x1F;
	}
	
	// 00 dev_type can_id 20
	head = dev_type << 16 | can_id << 8 | 0x20;
	frame.can_id = CAN_EFF_FLAG | head;
    frame.data[0] = cmd_type;
    frame.data[1] = 0x00;
    frame.data[2] = 0x00;
    frame.data[3] = 0x00;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00;
    frame.data[6] = 0x00;
    frame.data[7] = 0x00;
    frame.can_dlc = 8;
	ret = can_send(&frame, socket_can);
	if(ret < 0){
		DBG_PRINTF("can_send error\n");
		return;
	}
	parseCanFrame(&frame, buf, BUF_SIZE);
	DBG_PRINTF("data send to can-bus: %s\n", buf);
	
	if(cmd_type == 0x0f)	//读取波形数据
	{
		DBG_PRINTF("Reading wave data, waiting...\n");
	}
	if(cmd_type == 0x02)	//采集波形数据
	{
		DBG_PRINTF("Collecting wave data, waiting...\n");
	}

	// 等待can设备响应数据
	while(1){
		FD_ZERO(&fd_read);
		FD_SET(socket_can, &fd_read);

		// 3s，等不到数据就返回
		time.tv_sec = 3;
    	time.tv_usec = 0;

		//如果是读取波形数据，等待时间延长为60s
		if(cmd_type == 0x02 && !isWaveFinished) {
			time.tv_sec = 60;
    		time.tv_usec = 0;
		}
		
		ret = select(socket_can+1, &fd_read, NULL, NULL, &time);
		if(ret == 0){			//超时
			DBG_PRINTF("Completed...\n");
			
			switch(mode)
			{
			case MODE_P2P:		//p2p mode
				if(cmd_type == 0x00){			//呼叫通用数据
					len = GetCanFramesById(can_id, canFrames);
					memset(buf, 0, BUF_SIZE);
					// 根据canFrames里面的frameNumber字段进行排序
					// TODO...
					
					formatCommonData(canFrames, len, buf, BUF_SIZE);
					DBG_PRINTF(buf);

					memset(file_name, 0, 128);
					GenerateMacDateTimeFileName(file_name, 128, g_mac, ".dat");
					
					DBG_PRINTF("writeCANData2File()...: %s\n", file_name);
					
					//将格式化的数据写入文件
					CommonData2file(buf, file_name);
					
				}else if(cmd_type == 0x02){		//采集波形数据	
					
					len = GetCanFramesById(can_id, canFrames);
					//取出波形类别，和波形数据长度
					waveDataType= canFrames[len - 1].data[0];
					
					high = canFrames[len - 1].data[3] & 0xff;
					low = canFrames[len - 1].data[2] & 0xff;
					waveDataLen = low | (high << 8);

					DBG_PRINTF("waveDataType: %02x, waveDataLen: %d\n", waveDataType, waveDataLen);
					
				}else if(cmd_type == 0x0f){		//读取波形数据
					//认为采集波形数据只支持P2P模式，所以链表中的所有数据来自同一个设备
					//所以，就直接对全局链表进行操作
					waveData2file(can_id, waveDataType);
				}
				break;
			case MODE_BROADCAST:		//broadcast mode
				//广播只能呼叫通用数据
				if(cmd_type == 0x00)
				{
					// 1. 遍历设备列表中取出can_id
					// 2. 根据can_id，从链表中取出对应的can_frame
					// 3. 对取出的can_frame进行format
					if(which == 1){
						dev_list = g_ARR_A2;
					}else if(which == 2){
						dev_list = g_ARR_A3;
					}else{
						DBG_PRINTF("unkown which can\n");
						continue;
					}

					memset(file_name, 0, 128);
					GenerateMacDateTimeFileName(file_name, 128, g_mac, ".dat");
					DBG_PRINTF("writeCANData2File()...: %s\n", file_name);
					for(i = 1; i <= dev_list[0][0]; i++){
						len = GetCanFramesById(dev_list[0][i], canFrames);
						memset(buf, 0, BUF_SIZE);
						// 根据canFrames里面的frameNumber字段进行排序
						// TODO...

						formatCommonData(canFrames, len, buf, BUF_SIZE);
						DBG_PRINTF(buf);
						
						//将格式化的数据写入文件
						CommonData2file(buf, file_name);
					}
					
				}
				break;
			}
			
			//清空链表
			Linkedlist_clear();
			break;
		}else if(ret > 0){		//有数据到来
		
			memset(buf, 0, BUF_SIZE);
			ret = can_recv(&frame_recv, socket_can);
			if(ret < 0){
				DBG_PRINTF("can_recv error\n");
				continue;
			}

			//构造数据，存入链表
			linkedCanFrame = (T_LinkedCanFrame *)malloc(sizeof(T_LinkedCanFrame));
			linkedCanFrame->can_id = (frame_recv.can_id >> 8) & 0xff;
			linkedCanFrame->dev_type = (frame_recv.can_id >> 16) & 0xff;
			linkedCanFrame->frame_number = (frame_recv.can_id) & 0xff;
			linkedCanFrame->can_dlc = frame_recv.can_dlc;
			memcpy(linkedCanFrame->data, frame_recv.data, 8);
			AddCanFrame2LinkedList(linkedCanFrame);
			
			parseCanFrame(&frame_recv, buf, BUF_SIZE);
			//DBG_PRINTF("data from can-bus: %s\n", buf);

			//收到了采集波形数据响应帧，即采集完毕
			if(cmd_type == 0x02 && frame_recv.data[0] == 0x02) isWaveFinished = 1;
				
		}
	}
}

