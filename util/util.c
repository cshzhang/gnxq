#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <config.h>
#include <time.h>
#include <sys/time.h>
#include <net/if.h>
#include <sys/ioctl.h> 


/*u8 g_ARR_A0[2];		//心跳间隔，初始值
u8 g_ARR_A1[64];	//心跳帧内容初始值
u8 g_ARR_A2[2][64];	//CAN1，设备列表
u8 g_ARR_A3[2][64];	//CAN2，设备列表
u8 g_ARR_A4[4];		//CAN1, 设备控制参数初始值
u8 g_ARR_A5[4];		//CAN2，设备控制参数初始值*/

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

//返回字节数量
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
	格式化系统时间: yyyyMMddHHmmss
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

//返回系统UTC时间，单位S
long getSystemTimeInSecs()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	return tv.tv_sec;
}

/*
	外部需要为buf分配内存空间
	格式: yyMMddHHmmSS
*/
void getDateTimeInString(char *buf, int buf_size)
{
	long timeL = getSystemTimeInSecs();
	formatDateTime(timeL, buf, buf_size);
}

/**
*	获取站机MAC地址
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
	根据传入的mac地址和后缀名，生成文件名，绝对路径
	// 1.构造文件名: "aabbccddeeff20140322093010.xxx"
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
	根据传入的mac地址和后缀名，生成文件名，绝对路径
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
	char *buf[8];	//目前有8行数据，指针数组，每一个数组元素都指向一行字符串
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

