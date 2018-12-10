#define WIN32_LEAN_AND_MEAN
/* 
	This is an IRC Bot remove controller for uTorrent.
	In order to use it you need to specifiy the IRC server
	address and the channel you want to join. This assumes
	uTorrent webserver is running on port 4096
	
 	Author: Biniam Fisseha Demissie
 	Last Modified: 2009
 */
#include <windows.h>
#include <winsock2.h>
#include "utils.h"

#pragma comment(lib, "ws2_32.lib")

struct ircbot_t {
	char server[80];
	char channel[64];
	char key[64];
	unsigned short port;
};

struct ircbot_sync_t {
	char sender[80];
	char nick[80];
	char argument[1024];
	unsigned int sock;
	int cmd;
};


#define UT_PORT		4046
#define IRC_PORT	6668
/* torrent status */
#define TORRENT_QUEUED				200
#define TORRENT_WORKING				201
#define TORRENT_PAUSED				233
#define TORRENT_STOPPED				136
#define TORRENT_WORKING_FORCED		137
#define TORRENT_CHECKING			130
#define TORRENT_FORCED_PAUSE		169
#define TORRENT_ERROR				152

static struct torrent_tab {
	char *hash[256];
	char *name[256];
	char *stat[256];
	char *done[256];
	char *size[256];
	char *order[256];
	char *color[256];
	char *eta[256];	
	int count;
} t_tab;

#define CMD_PRIVMSG		0x0001
#define CMD_PING		0x0002
#define CMD_NAMES		0x0003

#define PRIVMSG_START		0x0001
#define PRIVMSG_STOP		0x0002
#define PRIVMSG_PAUSE		0x0003
#define PRIVMSG_RECHECK		0x0004
#define PRIVMSG_FORCE		0x0005
#define PRIVMSG_LIST		0x0006
#define PRIVMSG_ADD			0x0007
#define PRIVMSG_STOP_UPDATE 0x0008
#define PRIVMSG_UPDATE		0x0009

int update_running = 0;
int list_running = 0;

static unsigned long resolve(const char *server)
{
	unsigned long ip=INADDR_NONE;
	struct hostent *h = gethostbyname(server);
	if (h != NULL)
		ip = *(unsigned long *)h->h_addr_list[0];
	return (ip == INADDR_NONE) ? 0 : ip;
}

// is socket blocking
static int is_readable(unsigned int sock, int timeout)
{	
	struct timeval tv;
	struct fd_set fds;		

	FD_ZERO(&fds); FD_SET(sock, &fds);
	tv.tv_sec = timeout; tv.tv_usec = 0;
	return ((select(0, &fds, NULL, NULL, &tv) <= 0) ? 0 : 1);
}

static int xfree_alloc()
{
	int i=0;

	EnterCriticalSection(&cs);
	while (t_tab.count--) {
		HeapFree(GetProcessHeap(), 0, t_tab.hash[i]);
		HeapFree(GetProcessHeap(), 0, t_tab.name[i]);
		HeapFree(GetProcessHeap(), 0, t_tab.size[i]);
		HeapFree(GetProcessHeap(), 0, t_tab.stat[i]);
		HeapFree(GetProcessHeap(), 0, t_tab.done[i]);
		HeapFree(GetProcessHeap(), 0, t_tab.order[i]);
		i++;		
	}
	t_tab.count=0;
	LeaveCriticalSection(&cs);
	return 0;
}

static int socket_func(char *szMsg) {
	struct sockaddr_in addr;
	unsigned int sock, k, j;

	char szHeader =	"Host: localhost:4046\r\n"
					"User-Agent: Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20A\r\n"
					"Accept: */*\r\n"
					"Accept-Language: en-us,en;q=0.5\r\n"		
					"Keep-Alive: 300\r\n"
					"Connection: keep-alive\r\n"		
					"Authorization: Basic YmluaTpiaW5p\r\n\r\n";

	lstrcat(szMsg,szHeader);

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) return 1;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(UT_PORT);
	addr.sin_addr.s_addr = resolve("localhost");

	if (addr.sin_addr.s_addr != 0) {
		k = connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));	
		if (k == SOCKET_ERROR) return 1;
	}
	for (j=0;;) {
		k = send(sock, szMsg+j, lstrlen(szMsg)-j, 0);
		if (k <= 0) break;
		j += k;
	}

	closesocket(sock);

	return 0;
}

