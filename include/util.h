#ifndef _UTIL_H
#define _UTIL_H

void str_trim(char *pStr);
int char2byte(char ch);
void str_slice(char *str, int begin, int end, char result[]);
int hexStringToBytes(char *hexString, u8 res[512]);
char *strlwr(char *s);
int getMacAddr(u8 res[6], char *dev_name) ;
long getSystemTimeInSecs();
int formatDateTime(long timeL, char *buf, int buf_size);
void getDateTimeInString(char *buf, int buf_size);
int GenerateMacDateTimeFileName(char *file_name, int buf_size, u8 mac[6], char *suffix);
int GenerateMacCanidFnDateTimeFileName(char *file_name, int buf_size, 
				u8 can_id, u8 fn, u8 mac[6], char *suffix);


#endif

