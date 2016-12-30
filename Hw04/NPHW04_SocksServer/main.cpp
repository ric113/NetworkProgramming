//
//  main.cpp
//  NPHW04_SocksServer
//
//  Created by Ricky on 2016/12/18.
//

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <iterator>

using namespace std ;

#define MAX_BUFFER_SIZE 10000
#define CONFIG_FILE "socks.conf"

struct SOCKReq
{
    int mode ;                      /* 1 : CONNECT , 2 : BIND */
    unsigned short DST_port ;
    unsigned short SRC_port ;
    string SRC_ip ;
    unsigned char DST_ip[4];        /* XXX.XXX.XXX.XXX , 分別存在 [0] , [1] , [2] , [3]*/
    int replyMode ;                 /* 0 : Accept , 1 : Reject */
};

struct IPFormat
{
    string ip[4];       /* 存IP addr. 的四段 */
};

int stdinfd , stdoutfd ;
vector<IPFormat> CMode_permitIp ;   /* CONNECT MODE permit IP table */
vector<IPFormat> BMode_permitIp ;   /* BIND MODE permit IP table */


int ConnectTCP(string,unsigned);
int PassiveTCP(unsigned);
void ParseSOCKSReq(int,SOCKReq&);
void ProcessConnection(int,SOCKReq&);
void SOCKSReply(int,SOCKReq);
string ChainIp(SOCKReq);
void PrintInfo(SOCKReq);
void RedirectDataFlow(int,int);
void ParseConfigFile();
bool LegalDstIp(SOCKReq);
vector<string> SplitWithSpace(const string&);
vector<string> &SplitIp(const string&, char, vector<string>&);

vector<string> &SplitIp(const string &s, char delim, vector<string> &elems) {
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


vector<string> SplitWithSpace(const string &source)
{
    stringstream ss(source);
    vector<string> vec( (istream_iterator<string>(ss)), istream_iterator<string>() );
    return vec;
}

/* 判斷是否為合法IP (FireWall) */
bool LegalDstIp(SOCKReq sockreq)
{
    bool legal = false ;
    if(sockreq.mode == 1)
    {
        if(CMode_permitIp.size() < 1) return  true;
        
        vector<IPFormat>::iterator it = CMode_permitIp.begin();
        
        
        while(it != CMode_permitIp.end())
        {
            int count = 0 ;
            /* 分別比較四個區段 , 當遇到'*' , conti . */
            while(count < 4)
            {
                if((*it).ip[count] == "*") count ++ ;
                else
                {
                    if( atoi((*it).ip[count].c_str()) != sockreq.DST_ip[count] ) break;
                    else count ++ ;
                }
            }
            
            if(count == 4)
            {
                legal = true;
                break;
            }
            
            
            it ++;
        }
        
    }
    else if(sockreq.mode == 2)
    {
        if(BMode_permitIp.size() < 1) return  true;
        
        vector<IPFormat>::iterator it = BMode_permitIp.begin();
        while(it != BMode_permitIp.end())
        {
            int count = 0 ;
            while(count < 4)
            {
                if((*it).ip[count] == "*") count ++ ;
                else
                {
                    if( atoi((*it).ip[count].c_str()) != sockreq.DST_ip[count] ) break;
                    else count ++ ;
                }
            }
            
            if(count == 4)
            {
                legal = true;
                break;
            }
            
            it ++;
        }
    }
    
    return legal;
}


void ParseConfigFile()
{
    int configFileFd = open(CONFIG_FILE,O_RDONLY);
    string line;
    
    dup2(configFileFd,0);  /* Redirect filefd to stdin */
    
    while(getline(cin,line))
    {
        vector<string> token = SplitWithSpace(line);
        
        if(token[0] == "permit")
        {
            
            if(token.string)
            
            /*
            if(token[4] != "-")
            {
                struct IPFormat permitIp;
                vector<string> ipToken ;
                SplitIp(token[4], '.', ipToken);
                
                permitIp.ip[0] = ipToken[0].c_str();
                permitIp.ip[1] = ipToken[1].c_str();
                permitIp.ip[2] = ipToken[2].c_str();
                permitIp.ip[3] = ipToken[3].c_str();
                
                if(token[1] == "c")
                    CMode_permitIp.push_back(permitIp);
                else if(token[1] == "b")
                    BMode_permitIp.push_back(permitIp);
            }
            */
        }
        
    }
    
    close(0);
    dup(stdinfd);
    
}

/* ssock : Client(SRC)端 <-> SOCKS Server , hsock : SOCKS Server <-> Server(DST)端 */
void RedirectDataFlow(int ssock , int hsock)
{
    fd_set rfds, afds;
    int nfds;
    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(ssock, &afds);
    FD_SET(hsock, &afds);
    char buffer[MAX_BUFFER_SIZE];
    int msgLen ;
    
    while(1)
    {
        memcpy(&rfds, &afds, sizeof(rfds));
        if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0)
        {
            perror("server: select error\n");
            printf("%d\n", errno);
            exit(-1);
        }
        
        /* ssock -> hsock*/
        if (FD_ISSET(ssock, &rfds))
        {
            msgLen = read(ssock,buffer,sizeof(buffer));
            if(msgLen < 0)
            {
                perror("Redirect : read from client error!");
                exit(-1);
            }
            else if(msgLen == 0)            /* ssock close */
                break;
            else
                write(hsock, buffer, msgLen);
            
        }
        
        /* hsock -> ssock */
        if (FD_ISSET(hsock, &rfds))
        {
            msgLen = read(hsock,buffer,sizeof(buffer));
            if(msgLen < 0)
            {
                perror("Redirect : read from client error!");
                exit(-1);
            }
            else if(msgLen == 0)            /* hsock close */
                break;
            else
                write(ssock, buffer, msgLen);
            
        }
        
    }
}

