#ifndef _UTILS_H_
#define _UTILS_H_

#define is_alpha(c) ((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z')
#define is_numer(c) ((c) >= '0' && (c) <= '9')
#define is_alnum(c) ((is_alpha(c)) || (is_numer(c)))

extern CRITICAL_SECTION cs, csec;
char MyFile[MAX_PATH+64];
extern HANDLE hTempFile;

void init_cs(void);
void _rand_init(void);
int _rand(void);
int my_atoi(char *s);
void lower_case(char *str);
void upper_case(char *str);
char *xstrstr(char *str, const char *str2);
int instr(int start, char *in, const char *srch);
void strleft(char *str, int len);

#endif