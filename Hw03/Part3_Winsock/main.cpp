#include <sstream>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include <vector>
#include <errno.h>
#include <windows.h>
#include <list>
using namespace std;

#include "resource.h"

#define SERVER_PORT 7799

#define WM_SOCKET_NOTIFY (WM_USER + 1)
#define MAX_HOST_NUM 5
#define HOST_SOCKET_NOTIFY (WM_USER + 2)
#define MAX_BUFFER_SIZE 10001
#define COLUMN_NUM 3

struct Request
{
    string method;
    string file;
    string args;
    string protocol;
    string fileformat;
};

struct HostInfo
{
    string host;
    string ip;
    string patchFile;
    unsigned short port;
    int ssock , hsock;
    FILE* filePtr;
    string id;
    bool isExist;
    
    struct sockaddr_in serv_addr;
    struct hostent *he;
    
}hostInfoTable[5];

enum ShowCato
{
    CMDTXT,
    PROMPTORRESULT
};

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf (HWND, TCHAR *, ...);
void ParseRequest(char*,Request&);
void HttpAck();
//void ProcessHtml();
void ProcessCGI(int);
void ParseQueryString(string);
int FileReadLine(FILE*,char*,int);
int ReadLineFromSock(int,char*,int);
int GetHostIndexBySock(int);
vector<string> Split(string,char);
void AddMsgInHtml(string , int , ShowCato);
void SendCGIHtmlHeader(int);
string ReplaceBracket(string);

//=================================================================
//	Global Variables
//=================================================================
list<SOCKET> Socks;
WSADATA wsaData;
static HWND hwndEdit;
static SOCKET msock, ssock;
static struct sockaddr_in sa;

int hostIndex;
Request request ;
char SSockRecvBuff[MAX_BUFFER_SIZE];
char HSockRecvBuff[MAX_BUFFER_SIZE];

int SSockRecvLen;
int HSockRecvLen;