void PrintInfo(SOCKReq sockreq)
{
    string cmd ;
    if(sockreq.mode ==1) cmd = "CONNECT";
    else cmd = "BIND";
    
    fprintf(stderr, "SRC Ip :%s\n",sockreq.SRC_ip.c_str());
    fprintf(stderr, "SRC Port :%d\n",sockreq.SRC_port);
    fprintf(stderr, "DST Ip :%s\n",ChainIp(sockreq).c_str());
    fprintf(stderr, "DST Port :%d\n",sockreq.DST_port);
    fprintf(stderr, "Command :%s\n",cmd.c_str());
    
    fflush(stderr);
    
}

/* 將IP addr 串成 "XXX.XXX.XXX.XXX" */
string ChainIp(SOCKReq sockreq)
{
    string res = "";
    for(int i = 0 ; i < 4 ; i ++)
    {
        res += to_string(sockreq.DST_ip[i]);
        if(i == 3) break;
        res += ".";
    }
    return res;
}

/* 寄送Reply(類似ACK)回去給Client端 */
void SOCKSReply(int ssock,SOCKReq sockreq)
{
    unsigned char package[8];
    //dup2(ssock,1);
    
    package[0] = 0;
    package[1] = 90 + sockreq.replyMode;
    package[2] = sockreq.DST_port / 256;
    package[3] = sockreq.DST_port % 256;
    
    // ip = ip in SOCKS4_REQUEST for connect mode
    // ip = 0 for bind mode
    if(sockreq.mode == 1)
    {
        package[4] = sockreq.DST_ip[0] >> 24;
        package[5] = (sockreq.DST_ip[1] >> 16) & 0xFF;
        package[6] = (sockreq.DST_ip[2] >> 8)  & 0xFF;
        package[7] = sockreq.DST_ip[3] & 0xFF;
    }
    else
    {
        package[4] = 0 ;
        package[5] = 0 ;
        package[6] = 0 ;
        package[7] = 0 ;
    }
    
    write(ssock,package,8);
    
    // why 不行 ？  Ans : usigned char(byte) 不適合
    /*
     cout << package ;
     fflush(stdout);
     close(1);
     dup(stdoutfd);
     */
}

void ProcessConnection(int ssock,SOCKReq &sockreq)
{
    
    PrintInfo(sockreq);
    
    if(sockreq.mode == 1)   /* CONNECT Mode */
    {
        int hsock ;
        
        if((hsock = ConnectTCP(ChainIp(sockreq), sockreq.DST_port)) < 0)
        {
            sockreq.replyMode = 1 ;
            SOCKSReply(ssock, sockreq);
            fprintf(stderr, "SOCKS Connection Reject .. Connect to DST error!\n");
            fflush(stderr);
            exit(0);
        }
        else
        {
            SOCKSReply(ssock, sockreq);
            fprintf(stderr, "SOCKS Connection Accept !!..\n");
            RedirectDataFlow(ssock , hsock);
            
            close(hsock);
            close(ssock);
            exit(0);
        }
        
    }
    else if(sockreq.mode == 2)  /* BIND Mode */
    {
        int b_msock , b_hsock ;
        
        /* 建立一個sock , 給Server連進來 */
        srand(time(NULL));
        int port = 10000 + rand() % 50 ;        /* Randomly generate a port # */
        struct sockaddr_in b_serveraddr ;
        socklen_t b_serverLen ;
        
        sockreq.replyMode = 0 ;
        sockreq.DST_port = port ;
        
        b_msock = PassiveTCP(port) ;
        
        /* First Reply */
        SOCKSReply(ssock, sockreq);
        
        
        b_serverLen = sizeof(b_serveraddr);
        
        if( (b_hsock = accept(b_msock, (struct sockaddr*)&b_serveraddr, &b_serverLen)) < 0 )
        {
            fprintf(stderr, "SOCKS Connection Reject .. accept server sock error!\n");
            exit(0);
        }
        /* Server 連進來後 , Second Reply */
        SOCKSReply(ssock, sockreq);
        
        fprintf(stderr, "SOCKS Connection Accept !!..\n");
        RedirectDataFlow(ssock , b_hsock);
        
        close(b_hsock);
        close(b_msock);
        close(ssock);
        exit(0);
    }
    
}

