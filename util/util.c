#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <config.h>
#include <time.h>
#include <sys/time.h>
#include <net/if.h>
#include <sys/ioctl.h> 


/*u8 g_ARR_A0[2];		//�����������ʼֵ
u8 g_ARR_A1[64];	//����֡���ݳ�ʼֵ
u8 g_ARR_A2[2][64];	//CAN1���豸�б�
u8 g_ARR_A3[2][64];	//CAN2���豸�б�
u8 g_ARR_A4[4];		//CAN1, �豸���Ʋ�����ʼֵ
u8 g_ARR_A5[4];		//CAN2���豸���Ʋ�����ʼֵ*/

void str_trim(char *pStr)
{
	char *pTmp = pStr;
	while(*pStr != '\0')
	{
		if(*pStr != ' ') *pTmp++ = *pStr;
		pStr++;
	}
	*pTmp = '\0';
}

static int charIndexOfHexStr(char ch)
{
	char *hexStr = "0123456789ABCDEF";
	int i;
	for(i = 0; i < 16; i++)
	{
		if(hexStr[i] == ch) return i;
	}
	return -1;
}

int char2byte(char ch)
{
	if(ch >= 97) ch = ch -32;
	return charIndexOfHexStr(ch);
}

void str_slice(char *str, int begin, int end, char result[])
{
	int pos, i = 0;

	for(pos = begin; pos < end; pos++)
	{
		result[i++] = str[pos];
	}
	result[i] = '\0';
}

//�����ֽ�����
int hexStringToBytes(char *hexString, u8 res[512])
{
	int len = strlen(hexString) / 2;
	int i;
	int pos = 0;
	if(strlen(hexString) % 2 != 0) return -1;

	for(i = 0; i < len; i++)
	{
		pos = i * 2;
		res[i] = (u8)(char2byte(hexString[pos]) << 4 | char2byte(hexString[pos+1]));
	}
	return len;
}

/*
	��ʽ��ϵͳʱ��: yyyyMMddHHmmss
*/
int formatDateTime(long timeL, char *buf, int buf_size)
{
	int year, mon, day, hour, min, sec;
	int len = 0;
	struct tm *t_tm;
	
	t_tm = localtime(&timeL);
	year = t_tm->tm_year+1900;
	mon = t_tm->tm_mon+1;
	day = t_tm->tm_mday;
	hour = t_tm->tm_hour;
	min = t_tm->tm_min;
	sec = t_tm->tm_sec;

	len += snprintf(buf+len, buf_size-len, "%04d", year);
	len += snprintf(buf+len, buf_size-len, "%02d", mon);
	len += snprintf(buf+len, buf_size-len, "%02d", day);
	len += snprintf(buf+len, buf_size-len, "%02d", hour);
	len += snprintf(buf+len, buf_size-len, "%02d", min);
	len += snprintf(buf+len, buf_size-len, "%02d", sec);
	buf[len++] = '\0';

	return len;
}

//����ϵͳUTCʱ�䣬��λS
long getSystemTimeInSecs()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return tv.tv_sec;
}

/*
	�ⲿ��ҪΪbuf�����ڴ�ռ�
	��ʽ: yyMMddHHmmSS
*/
void getDateTimeInString(char *buf, int buf_size)
{
	long timeL = getSystemTimeInSecs();
	formatDateTime(timeL, buf, buf_size);
}

/**
*	��ȡվ��MAC��ַ
*/
int getMacAddr(u8 res[6], char *dev_name) 
{ 

    struct   ifreq   ifreq; 
    int   sock; 
    if((sock=socket(AF_INET,SOCK_STREAM,0)) <0) 
    { 
        perror( "socket "); 
        return -1;
    } 
    strcpy(ifreq.ifr_name, dev_name);
    if(ioctl(sock,SIOCGIFHWADDR,&ifreq) <0) 
    { 
        perror( "ioctl "); 
        return -1;
    }

	int i = 0;
	for(i = 0; i < 6; i++)
	{
		res[i] = ifreq.ifr_hwaddr.sa_data[i];
	}

	close(sock);

	return 0;
}

/*
	���ݴ����mac��ַ�ͺ�׺���������ļ���������·��
	// 1.�����ļ���: "aabbccddeeff20140322093010.xxx"
*/
int GenerateMacDateTimeFileName(char *file_name, int buf_size, 
													u8 mac[6], char *suffix)
{
	int len = 0;
	int i;
	char buf[128] = {0};
	
	if(strcmp(".log", suffix) == 0){
		len += snprintf(file_name + len, buf_size - len, "%s", CANLOG_PATH);
	}else if(strcmp(".dat", suffix) == 0){
		len += snprintf(file_name + len, buf_size - len, "%s", CANDATA_PATH);
	}else{
		DBG_PRINTF("unkown filename suffix\n");
		return -1;
	}

	for(i = 0; i < 6; i++){
		len += snprintf(file_name + len, buf_size - len, "%02x", mac[i]);
	}
	getDateTimeInString(buf, 128);
	len += snprintf(file_name + len, buf_size - len, "%s", buf);

	len += snprintf(file_name + len, buf_size - len, "%s", suffix);

	file_name[len++] = '\0';

	return 0;
}

