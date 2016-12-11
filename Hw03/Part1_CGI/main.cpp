//
//  main.cpp
//  NPHW03_CGI
//
//  Created by Ricky on 2016/11/26.
//


#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <vector>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string>
#include <string.h>

#define MAX_SERVER_NUM 5
#define COLUMN_NUM 3        /* IP , Port , PatchFileName */

using namespace std;

enum ShowCato
{
    CMDTXT,
    PROMPTORRESULT
};


struct HostInfo
{
    string host;
    string ip;
    string patchFile;
    unsigned short port;
    int sockFd,fileFd;              /* sockfd : 和該Host連線的sock , fileFd :該Host會用到的patch File */
    string id;
    bool isExist;
    
    struct sockaddr_in serv_addr;
    struct hostent *he;
    
}hostInfoTable[5];



fd_set afds;
int hostCount = 0;


void PrintHeader();
void ParseQueryString(string);
vector<string> Split(string,char);
void SocketCreateAndConnect();
void ListenSocket();
void AddMsgInHtml(string , int , ShowCato);
string ReplaceBracket(string);
int ReadLine(int,char,int);

/* 有點類似Html上的逃脫字元方法 */
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

/* 回傳一個Line , 且結尾不會有 \r , \n (P.S. html中的字串不接受以\r,\n結尾)*/
int ReadLine(int fd,char *ptr,int maxlen)
{
    int n,rc;
    char c;
    *ptr = 0;
    for(n=1;n<maxlen;n++)
    {
        if((rc=read(fd,&c,1)) == 1)             /* 一個byte一個byte讀取 */
        {
            *ptr++ = c;
            if(c==' '&& *(ptr-2) =='%') break;  /* 因為當Prompt出現時 , 不會以換行做結尾 , 需另外判斷 */
            if(c=='\n' || c == '\r')
            {
                if ( n != 1) return (n-1);
                else return 1;
            }
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

void PrintHeader()
{
    cout << "Content-type: text/html\n" << endl;
    cout << "<html>" << endl;
    cout << "<head>" << endl;
    cout << "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />" << endl;
    cout << "<title>Network Programming Homework 3</title>" << endl;
    cout << "</head>" << endl;
    cout << "<body bgcolor=#336699>" << endl;
    cout << "<font face=\"Courier New\" size=2 color=#FFFF99>" << endl;
    cout << "<table width=\"800\" border=\"1\">" << endl;
    cout << "<tr>" << endl;
    for(int i = 0 ; i < MAX_SERVER_NUM ; i++)
    {
        if(hostInfoTable[i].isExist)
            cout << "<td>" << hostInfoTable[i].ip << "</td>";
    }
    cout << "</tr>" << endl;
    cout << "<tr>" << endl;
    for(int i = 0 ; i < MAX_SERVER_NUM ; i++)
    {
        if(hostInfoTable[i].isExist)
            cout << "<td valign=\"top\" id=\"" << hostInfoTable[i].id << "\"></td>";
    }
    cout << "</tr>" << endl;
    cout << "</table>" << endl;
    cout << "</font>" << endl;
    cout << "</body>" << endl;
    cout << "</html> " << endl;
}

/* Setting HostInfo :   host/port/patchfile/fileFd/id/isExist */
void ParseQueryString(string querystring)
{
    vector<string> firstSplit = Split(querystring, '&');
    vector<string> secondSplit ;
    
    int index = 0;      /* index for host table */
    int itIndex = 0;    /* index for vector */
    
    while(itIndex < MAX_SERVER_NUM*COLUMN_NUM)
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
                hostInfoTable[index].fileFd = open(hostInfoTable[index].patchFile.c_str(),O_RDONLY);
                hostInfoTable[index].id = "m" + to_string(index);
                hostInfoTable[index].isExist = true;
                hostCount ++ ;
            }
            itIndex += 1;
        }
        
        index ++;
    }
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


/*  與各個Host連線 , 並且設定好select()要使用的探針
 * 
 *  Set HostInfo : serv_addr/ *he/ip/sockfd
 */
void SocketCreateAndConnect()
{
    for(int i = 0 ; i < MAX_SERVER_NUM ; i ++)
    {
        if(hostInfoTable[i].isExist)
        {
            if((hostInfoTable[i].he = gethostbyname(hostInfoTable[i].host.c_str())) == NULL){
                cout << "Error: get host by name ! " << endl;
                exit(1);
            }
            
            /* Setting for client */
            bzero((char *) &hostInfoTable[i].serv_addr, sizeof(hostInfoTable[i].serv_addr));
            hostInfoTable[i].serv_addr.sin_family = AF_INET;
            hostInfoTable[i].serv_addr.sin_addr = *((struct in_addr *)hostInfoTable[i].he -> h_addr); /* 欲連到的Server Ip */
            hostInfoTable[i].serv_addr.sin_port = htons(hostInfoTable[i].port); /* 欲使用的Server Port */
            
            /* 將addr轉成字串形式IP  */
            hostInfoTable[i].ip = inet_ntoa(hostInfoTable[i].serv_addr.sin_addr);
            
            /* Create socket */
            if ((hostInfoTable[i].sockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                perror("client: can't open stream socket");
            
            /* Non-blocking Connecting */
            int flags;
            
            if((flags = fcntl(hostInfoTable[i].sockFd, F_GETFL)) < 0) /* 取得當前該fd的flag */
                perror("F_GETFL error!");
            
            /* 修改flag使該fd變成Non-blocking */
            flags |= O_NONBLOCK;
            if(fcntl(hostInfoTable[i].sockFd, F_SETFL, flags) < 0)
                perror("F_SETFL error!");
            
            if(connect(hostInfoTable[i].sockFd, (struct sockaddr*)&hostInfoTable[i].serv_addr,sizeof(hostInfoTable[i].serv_addr)) < 0)
                if(errno!= EINPROGRESS) exit(-1);
            
            FD_SET(hostInfoTable[i].sockFd,&afds);
        }
    }
}

void ListenSocket()
{
    fd_set rfds,wfds,rstemp,wstemp ;
    int nfds ;
    nfds = getdtablesize();
    
    FD_ZERO(&rfds);FD_ZERO(&wfds);FD_ZERO(&rstemp);FD_ZERO(&rstemp);
    bcopy((char *)&afds, (char *)&rfds, sizeof(rfds));
    //bcopy((char *)&afds, (char *)&wfds, sizeof(wfds)); /* 先不監聽write探針,因為只要write buff未滿,就會不斷進入wirte探針 */
    char buffer[5][10001];
    char fileInput[5][10001];
    
    while(hostCount>0)
    {
        bcopy((char *)&rfds, (char *)&rstemp, sizeof(rstemp));
        bcopy((char *)&wfds, (char *)&wstemp, sizeof(wstemp));
        
        if(select(nfds,&rstemp,&wstemp,NULL,NULL) < 0) /* 監聽readfd以及writefd */
        {
            perror("Select error!");
            exit(1);
        }
        
        for(int i = 0 ; i < MAX_SERVER_NUM ; i ++)
        {
            if(hostInfoTable[i].isExist)
            {
                /* 發現了一個read fd request (from host to cgi) */
                if(FD_ISSET(hostInfoTable[i].sockFd,&rstemp))
                {
                    int n = ReadLine(hostInfoTable[i].sockFd, buffer[i], sizeof(buffer[i])); /* Read msg from Host */
                    if(n>0)
                    {
                        
                        buffer[i][n] = '\0';    /* Remeber to add this as C string end */
                        AddMsgInHtml(string(buffer[i]), i , PROMPTORRESULT);
                        if(buffer[i][0] == '%' && buffer[i][1] == ' ')  /* 若是Prompt , Host等待我們輸入Input , 可先關閉該Hostfd read探針 , 並開啟write探針 */
                        {
                            FD_CLR(hostInfoTable[i].sockFd,&rfds);
                            FD_SET(hostInfoTable[i].sockFd,&wfds);
                        }
                        
                    }
                    else if(n == 0)
                    {
                        /* Host exit! */
                        FD_CLR(hostInfoTable[i].sockFd,&rfds);
                        FD_CLR(hostInfoTable[i].sockFd,&wfds);
                        close(hostInfoTable[i].sockFd);
                        hostCount --;
                    }
                    else
                        perror("Read form host error!");
                }
                
                /* 只要fd wirte buffer 有位置 , 就會不斷進入此 if */
                if(FD_ISSET(hostInfoTable[i].sockFd,&wstemp))
                {
                    int f = ReadLine(hostInfoTable[i].fileFd, fileInput[i], sizeof(fileInput[i])); /* Read from Patch file */
                    if(f>=0)
                    {
                        fileInput[i][f] = '\0';                    /* 先'\0' , 給Html之output使用 */
                        AddMsgInHtml(fileInput[i], i, CMDTXT);
                        
                        if(f != 1)
                        {
                            fileInput[i][f] = '\n';                    /* '\n' 結尾給Input到Host的字串使用 . (\n 代表按下Enter , 輸入結束) */
                            if(write(hostInfoTable[i].sockFd, fileInput[i], f+1)<0)
                                cout << "write error!" << endl;
                            FD_CLR(hostInfoTable[i].sockFd,&wfds);
                        }
                        
                        /* 當有讀近來東西(表示有Input要Host端處理了) , 才SET reader 探針 */
                        if(f > 0)
                            FD_SET(hostInfoTable[i].sockFd,&rfds);
                    }
                    else
                    {
                        AddMsgInHtml("Error! without open file!", i, PROMPTORRESULT);
                        FD_CLR(hostInfoTable[i].sockFd,&rfds);
                        FD_CLR(hostInfoTable[i].sockFd,&wfds);
                        close(hostInfoTable[i].sockFd);
                        hostCount --;
                    }
                }
            }
            
        }
    }
}

/* 動態插入訊息至Html Table */
void AddMsgInHtml(string msg,int hostIndex,ShowCato showCato)
{
    if(showCato == CMDTXT)
    {
        cout << "<script>document.all['"<<hostInfoTable[hostIndex].id<<"'].innerHTML += \"<b>"<<msg<<"<br></b>\";</script>" << endl;
    }
    else
    {
        if(msg[0] == '%' && msg[1] == ' ')
        {
            cout << "<script>document.all['"<<hostInfoTable[hostIndex].id<<"'].innerHTML += \""<<msg<<"\";</script>" << endl;
        }
        else
        {
            msg = ReplaceBracket(msg);
            cout << "<script>document.all['"<<hostInfoTable[hostIndex].id<<"'].innerHTML += \""<<msg<<"<br>\";</script>" << endl;
        }
    }
}



int main(int argc, const char * argv[]) {
    // insert code here...

    string queryString = getenv("QUERY_STRING");  /* "QUERY_STRING"放著Http Server傳來的參數 */
    FD_ZERO(&afds);
    
    ParseQueryString(queryString);
    SocketCreateAndConnect();
    PrintHeader();
    ListenSocket();
    
    
    return 0;
}
