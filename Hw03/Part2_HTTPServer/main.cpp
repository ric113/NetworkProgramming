//
//  main.cpp
//  NPHW03_HTTPServer
//
//  Created by Ricky on 2016/11/27.
//

#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

using namespace std;

/* Keep HTTP Request from client browser */
struct Request
{
    string method;
    string file;
    string args;
    string protocol;
    string fileformat;
};

void ParseRequest(char*,Request&);
void SetHttpEnvVar(Request);
void ProcessRequest(int,Request);

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

void SetHttpEnvVar(Request request)
{
    setenv("QUERY_STRING",(request.args).c_str(), 1);
    setenv("REQUEST_METHOD", (request.method).c_str(), 1);
    setenv("SCRIPT_NAME", (request.file).c_str(), 1);
    setenv("REMOTE_ADDR", "140.113.185.117", 1);
    setenv("REMOTE_HOST", " ", 1);
    setenv("AUTH_TYPE", " ", 1);
    setenv("REMOTE_USER", " ", 1);
    setenv("CONTENT_LENGTH", " ", 1);
    setenv("REMOTE_IDENT", " ", 1);
}


void ProcessRequest(int ssock , Request request)
{
    string path = "/net/gcs/105/0556087/public_html";
    path += request.file;
    
    /* Dup ssock to stdin/out/err */
    dup2(ssock, 0);
    dup2(ssock, 1);
    dup2(ssock, 2);
    
    /* Http Ack */
    cout << "HTTP/1.0 200 OK\r\n" ;
    cout << "Content-Type: text/html\r\n" ;
    //cout << "\r\n" ;
    fflush(stdout);
    
    /* 執行.cgi fil */
    if(request.fileformat == "cgi")
    {
        int childpid = fork();
        if(childpid == 0)
        {
            if(execv(path.c_str(), NULL) == -1)
            {
                perror("Execv error!");
                exit(-1);
            }
        }
        else if(childpid > 0)
        {
            int status = 0;
            while (waitpid(childpid, &status, 0) > 0);
            if (status != 0)
            {
                exit(-1);
            }
        }
        else
        {
            perror("Exec CGI error!");
            exit(-1);
        }
            
    }
    /* 回傳 .html file 內容至Client browser */
    else if(request.fileformat == "html" || request.fileformat == "htm" )
    {
        
        int htmlFilefd = open(path.c_str(),O_RDONLY);
        if(htmlFilefd < 0)
        {
            perror("Html file open error!");
            exit(-1);
        }
        dup2(htmlFilefd, 0);
        string htmlText ="" ;
        string str ;
        while(getline(cin, str))
        {
            htmlText += str + "\n";       // Need "\n" !
         
        }
        
        cout << htmlText;
        fflush(stdout);
        close(htmlFilefd);
        
    }

}

int main(int argc, const char * argv[]) {
    // insert code here...
    
    if(argc != 2)
    {
        cout << "Usage: ./[http-server name] [port-number]!" << endl;
        return 0;
    }
    
    int msock,ssock,childpid;
    struct sockaddr_in cli_addr,serv_addr;
    socklen_t clilen;
    int PORT = atoi(argv[1]);
    
    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)  // 建立sockfd .
        perror("server erro!");
    
    bzero((char*)&serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY : 可接受任何Client .
    serv_addr.sin_port = htons(PORT);
    
    // bind 前須加 '::' , 否則會和std的bind搞混 .
    if(::bind(msock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <0) // bind sock & port .
        perror("bind error");
    
    listen(msock, 5); // listen client .
    signal(SIGCHLD, SIG_IGN); // kill zombie
    
    while(1)
    {
        clilen = sizeof(cli_addr);
        ssock = accept(msock, (struct sockaddr*)&cli_addr, &clilen); // accept , 並建立新fd來服務該client .
        
        if (ssock < 0)
        {
            perror("server cannot accept");
            return -1;
        }
        
        if ((childpid = fork()) < 0)
        {
            perror("server fork error");
            return -1;
        }
        else if (childpid == 0)
        {
            close(msock);
            char requestString[1000];
            
            int len = read(ssock, requestString, sizeof(requestString)) ;
            
            if(len <= 0)
                perror("Read request error!");
            else
            {
                requestString[len] = '\0';
                Request request;
                
                ParseRequest(requestString,request);
                SetHttpEnvVar(request);
                ProcessRequest(ssock, request);
                
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
