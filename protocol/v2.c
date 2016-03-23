#include <stdio.h>
#include <string.h>
#include <time.h>

#include <proto_manager.h>
#include <v2.h>
#include <time.h>

#include <config.h>

static int V2_MakeFrame(int send_seq, int ack_seq, int frame_type, 
	char *data, int data_len, char *result);
static int V2_GetFrameType(char *frame, int frame_len);
static int v2_GetFrameSendSeq(char *frame, int frame_len);
static int v2_GetFrameAckSeq(char *frame, int frame_len);
static int V2_ProtoInit();


static T_ProtoOpr g_tV2ProtoOpr = {
	.name = "V2",
	.ProtoInit		 = V2_ProtoInit,
	.MakeFrame 		 = V2_MakeFrame,
	.GetFrameType 	 = V2_GetFrameType,
	.GetFrameSendSeq = v2_GetFrameSendSeq,
	.GetFrameAckSeq  = v2_GetFrameAckSeq,
};

static int V2_ProtoInit()
{
	return 0;
}
/*
	@send_seq �������
	@ack_seq  ȷ�����
	@frame_type ֡����
	@data  ֡����
	@data_len ����֡�ĳ���
	@return ���������֡���ܳ���
*/
static int V2_MakeFrame(int send_seq, int ack_seq, int frame_type, 
	char *data, int data_len, char *result)
{
	int frame_len;
	time_t timep;
	int i,j = 0;

	if(data_len< 10)
	{
		printf("data length is illegal..\n");
		return -1;
	}
	
	//�汾��
	result[j++] = PROTO_VERSION;

	//֡���ݳ���
	if(frame_type != FRAME_TYPE_TCD)
	{
		//�����������֡16���ֽ�
		result[j++] = 0x10;
		result[j++] = 0x00;
	}else
	{
		//���������֡��N+10���ֽ�
		frame_len = data_len + 10;
		if(frame_len < 0xff)
		{
			result[j++] = frame_len;
			result[j++] = 0x00;
		}else
		{
			result[j++] = 0xff;
			result[j++] = frame_len - 0xff;
		}
	}

	//�������
	result[j++] = send_seq;

	//ȷ�����
	result[j++] = ack_seq;

	//֡����
	result[j++] = frame_type;

	/*֡������*/
	//�ͻ�������
	result[j++] = 0x10;

	//Ԥ��
	result[j++] = 0x00;

	//ʱ��
	time(&timep);
	char *result_time = (char *)&timep;
	for(i = 0; i < 4; i++)
	{
		result[j + i] = result_time[i];
	}
	j += i;

	//����������
	for(i = 0; i < data_len; i++)
	{
		result[j + i] = data[i];
	}
	j += data_len;

	//������
	if(frame_type == FRAME_TYPE_TCD)
	{
		result[j++] = 0x43;
		result[j++] = 0x52;
		result[j++] = 0x53;
		result[j++] = 0x43;					
	}

	DBG_PRINTF("V2_MakeFrame frame length = %d\n", j);
	
	return j;
	
}

static int V2_GetFrameType(char *frame, int frame_len)
{
	if(frame_len < 22)
	{
		printf("frame is illegal, length: %d\n", sizeof(frame));
		return -1;
	}

	return frame[5];
}

static int v2_GetFrameSendSeq(char *frame, int frame_len)
{
	if(frame_len < 22)
	{
		printf("frame is illegal, length: %d\n", sizeof(frame));
		return -1;
	}

	return frame[3];
}


static int v2_GetFrameAckSeq(char *frame, int frame_len)
{
	if(frame_len < 22)
	{
		printf("frame is illegal, length: %d\n", sizeof(frame));
		return -1;
	}

	return frame[4];
}


int V2Init(void)
{
	return RegisterProtoOpr(&g_tV2ProtoOpr);
}



























