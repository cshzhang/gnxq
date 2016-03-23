#ifndef _CAN_H
#define _CAN_H

#define MODE_STOP 		0x00
#define MODE_P2P 		0x0F
#define MODE_POLL 		0xF0
#define MODE_BROADCAST 	0xFF

typedef struct CanWrapper{
	int socket_can;	//如果只开一路CAN，就用这个变量
	int socket_can1;
	int socket_can2;
	int flag;	//flag: 标志位，0:两路CAN都启动; 1:启动CAN1; 2:启动CAN2
}T_CanWrapper, *PT_CanWrapper;

/*
typedef struct MacStationInfo{
	int HBFreq;		//心跳的频率,2bytes,3~3600s
	
};
*/

int can_init(T_CanWrapper *canWrapper, struct sockaddr_can *ptSockaddr_can);
int parseCanFrame(struct can_frame *canFrame, char *buf, int buf_len);
int makeDataInFrame(char *FJ_data, char *SH_data, char *FXP_data, char *dataInFrame);
void getIdFrombuf(char *buf, char *id);
int canFrame2file(char *buf, char *filename);
int can_send(struct can_frame *frame, int socket_can);
int can_recv(struct can_frame *frame, int socket_can);
u8 findDevTypeById(u8 can_id, int which);
void handleP2PAndBroadcastMode(int socket_can, int which, u8 *can_control);

#endif