static int perform_add_url(char *url)
{
	char szFormat[] = 

		"GET /gui/?action=add-url&s=%s HTTP/1.1\r\n";
	
	char szMsg[2048]="", s[512], s2[768]="", *b;
	
	b = xstrstr(url, "http://");
	if (b != NULL) b += 7;
	lstrcpy(s, "http%3A//"); lstrcat(s, (b == NULL) ? url : b);
	for (k=0, j=0; s[k] != 0;) {
		if (s[k] == '?') {
			lstrcpy(s2+j, "%3F");
			k++; j += 3;
			continue;
		} 
		else if(s[k] == '=') {
			lstrcpy(s2+j, "%3D");
			k++; j += 3;
			continue;
		}
		else if(s[k] == ' ') {
			lstrcpy(s2+j, "%20");
			k++; j += 3;
			continue;
		}
		else if(s[k] == '&') {
			lstrcpy(s2+j, "%26");
			k++; j += 3;
			continue;
		}
		s2[j++] = s[k++];
	}
	s2[j] = 0;
	// MessageBox(0, s2, s, 0);
	wsprintf(szMsg, szFormat, s2);

	return socket_func(szMsg);
}

static int perform_recheck(const char *hash)
{
	char szFormat[] = 

		"GET /gui/?action=recheck&hash=%s HTTP/1.1\r\n";
	
	char szMsg[2048]="";

	wsprintf(szMsg, szFormat, hash);
	
	return socket_func(szMsg);
}

static int perform_force(const char *hash)
{
	char szFormat[] = 

		"GET /gui/?action=forcestart&hash=%s HTTP/1.1\r\n";
	
	char szMsg[2048]="";
			
	wsprintf(szMsg, szFormat, hash);
	
	return socket_func(szMsg);
}

static int perform_start(const char *hash)
{
	char szFormat[] = 

		"GET /gui/?action=start&hash=%s HTTP/1.1\r\n";
	
	char szMsg[2048]="";

	wsprintf(szMsg, szFormat, hash);
	
	return socket_func(szMsg);
}

static int perform_stop(const char *hash)
{
	char szFormat[] = 

		"GET /gui/?action=stop&hash=%s HTTP/1.1\r\n";
	
	char szMsg[2048]="";

	wsprintf(szMsg, szFormat, hash);
	
	return socket_func(szMsg);
}

static int perform_pause(const char *hash)
{
	char szFormat[] = 

		"GET /gui/?action=pause&hash=%s HTTP/1.1\r\n";

	
	char szMsg[8192]="";

	wsprintf(szMsg, szFormat, hash);

	return socket_func(szMsg);
}