/*
	���ݴ����mac��ַ�ͺ�׺���������ļ���������·��
*/
int GenerateMacCanidFnDateTimeFileName(char *file_name, int buf_size, 
				u8 can_id, u8 fn, u8 mac[6], char *suffix)
{
	int len = 0;
	int i;
	char buf[128] = {0};
	
	if(strcmp(".log", suffix) == 0){
		len += snprintf(file_name + len, buf_size - len, "%s", CANLOG_PATH);
	}else if(strcmp(".dat", suffix) == 0){
		len += snprintf(file_name + len, buf_size - len, "%s", CANDATA_PATH);
	}else{
		DBG_PRINTF("unkown filename suffix\n");
		return -1;
	}

	for(i = 0; i < 6; i++){
		len += snprintf(file_name + len, buf_size - len, "%02x", mac[i]);
	}
	
	len += snprintf(file_name + len, buf_size - len, "%02x", can_id);
	len += snprintf(file_name + len, buf_size - len, "%02x", fn);
	
	getDateTimeInString(buf, 128);
	len += snprintf(file_name + len, buf_size - len, "%s", buf);

	len += snprintf(file_name + len, buf_size - len, "%s", suffix);

	file_name[len++] = '\0';

	return 0;
}


char *strlwr(char *s)
{
	char *str;
	str = s;
	while(*str != '\0')
	{
		if(*str >= 'A' && *str <= 'Z')
		{
			*str += 'a' - 'A';
		}
		str++;
	}

	return s;
}


#if 0
int main()
{
	char data[1024];
	char *buf[8];	//Ŀǰ��8�����ݣ�ָ�����飬ÿһ������Ԫ�ض�ָ��һ���ַ���
	FILE *fp;
	int index = 0;
	int line = 0;
	int i;

	//g_ARR_A0
	if((fp = fopen("/home/work/1.cfg", "a+")) == NULL)
	{
		perror("write2file");
		return -1;
	}

	while(fgets(data, 1024, fp) != NULL)
	{
		for(i = 0; i < strlen(data); i++)
		{
			if(data[i] == '\r' || data[i] == '\n' || data[i] == '#')
			{
				data[i] = '\0';
				break;
			}
		}
		printf("strlen[data]: %d, fgets: %s\n", strlen(data), data);
		printf("index: %d, line: %d\n", index, line);
		if(line > 0)
		{
			buf[index] = malloc(strlen(data) + 1);
			strcpy(buf[index], data);
			str_trim(buf[index]);
			printf("buf[%d]: %s\n", index, buf[index]);

			line--;
			index++;
		}
		if(strcmp(data, "A0:") == 0){
			line = 1;
		}
		if(strcmp(data, "A1:") == 0){
			line = 1;
		}
		if(strcmp(data, "A2:") == 0){
			line = 2;
		}
		if(strcmp(data, "A3:") == 0){
			line = 2;
		}
		if(strcmp(data, "A4:") == 0){
			line = 1;
		}
		if(strcmp(data, "A5:") == 0){
			line = 1;
		}
	}

	fclose(fp);

	printf("result: \n");
	for(i = 0; i < 8; i++)
	{
		printf("%s\n", buf[i]);
	}

	
	//�����������
	hexStringToBytes(buf[0], g_ARR_A0);


	//�����������飬����+��ͨ�ַ�(ID+��վ��)
	char res[64];
	str_slice(buf[1], 0, 6, res);
	//AS,SS��ֵ
	hexStringToBytes(res, g_ARR_A1);
	//ID,��վ��
	str_slice(buf[1], 6, strlen(buf[1]), res);
	i = 0;
	while(res[i] != '\0')
	{
		g_ARR_A1[i + 3] = res[i];
		i++;
	}

	//��ά����
	hexStringToBytes(buf[2], g_ARR_A2[0]);
	hexStringToBytes(buf[3], g_ARR_A2[1]);
	//��ά����
	hexStringToBytes(buf[4], g_ARR_A3[0]);
	hexStringToBytes(buf[5], g_ARR_A3[1]);

	//��·can�Ŀ��Ʋ���
	hexStringToBytes(buf[6], g_ARR_A4);
	hexStringToBytes(buf[7], g_ARR_A5);

	/*u8 g_ARR_A0[2];		//�����������ʼֵ
	u8 g_ARR_A1[64];	//����֡���ݳ�ʼֵ
	u8 g_ARR_A2[2][64];	//CAN1���豸�б�
	u8 g_ARR_A3[2][64];	//CAN2���豸�б�
	u8 g_ARR_A4[4];		//CAN1, �豸���Ʋ�����ʼֵ
	u8 g_ARR_A5[4];		//CAN2���豸���Ʋ�����ʼֵ*/
	
	printf("g_ARR_A0: ");
	print_arr(g_ARR_A0, 2);
	printf("g_ARR_A1: ");
	print_arr(g_ARR_A1, 64);
	printf("g_ARR_A4: ");
	print_arr(g_ARR_A4, 4);
	printf("g_ARR_A5: ");
	print_arr(g_ARR_A5, 4);

	return 0;
}
#endif