FILE *htmlfp ;
FILE *patchfp ;
string s ;
int totalhost = 0;


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow)
{
	
	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	
    int err;
	
	switch(Message) 
	{
		case WM_INITDIALOG:
			hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
			break;
		case WM_COMMAND:     /* 監聽整個Window視窗部分 (e.g.任何button) */
			switch(LOWORD(wParam))
			{
				case ID_LISTEN: /* 當按下 'Linsten' button 時觸發 */

					WSAStartup(MAKEWORD(2, 0), &wsaData);

					//create master socket
					msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);


					if( msock == INVALID_SOCKET ) {
						EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
						WSACleanup();
						return TRUE;
					}

					err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ | FD_WRITE);

					if ( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
						closesocket(msock);
						WSACleanup();
						return TRUE;
					}

					//fill the address info about server
					sa.sin_family		= AF_INET;
					sa.sin_port			= htons(SERVER_PORT);
					sa.sin_addr.s_addr	= INADDR_ANY;

					//bind socket
					err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
						WSACleanup();
						return FALSE;
					}

					err = listen(msock, 2);
		
					if( err == SOCKET_ERROR ) {
						EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
						WSACleanup();
						return FALSE;
					}
					else {
						EditPrintf(hwndEdit, TEXT("=== Server START ===\r\n"));
					}

					break;
				case ID_EXIT:
					EndDialog(hwnd, 0);
					break;
			};
			break;

		case WM_CLOSE:
			EndDialog(hwnd, 0);
			break;

		case WM_SOCKET_NOTIFY:      /* 負責監聽Socket事件 (從Broser Clinet到WinSock Process的事件) */
			switch( WSAGETSELECTEVENT(lParam) )
			{
				case FD_ACCEPT:
					ssock = accept(msock, NULL, NULL);
					Socks.push_back(ssock);
					EditPrintf(hwndEdit, TEXT("=== Accept one new client(%d), List size:%d ===\r\n"), ssock, Socks.size());
					break;
				case FD_READ:
				//Write your code for read event here.
					
					SSockRecvLen = recv(wParam, SSockRecvBuff, sizeof(SSockRecvBuff), 0);
					SSockRecvBuff[SSockRecvLen] = '\0';

                    ParseRequest(SSockRecvBuff,request) ;
						
                    if(request.fileformat == "cgi")
                    {
                        /* HTTP Ack */
                        send(wParam, "HTTP/1.1 200 OK\r\n", strlen("HTTP/1.1 200 OK\r\n"), 0);
                        send(wParam, "Content-Type: text/html\r\n", strlen("Content-Type: text/html\r\n"), 0);
                        send(wParam, "\r\n", strlen("\r\n"), 0);
							
                        if(request.file == "/hw3.cgi")
                        {
                            ProcessCGI(wParam);           /* wParm : 監聽到事件的Socket fd */
                            SendCGIHtmlHeader(wParam);
								
                            for(int i = 0 ; i < MAX_HOST_NUM ; i ++)
                            {
                                if(hostInfoTable[i].isExist)
                                {
                                    /* 註冊連線到Host的socket的監聽(HOST_SOCKET_NOTIFY) */
                                    err = WSAAsyncSelect(hostInfoTable[i].hsock, hwnd, HOST_SOCKET_NOTIFY, FD_CONNECT | FD_CLOSE | FD_READ | FD_WRITE );
                                    EditPrintf(hwndEdit, TEXT("IP:[%s]\r\n"), hostInfoTable[i].ip.c_str());
                                    totalhost ++;
                                    if (err == SOCKET_ERROR)
                                    {
                                        EditPrintf(hwndEdit, TEXT("=== Error: select error for host [%d]===\r\n"), i);
                                        closesocket(hostInfoTable[i].hsock);
                                    }
                                }
                            }
								
                        }

                    }
                    else if(request.fileformat == "html" || request.fileformat == "htm" )
                    {
                        request.file = request.file.substr(1,request.file.length()-1);  /* 去掉第一個字元 '/' */
                        htmlfp = fopen(request.file.c_str(),"r");
							
                        if(htmlfp != NULL)
                        {
                            char fileBuff[MAX_BUFFER_SIZE];
                            int n;
                            while((n = FileReadLine(htmlfp,fileBuff,MAX_BUFFER_SIZE)) > 0)
                            {
                                send(wParam,fileBuff,n,0);
                            }
								fclose(htmlfp);
                        }
                        else
                        {
                            EditPrintf(hwndEdit, TEXT("Error ! File not open"));
                            char filename[50];
                            strcpy(filename,request.file.c_str());
                            EditPrintf(hwndEdit, TEXT(filename));
                            char error_meg[100];
                            strcpy(error_meg,filename);strcat(error_meg,": file not open");
                            send(wParam,error_meg,strlen(error_meg),0);
                        }
							closesocket(wParam);
                    }
					
					break;
				case FD_WRITE:
				//Write your code for write event here

					break;
				case FD_CLOSE:
					break;
			};
			break;

		case HOST_SOCKET_NOTIFY:  /* 負責監聽Socket事件 (從Host Server到Winsock Process的事件) */
			switch(WSAGETSELECTEVENT(lParam))
			{
				case FD_CONNECT:
					break;
				case FD_READ:
					
					hostIndex = GetHostIndexBySock(wParam);
					HSockRecvLen = ReadLineFromSock(wParam,HSockRecvBuff,sizeof(HSockRecvBuff));

                    if(HSockRecvLen>0)
                    {
                        
                        HSockRecvBuff[HSockRecvLen] = '\0';
						EditPrintf(hwndEdit, TEXT("Recv Msg:[%s]\r\n"),  HSockRecvBuff);
                        
						AddMsgInHtml(string(HSockRecvBuff), hostIndex , PROMPTORRESULT);
                        
						if(HSockRecvBuff[0] == '%' && HSockRecvBuff[1] == ' ')  /* Prompt , wait for file cmd */
                        {
                           
							char cmd[MAX_BUFFER_SIZE];
							int cmdLen ;
                            
							cmdLen = FileReadLine(hostInfoTable[hostIndex].filePtr,cmd,sizeof(cmd));
							cmd[cmdLen] = '\n';
							cmd[cmdLen+1] = '\0';
							send(wParam,cmd,cmdLen+1,0);
							cmd[cmdLen] = '\0';

							AddMsgInHtml(string(cmd),hostIndex,CMDTXT);
							EditPrintf(hwndEdit, TEXT("cmd from patch file : [%s]\r\n"),cmd);
                        }
                    }
					break;
				case FD_WRITE:
					break;
				case FD_CLOSE:
					hostIndex = GetHostIndexBySock(wParam);
					hostInfoTable[hostIndex].isExist = false;
                    totalhost -- ;
                    if(totalhost <= 0)
                        closesocket(hostInfoTable[hostIndex].ssock);
					break;

			};

			break;
		
		default:
			return FALSE;


	};

	return TRUE;
}

int EditPrintf (HWND hwndEdit, TCHAR * szFormat, ...)
{
     TCHAR   szBuffer [1024] ;
     va_list pArgList ;

     va_start (pArgList, szFormat) ;
     wvsprintf (szBuffer, szFormat, pArgList) ;
     va_end (pArgList) ;

     SendMessage (hwndEdit, EM_SETSEL, (WPARAM) -1, (LPARAM) -1) ;
     SendMessage (hwndEdit, EM_REPLACESEL, FALSE, (LPARAM) szBuffer) ;
     SendMessage (hwndEdit, EM_SCROLLCARET, 0, 0) ;
	 return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0); 
}