static int proc_msg(char *buff)
{
	
	char *k=xstrstr(buff, "\"torrents\"");
	char buf2[2048];
	int i,j;
	unsigned long size, speed, dwSec;
	
	EnterCriticalSection(&csec);
	k += 13;
loop:
	j = 0;
	while(*k++ != '[');
	memset(&buf2, 0, sizeof(buf2));
	if (*k == '"') k++;
	
	for (i=0; *k != '"';) buf2[i++] = *k++;
	t_tab.hash[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, i+1);
	lstrcpy(t_tab.hash[t_tab.count], buf2);

	k += 2;
	memset(&buf2, 0, sizeof(buf2));
	for (i=0; *k != ',';) i = i * 10 + *k++ - '0';
	t_tab.color[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, 8);
	switch (i) {		
		case TORRENT_QUEUED:
			lstrcpy(buf2, "Queued");
			lstrcpy(t_tab.color[t_tab.count], "8,14");
			break;
		case TORRENT_WORKING:
			lstrcpy(buf2, "Downloading");
			lstrcpy(t_tab.color[t_tab.count], "0,12");
			break;
		case TORRENT_PAUSED:
			lstrcpy(buf2, "Pausing");
			lstrcpy(t_tab.color[t_tab.count], "1,15");
			break;
		case TORRENT_STOPPED:
			lstrcpy(buf2, "Stopped");
			lstrcpy(t_tab.color[t_tab.count], "0,1");
			break;
		case TORRENT_WORKING_FORCED:
			lstrcpy(buf2, "Downloading[F]");
			lstrcpy(t_tab.color[t_tab.count], "0,12");
			break;
		case TORRENT_CHECKING:
			lstrcpy(buf2, "Checking");
			lstrcpy(t_tab.color[t_tab.count], "0,1");
			break;
		case TORRENT_FORCED_PAUSE:
			lstrcpy(buf2, "Pausing[F]");
			lstrcpy(t_tab.color[t_tab.count], "1,15");
			break;
		case TORRENT_ERROR:
			lstrcpy(buf2, "Error[F]");
			lstrcpy(t_tab.color[t_tab.count], "0,4");
			break;
	}
	t_tab.stat[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, lstrlen(buf2)+4);
	lstrcpy(t_tab.stat[t_tab.count], buf2);

	k += 2;
	memset(&buf2, 0, sizeof(buf2));
	for (i=0; *k != '"';) buf2[i++] = *k++;
	t_tab.name[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, i+1);
	lstrcpy(t_tab.name[t_tab.count], buf2);

	k += 2;
	memset(&buf2, 0, sizeof(buf2));
	for (size=0; *k != ',';) size = size * 10 + *k++ - '0';
	size /= (1024 * 1024);
	if (size > 1000)
		wsprintf(buf2, "%i.%i", (int)(size / 1024), (int)((int)size % 1024));
	else
		wsprintf(buf2, "%i.%i", (int)size, (int)((int)size % 1024)/100);
	t_tab.size[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, i+1);
	lstrcpy(t_tab.size[t_tab.count], buf2); lstrcat(t_tab.size[t_tab.count], size > 1000 ? "GB" : "MB");
	
	k += 1;
	memset(&buf2, 0, sizeof(buf2));
	for (i=0; *k != ',';) 
		i = i * 10 + *k++ - '0';
	wsprintf(buf2, "%i.%i%", i/10, i % 10);
	t_tab.done[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, i+1);
	lstrcpy(t_tab.done[t_tab.count], buf2);

	for (i=0; i<5;) if (*k++ == ',') i++;	
	for (speed=0; *k != ',';) 
		speed = speed * 10 + *k++ - '0';
	
	for (i=1; i<=8;) if (*k++ == ',') i++;
	memset(&buf2, 0, sizeof(buf2));
	if (*k == '-') {j=1; k++;}
	for (i=0; *k != ',';) 
		i = i * 10 + *k++ - '0';
	
	if (j) i *= -1;
	if (i != -1)
		wsprintf(buf2, "-%i-", i);
	else {
		wsprintf(buf2, "-%s-", "*");
		if (xstrstr(t_tab.stat[t_tab.count], "downloading")) {
			lstrcpy(t_tab.stat[t_tab.count], "Seeding");
			lstrcpy(t_tab.color[t_tab.count], "1,9");
		}		
		else if (xstrstr(t_tab.stat[t_tab.count], "stopped")) {
			lstrcpy(t_tab.stat[t_tab.count], "Finished"); // overflow: stopped(7) -> finished(8)
			lstrcpy(t_tab.color[t_tab.count], "0,14");
		}
		
	}
	t_tab.order[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, 8);
	lstrcpy(t_tab.order[t_tab.count], buf2);
	
	k += 1;
	for (size=0; *k != ']';) size = size * 10 + *k++ - '0';
	memset(&buf2, 0, sizeof(buf2));
	if (speed == 0) {
		if (xstrstr(t_tab.order[t_tab.count], "-*-"))
			lstrcpy(buf2, "0s");
		else
			lstrcpy(buf2, "æ");
	}
	else
		wsprintf(buf2, "%dh:%dm", ((size/speed)/60)/60, (size/speed)%60);
	
	t_tab.eta[t_tab.count] = (char *)HeapAlloc(GetProcessHeap(), 0, 16);	
	lstrcpy(t_tab.eta[t_tab.count], buf2);

	while (*k && *k++ != ']');
	if (!*k || *k == ']') {LeaveCriticalSection(&csec); return 0;}
	t_tab.count++;
	goto loop;

	// original implementation: dead code
	// LeaveCriticalSection(&csec);
	// return 0;
	
}

