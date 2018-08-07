#define WIN32_LEAN_AND_MEAN
/* 
	This is a utility specially for string
	processing
	
 	Author: Biniam Fisseha Demissie
 	Last Modified: 2007
 */
#include <windows.h>
#include "utils.h" 

CRITICAL_SECTION cs, csec;
char MyFile[MAX_PATH+64];
HANDLE hTempFile;

void init_cs(void)
{
	InitializeCriticalSection(&cs);
	InitializeCriticalSection(&csec);
}

static DWORD _randseed;

void _rand_init(void)
{	
	_randseed = GetTickCount();
}

int _rand(void)
{		
	_randseed = (_randseed * 214013L) + 2531011L;
	return (int)(_randseed >> 16) & (int)0x7FFF;	
}

int to_lower(int c)
{
	if (c >= 'A' && c <= 'Z') return c+('a'-'A');
	return c;
}

int to_upper(int c)
{
	if (c >= 'a' && c <= 'z') return (c-'a')+'A';
	return c;
}

void lower_case(char *str)
{
	char *p;
	for (p=str; *p; p++)
		*p = to_lower(*p); 
}

void upper_case(char *str)
{
	char *p;
	for (p=str; *p; p++)
		*p = to_upper(*p);
}

char *xstrstr(register char *str, const char *str2)
{
	register int i;
	for (; *str; *str++) {
		for (i=0; to_lower(*str) == to_lower(*str2); *str++, i++)
			if (*++str2 == 0) return (char *)(str - i);		
		str2 -= i;
	}
	return NULL;
}

int instr(int start, char *str, const char *str2)
{
	int i=start;
	char *ptr;
	if (!*str || !*str2 || start > lstrlen(str) || start < 1) return 0;
	while (--i) str++;
	ptr = xstrstr(str, str2);
	if (ptr != NULL) 
		return (int)(ptr - str + start);
	return 0;
}

void strleft(char *str, int len)
{
	if (!*str || len <= 1 || len > lstrlen(str)) return;
	memcpy(str+len, "\0", 1);	
}