void ParseSOCKSReq(int ssock,SOCKReq &sockReq)
{
    unsigned char ReqStr[MAX_BUFFER_SIZE];
    unsigned char VN , CD ;
    unsigned int DST_port ;
    string DST_ip = "";
    
    if(read(ssock,ReqStr,sizeof(ReqStr)) < 0)
    {
        fprintf(stderr, "Read SOCKS req error!\n");
        exit(-1);
    }
    
    VN = ReqStr[0] ;
    CD = ReqStr[1] ;
    DST_port = ReqStr[2] << 8 | ReqStr[3] ;
    
    sockReq.mode = CD ;
    sockReq.DST_port = DST_port;
    sockReq.DST_ip[0] = ReqStr[4];
    sockReq.DST_ip[1] = ReqStr[5];
    sockReq.DST_ip[2] = ReqStr[6];
    sockReq.DST_ip[3] = ReqStr[7];
    sockReq.replyMode = 0 ;
    
    
}

/*
 *  For Connect to Server , return the ssock
 *      arg : ip(format : XXX.XXX.XXX.XXX or hostname)
 *            port(unsigned short)
 */
int ConnectTCP(string ip, unsigned port)
{
    int    client_fd;
    struct sockaddr_in client_sin;
    struct hostent *he;
    
    if((he = gethostbyname(ip.c_str())) == NULL){
        cout << "Error: get host by name ! " << endl;
        exit(1);
    }
    
    client_fd = socket(AF_INET, SOCK_STREAM ,0);
    bzero(&client_sin, sizeof(client_sin));
    client_sin.sin_family = AF_INET;
    client_sin.sin_addr = *((struct in_addr *)he -> h_addr);
    client_sin.sin_port = htons((u_short)port);
    
    if (connect(client_fd, (struct sockaddr *)&client_sin, sizeof(client_sin)) == -1)
        return -1;
    return client_fd;
}

/* For Listen to Client , return the msock */
int PassiveTCP(unsigned port)
{
    struct sockaddr_in serv_addr;
    int sockfd;
    
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Error: can't open stream socket\n");
        return -1;
    }
    
    bzero((char*) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    
    if (::bind(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Error: can't bind local address\n");
        return -1;
    }
    
    if (listen(sockfd, 5) < 0){
        perror("Error: listen failed\n");
        exit(-1);
    }
    return sockfd;
}

int main(int argc, const char * argv[]) {
    // insert code here...
    
    if(argc != 2)
    {
        cout << "Usage: ./SOCKS-server name] [port-number]!" << endl;
        return 0;
    }
    
    struct sockaddr_in cli_addr;
    socklen_t clilen;
    int ssock ;
    int childpid ;
    int PORT = atoi(argv[1]);
    int msock = PassiveTCP(PORT);
    
    stdinfd = dup(0); stdoutfd = dup(1);
    
    signal(SIGCHLD, SIG_IGN); // kill zombie
    ParseConfigFile();
    
    while(1)
    {
        clilen = sizeof(cli_addr);
        ssock = accept(msock, (struct sockaddr*)&cli_addr, &clilen);
        
        if (ssock < 0)
        {
            perror("server cannot accept\n");
            return -1;
        }
        
        if ((childpid = fork()) < 0)
        {
            perror("server fork error\n");
            return -1;
        }
        else if (childpid == 0)
        {
            close(msock);
            
            SOCKReq socksReq ;
            socksReq.SRC_ip = string(inet_ntoa(cli_addr.sin_addr));
            socksReq.SRC_port = cli_addr.sin_port;
            
            ParseSOCKSReq(ssock, socksReq);
            if(LegalDstIp(socksReq)) ProcessConnection(ssock,socksReq);
            else
            {
                fprintf(stderr, "An illegal connection from %s\n" , (ChainIp(socksReq)).c_str());
            }
            
            exit(0);
        }
        else
        {
            close(ssock);
        }
        
        
    }
    
    
    return 0;
}