static int get_home_page()
{
	char szFormat[] = 
		"GET /gui/?list=1&cid=%d&getmsg=1 HTTP/1.1\r\n"
		"Host: localhost:4046\r\n"
		"User-Agent: Mozilla/5.0 (Windows; U; Windows NT 6.0; en-US; rv:1.8.1.20) Gecko/20081217 Firefox/2.0.0.20A\r\n"
		"Accept: */*\r\n"
		"Accept-Language: en-us,en;q=0.5\r\n"		
		"Keep-Alive: 300\r\n"
		"Connection: keep-alive\r\n"		
		"Authorization: Basic YmluaTpiaW5p\r\n\r\n";
	char szMsg[8192]="";
	struct sockaddr_in addr;
	unsigned int sock, k, j;

	wsprintf(szMsg, szFormat, GetTickCount());

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock <= 0) return 1;
			
	addr.sin_family = AF_INET;
	addr.sin_port = htons(UT_PORT);
	addr.sin_addr.s_addr = resolve("localhost");
	if (addr.sin_addr.s_addr != 0) {		
		k = connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));	
		if (k == SOCKET_ERROR) return 1;
	}
	if (k != 0) return 1;
	k = send(sock, szMsg, lstrlen(szMsg), 0);
	
	for (j=0; is_readable(sock, 1);) {
		k = recv(sock, szMsg+j, sizeof(szMsg)-j, 0);
		if (k == 0) break;
		j += k;
	}
	szMsg[j+1] = 0;		
	proc_msg(szMsg);
	closesocket(sock);
/*
	printf("#  -  Order     Name                              Size     Done     Stat\n");
	 
	for (k=0; k<=t_tab.count; k++)
		printf("#%i -  %s     %- 30.30s  %s     %s \t%s\n", k+1, t_tab.order[k], t_tab.name[k], t_tab.size[k], t_tab.done[k], t_tab.stat[k]);
*/
	return 0;
}

DWORD WINAPI worker_th(LPVOID pv)
{
	struct ircbot_sync_t *irc=(struct ircbot_sync_t *)pv;

	//ExitThread(0);
	return 0;
}

static int parse_cmd(char *str)
{	
	int code=0;
	if (instr(1, str, "PING") == 1)
		return CMD_PING;
	while (*str && (*str == ' ' || *str ==  '\t')) *str++;
	while (*str++ != ' ');
	if (xstrstr(str, "PRIVMSG") != NULL)
		return CMD_PRIVMSG;
	else {
		while (*str >= '0' && *str <= '9')
			code = code * 10 + *str++ - '0';
		if (code == 353)
			return CMD_NAMES;
	}
	
	return 0;
}

// connect to irc server (without proxy)
static int irc_connect(unsigned int sock, const char *irc_server, u_short irc_port)
{
	int k;
	struct sockaddr_in addr;
	
	/* try direct connection */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(irc_port);
	addr.sin_addr.s_addr = resolve(irc_server);
	if (addr.sin_addr.s_addr != 0)
		k = connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (k == 0) return 0;	
	
	return 1;
}

static int process_privmsg(char *response)
{
	if (xstrstr(response, "!start") != NULL)		/* !start # - followed by number (e.g #4) */
		return PRIVMSG_START;
	else if (xstrstr(response, "!stop") != NULL)		/*  !stop # */
		return PRIVMSG_STOP;
	else if (xstrstr(response, "!pause") != NULL)		/* !pause # */
		return PRIVMSG_PAUSE;
	else if (xstrstr(response, "!list") != NULL)			/* !list - no argv */
		return PRIVMSG_LIST;
	else if (xstrstr(response, "!recheck") != NULL)		/* !recheck # */
		return PRIVMSG_RECHECK;
	else if (xstrstr(response, "!force") != NULL)		/* !force # */
		return PRIVMSG_FORCE;
	else if (xstrstr(response, "!add") != NULL)			/* !add url */
		return PRIVMSG_ADD; 
	else if (xstrstr(response, "!update") != NULL)
		return PRIVMSG_UPDATE;
	else if (xstrstr(response, "!supdate") != NULL)
		return PRIVMSG_STOP_UPDATE;

	return 0;
}