string ReplaceBracket(string msg)
{
    string res = "";
    for(int i = 0 ; i < msg.length() ; i ++)
    {
        if(msg[i] == '<')
            res += "&lt";
        else if(msg[i] == '>')
            res += "&gt";
        else if(msg[i] == '\n')
            res += "<br>";
        else if(msg[i] == '\r' && msg[i+1] == '\n')
        {
            res += "<br>";
            i++;
        }
        else if(msg[i] == '"')
            res += "&quot;";
        else
            res += msg[i];
    }
    
    return res;
}

void SendCGIHtmlHeader(int ssock)
{
	string temp = "" ;
	//send(ssock ,"Content-type: text/html\r\n" ,strlen("Content-type: text/html\r\n") , 0 );
    send(ssock , "<html>",strlen("<html>") , 0 );
    send(ssock , "<head>" , strlen("<head>") , 0 );
    send(ssock , "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />" , strlen("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />") , 0 ) ; 
    send(ssock , "<title>Network Programming Homework 3</title>" , strlen("<title>Network Programming Homework 3</title>") , 0 ) ; 
    send(ssock , "</head>" , strlen("</head>") , 0 ) ; 
    send(ssock , "<body bgcolor=#336699>" , strlen("<body bgcolor=#336699>") , 0 ) ; 
    send(ssock , "<font face=\"Courier New\" size=2 color=#FFFF99>" , strlen("<font face=\"Courier New\" size=2 color=#FFFF99>") , 0 ) ;
    send(ssock , "<table width=\"800\" border=\"1\">" , strlen("<table width=\"800\" border=\"1\">") , 0 ) ;
    send(ssock , "<tr>" , strlen("<tr>") , 0 ) ; 
    for(int i = 0 ; i < MAX_HOST_NUM ; i++)
    {
        if(hostInfoTable[i].isExist)
		{
			temp = "<td>" + hostInfoTable[i].ip + "</td>" ;
			send(ssock , temp.c_str() , strlen(temp.c_str()) , 0);
		}
    }
    send(ssock , "</tr>" , strlen("</tr>") , 0 ) ;
    send(ssock , "<tr>" , strlen("<tr>") , 0 ) ;
    for(int i = 0 ; i < MAX_HOST_NUM ; i++)
    {
        if(hostInfoTable[i].isExist)
		{
			temp = "<td valign=\"top\" id=\"" + hostInfoTable[i].id + "\"></td>";
            send(ssock , temp.c_str() , strlen(temp.c_str()) , 0) ;
		}
    }
	send(ssock,"</tr>",strlen("</tr>"),0);
	send(ssock,"</table>",strlen("</table>"),0);
	send(ssock,"</font>",strlen("</font>"),0);
	send(ssock,"</body>",strlen("</body>"),0);
	send(ssock,"</html> ",strlen("</html> "),0);
}

void AddMsgInHtml(string msg,int hostIndex,ShowCato showCato)
{
	string temp ;
    if(showCato == CMDTXT)
    {
		temp = "<script>document.all['"+ hostInfoTable[hostIndex].id+"'].innerHTML += \"<b>"+msg+"<br></b>\";</script>\n";
    }
    else
    {
        if(msg[0] == '%' && msg[1] == ' ')
        {
        	temp = "<script>document.all['"+hostInfoTable[hostIndex].id+"'].innerHTML += \""+msg+"\";</script>\n";
        }
        else
        {
			msg = ReplaceBracket(msg);
        	temp = "<script>document.all['"+hostInfoTable[hostIndex].id+"'].innerHTML += \""+msg+"<br>\";</script>\n";
			
        }
    }
	send(hostInfoTable[hostIndex].ssock,temp.c_str(),strlen(temp.c_str()),0);
}

int GetHostIndexBySock(int hsock)
{
	for(int i = 0 ; i < MAX_HOST_NUM ; i ++)
	{
		if(hostInfoTable[i].hsock == hsock && hostInfoTable[i].isExist == true )
			return i;
	}
	return -1;
}

vector<string> Split(string str, char delimiter) {
    vector<string> internal;
    stringstream ss(str); // Turn the string into a stream.
    string tok;
    
    while(getline(ss, tok, delimiter)) {
        internal.push_back(tok);
    }
    
    return internal;
}