static DWORD _stdcall ircbot_sync_th(LPVOID pv)
{
	struct ircbot_sync_t *irc=(struct ircbot_sync_t *)pv;

	switch (irc->cmd) {

			case PRIVMSG_START:
				{
					char *p=irc->argument, buf[1204]="";
					int i=0, k;

					while (*p >= '0' && *p <= '9') i = i * 10 + *p++ - '0';

					i--;
					if (i > t_tab.count || i < 0) {
						wsprintf(buf, "PRIVMSG %s :x0,4Invalid index!\n", irc->sender);
							goto err_start;
					}
					xfree_alloc();
					get_home_page();					
					perform_start(t_tab.hash[i]);
					Sleep(2000);
					xfree_alloc();
					get_home_page();					
					
					wsprintf(buf, "PRIVMSG %s :x0,12#%i  %s  %s  %s  %s  %s\n", irc->sender, i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.stat[i]);
err_start:
					for (k=0; buf[k] != ':'; k++);
					buf[k+1] = 3; 
					send(irc->sock, buf, lstrlen(buf), 0);
					break;
				}
			case PRIVMSG_STOP:
				{									
					char *p=irc->argument, buf[1204]="";
					int i=0, k;

					while (*p >= '0' && *p <= '9') i = i * 10 + *p++ - '0';

					i--;
					if (i > t_tab.count || i < 0) {
						wsprintf(buf, "PRIVMSG %s :x0,4Invalid index!\n", irc->sender);
							goto err_stop;
					}
					xfree_alloc();
					get_home_page();					
					perform_stop(t_tab.hash[i]);
					Sleep(2000);
					xfree_alloc();
					get_home_page();					
					
					wsprintf(buf, "PRIVMSG %s :x0,1#%i  %s  %s  %s  %s  %s\n", irc->sender, i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.stat[i]);
err_stop:
					for (k=0; buf[k] != ':'; k++);
					buf[k+1] = 3; 
					send(irc->sock, buf, lstrlen(buf), 0);
					break;
				}
			case PRIVMSG_PAUSE:
				{
					char *p=irc->argument, buf[1204]="";
					int i=0, k;

					while (*p >= '0' && *p <= '9') i = i * 10 + *p++ - '0';

					i--;
					if (i > t_tab.count || i < 0) {
						wsprintf(buf, "PRIVMSG %s :x0,4Invalid index!\n", irc->sender);
							goto err_pause;
					}
					xfree_alloc();
					get_home_page();					
					perform_pause(t_tab.hash[i]);
					Sleep(2000);
					xfree_alloc();
					get_home_page();					
					
					wsprintf(buf, "PRIVMSG %s :x1,15#%i  %s  %s  %s  %s  %s\n", irc->sender, i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.stat[i]);
err_pause:
					for (k=0; buf[k] != ':'; k++);
					buf[k+1] = 3; 
					send(irc->sock, buf, lstrlen(buf), 0);
					break;
				}
			case PRIVMSG_RECHECK:
				{
					char *p=irc->argument, buf[1204]="";
					int i=0, k;

					while (*p >= '0' && *p <= '9') i = i * 10 + *p++ - '0';

					i--;
					if (i > t_tab.count || i < 0) {
						wsprintf(buf, "PRIVMSG %s :x0,4Invalid index!\n", irc->sender);
							goto err_recheck;
					}
					xfree_alloc();
					get_home_page();					
					perform_recheck(t_tab.hash[i]);
					Sleep(2000);
					xfree_alloc();
					get_home_page();					
					
					wsprintf(buf, "PRIVMSG %s :x0,1#%i  %s  %s  %s  %s  %s\n", irc->sender, i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.stat[i]);
err_recheck:
					for (k=0; buf[k] != ':'; k++);
					buf[k+1] = 3; 
					send(irc->sock, buf, lstrlen(buf), 0);
					break;
				}
			case PRIVMSG_FORCE:
				{	
					char *p=irc->argument, buf[1204]="";
					int i=0, k;

					while (*p >= '0' && *p <= '9') i = i * 10 + *p++ - '0';

					i--;
					if (i > t_tab.count || i < 0) {
						wsprintf(buf, "PRIVMSG %s :x0,4Invalid index!\n", irc->sender);
							goto err_force;
					}
					xfree_alloc();
					get_home_page();					
					perform_force(t_tab.hash[i]);
					Sleep(2000);
					xfree_alloc();
					get_home_page();					
					
					wsprintf(buf, "PRIVMSG %s :x0,12#%i  %s  %s  %s  %s  %s\n", irc->sender, i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.stat[i]);
err_force:
					for (k=0; buf[k] != ':'; k++);
					buf[k+1] = 3; 
					send(irc->sock, buf, lstrlen(buf), 0);
					break;
				}
			case PRIVMSG_LIST:
				{	
					char *p=irc->argument, buf[1204]="";
					int i=0, k;
					
					if (list_running) break;
					list_running = 1;
					xfree_alloc();
					get_home_page();

					for (i=0; i<=t_tab.count; i++) {
						memset(&buf, 0, sizeof(buf));						
						wsprintf(buf, "PRIVMSG %s :x%s#%i  %s  %-70s  %-8s  %-8s %s %s\n", irc->sender, t_tab.color[i], i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.eta[i], t_tab.stat[i]);
						for (k=0; buf[k] != ':'; k++);
						buf[k+1] = 3; 
						send(irc->sock, buf, lstrlen(buf), 0);
						Sleep(1000);
					}
					list_running = 0;
					break;
				}
			case PRIVMSG_UPDATE:
				{
					char buf[1204]="";
					int i=0, k;
					
					if (update_running) break;
					update_running = 1;
					for (;;) {
						xfree_alloc();
						get_home_page();
						if (t_tab.count == 0) break;
						for (i=0; i<=t_tab.count; i++) {
							if (xstrstr(t_tab.stat[i], "download") == NULL) continue;
							memset(&buf, 0, sizeof(buf));						
							wsprintf(buf, "PRIVMSG %s :x2,15#%i  %s  %-80s  %-8s  %-8s  %s\n", irc->sender, i+1, t_tab.order[i], t_tab.name[i], t_tab.size[i], t_tab.done[i], t_tab.stat[i]);
							for (k=0; buf[k] != ':'; k++);
							buf[k+1] = 3; 
							k = send(irc->sock, buf, lstrlen(buf), 0);
							if (k <= 0) break;
							Sleep(3000);
						}
						if (k <= 0) break;
						Sleep(10000);
					}
				break;
				}
			case PRIVMSG_ADD:
				{	
					DWORD tick;
					int i=t_tab.count;
					char buf[256]="";
					
					tick = GetTickCount();

					perform_add_url(irc->argument);
					for (;;) {
						xfree_alloc();
						get_home_page();

						if (i != t_tab.count) break;

						if (GetTickCount()-tick > 30000) {
							wsprintf(buf, "PRIVMSG %s :x0,4Nothing added so far. Try again.(%d)\n", irc->sender, GetTickCount()-tick);
							for (i=0; buf[i] != ':'; i++);
							buf[i+1] = 3; 
							send(irc->sock, buf, lstrlen(buf), 0);
						}
						Sleep(1000);
					}
					
					break;
				}
	}
	HeapFree(GetProcessHeap(), 0, irc);
	//ExitThread(0);
	return 0;
}

static void msg_process(unsigned int sock, char *nick) 
{	
	char buf[4096], temp2[512], sender[128], *q;		
	DWORD t_tab;
	HANDLE hThread=NULL;
	int n;

	for (;;) {
		n=0;		
		
		if (is_readable(sock, 360)) {
			n = recv(sock, buf, sizeof(buf), 0);
		} else if (!update_running) {			
			break;		
		}

		if (n <= 0) break;		
		buf[n] = 0;
		printf("%s\n", buf);

		n = parse_cmd(buf);
		q = buf;
		switch (n) {		
			case CMD_PING: 
				{
					printf("pinging\n");
					while (*q == '\t' || *q == ' ') q++;
					q[1] = 'O';
					send(sock, q, lstrlen(q), 0);
					break;
				}
			case CMD_PRIVMSG:
				{	
					printf("privmsg\n");
					for (n=0; n<25 && *q && *q != '!'; q++) {
						if (*q == ':') continue;
						sender[n++] = *q;
					}				
					sender[n] = 0;
					while (*q == '\t' || *q == ' ') ++q;
					while (*q && *q++ != ' '); while (*q && *q++ != ' ');					
					if (xstrstr(q, nick)) {
						if (instr(1, q, "PING") == lstrlen(nick) + 4) {
							while (*q && *q != ' ') ++q;
							memset(&temp2, 0, sizeof(temp2));
							q[lstrlen(q)-1] = '\n';
							q[lstrlen(q)] = '\0';
							wsprintf(temp2, "NOTICE %s%s", sender, q);
							send(sock, temp2, lstrlen(temp2), 0);
							break;
						}	
						while (*q && *q++ != ':');
						q[lstrlen(q)-1] = 0;
						/* now filter supported commands */
						switch (process_privmsg(q)) {
							case PRIVMSG_START:
								{
									char *p, arg[256];
									struct ircbot_sync_t *irc;
									int i=0;									

									p = xstrstr(q, "#");if (p == NULL) break;
									p++;
									while (*p >= '0' && *p <= '9') arg[i++] = *p++;
									arg[i] = '\0';

									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));
									lstrcpy(irc->argument, arg);
									irc->sock = sock;
									irc->cmd = PRIVMSG_START;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);

									break;
								}
							case PRIVMSG_STOP:
								{									
									char *p, arg[256];
									struct ircbot_sync_t *irc;
									int i=0;									

									p = xstrstr(q, "#");if (p == NULL) break;
									p++;
									while (*p >= '0' && *p <= '9') arg[i++] = *p++;
									arg[i] = '\0';

									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));
									lstrcpy(irc->argument, arg);
									irc->sock = sock;
									irc->cmd = PRIVMSG_STOP;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);

									break;
								}
							case PRIVMSG_PAUSE:
								{
									char *p, arg[256];
									struct ircbot_sync_t *irc;
									int i=0;									

									p = xstrstr(q, "#");if (p == NULL) break;
									p++;
									while (*p >= '0' && *p <= '9') arg[i++] = *p++;
									arg[i] = '\0';

									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));
									lstrcpy(irc->argument, arg);
									irc->sock = sock;
									irc->cmd = PRIVMSG_PAUSE;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);

									break;
								}
							case PRIVMSG_RECHECK:
								{
									char *p, arg[256];
									struct ircbot_sync_t *irc;
									int i=0;									

									p = xstrstr(q, "#");if (p == NULL) break;
									p++;
									while (*p >= '0' && *p <= '9') arg[i++] = *p++;
									arg[i] = '\0';

									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));
									lstrcpy(irc->argument, arg);
									irc->sock = sock;
									irc->cmd = PRIVMSG_RECHECK;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);

									break;
								}
							case PRIVMSG_FORCE:
								{	
									char *p, arg[256];
									struct ircbot_sync_t *irc;
									int i=0;									

									p = xstrstr(q, "#");if (p == NULL) break;
									p++;
									while (*p >= '0' && *p <= '9') arg[i++] = *p++;
									arg[i] = '\0';

									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));
									lstrcpy(irc->argument, arg);
									irc->sock = sock;
									irc->cmd = PRIVMSG_FORCE;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);

									break;
								}
							case PRIVMSG_LIST:
								{	
									struct ircbot_sync_t *irc;
									int i=0;
									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));									
									irc->sock = sock;
									irc->cmd = PRIVMSG_LIST;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);
									break;
								}
							case PRIVMSG_UPDATE:
								{	
									struct ircbot_sync_t *irc;
									int i=0;
									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));									
									irc->sock = sock;
									irc->cmd = PRIVMSG_UPDATE;
									lstrcpy(irc->sender, sender);

									hThread = CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);
									break;
								}
							case PRIVMSG_STOP_UPDATE:
									if (hThread != NULL && hThread != INVALID_HANDLE_VALUE) {
											TerminateThread(hThread, 0);
											update_running = 0;
									}
										break;
							case PRIVMSG_ADD:
								{	
									char *p, arg[512];
									struct ircbot_sync_t *irc;
									int i=0;									

									p = xstrstr(q, " "); if (p == NULL) break;
									p++;
									while (*p && *p != '\n') arg[i++] = *p++;
									arg[i] = '\0';

									irc = (struct ircbot_sync_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_sync_t));
									lstrcpy(irc->argument, arg);
									irc->sock = sock;
									irc->cmd = PRIVMSG_ADD;
									lstrcpy(irc->sender, sender);

									CreateThread(0, 0, ircbot_sync_th, (LPVOID)irc, 0, &t_tab);

									break;
								}
						} /* end of switch() */
					} /* end of xstrstr() */
					break;
				} /* end of CMD_PRIVMSG */
		} /* end of switch() */
	}
	closesocket(sock);
}