int FileReadLine(FILE* fd,char *ptr,int maxlen)
{
    int n,rc;
    char c;
    *ptr = 0;
    for(n=1;n<maxlen;n++)
    {
        c = fgetc(fd);
        if (c != EOF)
		{
			*ptr++ = c;
			if (c == '\n' || c == '\r')
			{
				if ( n != 1) return (n-1);
                else return 1;
			}
		}
		else
		{
			if (n == 1)     return 0;
			else         return (n-1);
		}
    }

    return(n);
}

int ReadLineFromSock(int sock,char *ptr,int maxlen)
{
    int n,rc;
    char c;
    *ptr = 0;
    for(n=1;n<maxlen;n++)
    {
        if((rc=recv(sock,&c,1,0)) == 1)
        {
            *ptr++ = c;
            if(c==' '&& *(ptr-2) =='%') break; // 記得加此 ！
            if(c=='\n' || c == '\r' )  return (n-1); // 否則html會怪怪！？
        }
        else if(rc==0)
        {
            if(n==1)     return(0);
            else         break;
        }
        else
            return(-1);
    }
    /* 當rc == 0 , 代表當下字元為空 , 因此實際字元數為 n-1 */
    if(rc == 0)
        return (n-1);
    return(n);
}

void ParseQueryString(string querystring)
{
    vector<string> firstSplit = Split(querystring, '&');
    vector<string> secondSplit ;
    
    int index = 0;      /* index for host table */
    int itIndex = 0;    /* index for vector */
    
    while(itIndex < MAX_HOST_NUM * COLUMN_NUM)
    {
        for(int i = 0 ; i < COLUMN_NUM ; i++)
        {
            
            secondSplit.clear();
            secondSplit = Split(firstSplit[itIndex], '=');
            
            /* e.g. 'h1=' */
            if(secondSplit.size()<2)
            {
                hostInfoTable[index].isExist = false;
                itIndex += (COLUMN_NUM - i);
                break;
            }
            
            if(i == 0)
                hostInfoTable[index].host = secondSplit[1];
            else if(i==1)
                hostInfoTable[index].port = atoi(secondSplit[1].c_str());
            else if(i==2)
            {
                hostInfoTable[index].patchFile = secondSplit[1];
                hostInfoTable[index].filePtr = fopen(hostInfoTable[index].patchFile.c_str(),"r");
                hostInfoTable[index].id = "m" + to_string(index);
                hostInfoTable[index].isExist = true;
    
            }
            itIndex += 1;
        }
        
        index ++;
    }
}

void ConnectHost(int wParm)
{
	for (int i = 0; i < MAX_HOST_NUM; i++)
	{
		if (hostInfoTable[i].isExist)
		{
			if((hostInfoTable[i].he = gethostbyname(hostInfoTable[i].host.c_str())) == NULL){
                perror("Get Host By Name Error!");
            }
			
			hostInfoTable[i].hsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			memset(&hostInfoTable[i].serv_addr, 0, sizeof(&hostInfoTable[i].serv_addr));
			hostInfoTable[i].serv_addr.sin_family = AF_INET;
            hostInfoTable[i].serv_addr.sin_addr = *((struct in_addr *)hostInfoTable[i].he -> h_addr);
            hostInfoTable[i].serv_addr.sin_port = htons(hostInfoTable[i].port);
			hostInfoTable[i].ssock = wParm ; 

		    hostInfoTable[i].ip = inet_ntoa(hostInfoTable[i].serv_addr.sin_addr);

            if(connect(hostInfoTable[i].hsock, (struct sockaddr*)&hostInfoTable[i].serv_addr,sizeof(hostInfoTable[i].serv_addr)) < 0)
			{
				perror("connect");
			}
		}
	}
}

/* 執行一些Cgi的前置作業 */
void ProcessCGI(int wParm)
{
	ParseQueryString(request.args);
	ConnectHost(wParm);
}


void ParseRequest(char *buffer,Request &request)
{
    char *tok;
    int count = 0 ;
    bool hasArg = false;
    
    if(strstr(buffer, "?") != NULL)
        hasArg =true;
    
    tok = strtok(buffer, " ?\n\r");
    while (tok != NULL)
    {
        if(count == 0)
            request.method = string(tok);
        else if(count == 1)
            request.file = string(tok);
        else if(count == 2)
        {
            if(hasArg)
                request.args = string(tok);
            else
            {
                request.protocol = string(tok);
                break;
            }
        }
        else if(count == 3)
        {
            request.protocol = string(tok);
            break;
        }
        
        count ++;
        tok = strtok (NULL, " ?\n\r");
    }
    
    int dotIndex = (request.file).find(".");
    request.fileformat = (request.file).substr(dotIndex+1,((request.file).length() - dotIndex - 1));
    
}