DWORD _stdcall ircbot_main_th(LPVOID pv)
{
	struct ircbot_t *irc=(struct ircbot_t *)pv;	
	unsigned int sock, k, i, j;			
	char buf[MAX_PATH], usr[128];	
	char szNick[80], szUser[80], szChannel[80];
	DWORD buf_size = sizeof(buf);

	GetComputerName(buf, &buf_size);		

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock != INVALID_SOCKET) {
		printf("connecting...\n");
		k = irc_connect(sock, irc->server, irc->port);
		
		if (k != 0) goto err;
		printf("connected\n");
		k = 6 + _rand() % 4; /* nick 6-9 chars */
		for (i=0, j=0; i<k;) {
			j = _rand() % 58 + 65;
			if ((j >= 'A' && j <= 'Z') || (j >= 'a' && j <= 'z'))
				usr[i++] = j;
		}
		usr[i] = '\0';
		usr[0] = '_';

		wsprintf(szNick, "NICK %s\n", usr);
		wsprintf(szUser, "USER %s \"%s\" \"%s\" :biniam\n", usr, buf, irc->server);
		wsprintf(szChannel, "JOIN %s\n", irc->channel);
		send(sock, szNick, lstrlen(szNick), 0);
		send(sock, szUser, lstrlen(szUser), 0);
		send(sock, szChannel, lstrlen(szChannel), 0);						
		
			/* go on the loop */			
		msg_process(sock, usr);
		//ExitThread(0);
		return 0;

	}
err:	
	if (sock != INVALID_SOCKET) closesocket(sock);
	//ExitThread(0);
	return 0;
}

DWORD WINAPI ircbot_main(LPVOID pv)
{
	struct ircbot_t *bot;
	HANDLE hThread[2] = {NULL, NULL};
	DWORD t_tab;
	
	

	bot = (struct ircbot_t *)HeapAlloc(GetProcessHeap(), 0, sizeof(struct ircbot_t));
	/* channel 1 */
	lstrcpy(bot->channel, "#yournickhere");	
	lstrcpy(bot->server, "irc.yourircserver.com");	
	bot->port = IRC_PORT; // irc server port

	hThread[0] = CreateThread(0, 0, ircbot_main_th, (LPVOID)bot, 0, &t_tab);

	WaitForSingleObject(hThread[0], INFINITE);

	return 0;
}

/*
void main()
{
//	struct ircbot_t *q;
	DWORD t_tab;
	HANDLE hThread;
	WSADATA wsad;	

	
	WSAStartup(0x2, &wsad);
	init_cs();
	_rand_init();	

	//waitformultipleobject()-- when a thread terminates.. start it again.	
	for (;;) {
		memset(&t_tab, 0, sizeof(t_tab));
		get_home_page();
		hThread = CreateThread(0, 0, ircbot_main, NULL, 0, &t_tab);
		if (hThread != NULL && hThread != INVALID_HANDLE_VALUE)
			WaitForSingleObject(hThread, INFINITE);
		xfree_alloc();
		Sleep(2000);
	}
}
*/

  
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	DWORD t_tab;
	HANDLE hThread;
	WSADATA wsad;	

	WSAStartup(0x2, &wsad);
	init_cs();
	_rand_init();	
	
	for (;;) {
		memset(&t_tab, 0, sizeof(t_tab));
		get_home_page();
		hThread = CreateThread(0, 0, ircbot_main, NULL, 0, &t_tab);
		if (hThread != NULL && hThread != INVALID_HANDLE_VALUE)
			WaitForSingleObject(hThread, INFINITE);
		xfree_alloc();
	}
	return 0;
}
