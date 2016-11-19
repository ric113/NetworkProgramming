//
//  main.cpp
//  NP_HW01
//
//  Created by Ricky on 2016/10/16.
//  Copyright © 2016年 Ricky. All rights reserved.
//
//  NPHW02 - Single Process multiple sock version :

// yell tell name 若沒下一個參數 : unknow cmd


#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <sstream>
#include <errno.h>
#include <signal.h>
#include <iterator>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <arpa/inet.h>
#include <iomanip>

#define BUFSIZE 4096
#define MAX_CLIENT_NUM 30

using namespace std;

// Enviroment Variable .
struct EnvVar{
    string name ;
    string value ;
};

// Name Pipe Infomation .
struct ClientTranInfo
{
    int TargetClientFd;
    int SourceClientFd;
};

// Client Information .
struct ClientData{
    int fdNum;
    int uid;
    string name;
    string ip;
    unsigned short port;
    string sendMsg;
    vector<ClientTranInfo> sendTable;   // 紀錄Name Pipe out的收件人 .
    vector<ClientTranInfo> recvTable;   // 紀錄Name Pipe in的寄件人 .
    vector<EnvVar> envVarTable;
};


// Pipe .
struct pipeFd{
    int EnterPipe ;     // 入口 .
    int OutPipe ;       // 出口 .
    int count ;         // 記錄須pipe到往後第幾個cmd .
    bool toNext ;       // 紀錄是否是單純的Pipe , 即沒有數字的Pipe .
    int ownerClientFd ; // 紀錄該Pipe得Owner Client .
};

// 當下cmd之Sign .
enum Sign
{
    PIPE,
    ERRPIPE,
    BRACKET,
    PIPE_ERRPIPE,
    SEND_BRACKET,
    NONE
};

/* Global Variables */

fd_set afds;                    /* active file descriptor set */
int nfds;                       /* Max fd Number */
int msock;                      /* Master sock , listen client connection */
vector<pipeFd> pipeTable;       /* Keep Pipes Info . */
vector<ClientData> clientTable; /* Keep Active client . */
int uidTable[MAX_CLIENT_NUM+1]; /* Keep Active client's Uid . */


void PrintWelcome(int);
int RunShell(int,string,vector<ClientData>::iterator);
void ParseCmd(vector<string>& , vector<string>::iterator& , vector<string>& , string& , int& ,int&, Sign&,bool&,int&,int&,bool&);
vector<string> SplitWithSpace(const string&);
void SubPipeCount (vector<pipeFd>&,vector<ClientData>::iterator);
bool LegalCmd(string,vector<string>);
void EraseNonUsePipe(vector<pipeFd>&,vector<ClientData>::iterator);
bool IsBuildIn(string);
void PipeToSameSetting(vector<pipeFd>&,int&,int&,bool&,vector<ClientData>::iterator);
bool HasInPipe(vector<pipeFd>,vector<ClientData>::iterator);
int RunCmd(int,int,int,int,string,vector<string>,vector<string>);
void CreateNewPipe(int ,int&, vector<pipeFd>&,bool,vector<ClientData>::iterator);
void StdInSetting(int& ,bool&, vector<pipeFd>,vector<ClientData>::iterator);
void ArgtableReset(vector<string>&);
char** TranVecToCharArr(vector<string>,string);
vector<string> &SplitPath(const string&, char, vector<string>&);

void BroadCast(int,string,vector<ClientData>::iterator,int);
void Who(vector<ClientData>::iterator);
void Tell(string,int,vector<ClientData>::iterator);
void Name(string,vector<ClientData>::iterator);
bool IsInSendTable(int,vector<ClientData>::iterator);
bool IsInRecvTable(int,vector<ClientData>::iterator);
string StringArgTable(vector<string>,int);
void EraseExitClient(vector<ClientData>::iterator);
vector<ClientData>::iterator GetClientByFd(int);
string GetEnvValue(string,vector<ClientData>::iterator);
void EraseEnv(string,vector<ClientData>::iterator);
vector<ClientData>::iterator GetClientByUid(int uid);
int AssignId();
bool HasClient(int);
bool MsgCmd(string);
int PrepareNamePipeIn(int, vector<ClientData>::iterator,vector<string>&,string&,string,bool&);

int PrepareNamePipeIn(int recvNum, vector<ClientData>::iterator currentClientIt,vector<string> &arg_table,string &removefilename,string input,bool &recvSucess)
{
    if(!HasClient(recvNum))
    {
        cout <<"*** Error: user #"+to_string(recvNum)+" does not exist yet. ***" << endl;
        cout << "% ";
        fflush(stdout);
        return 0;
    }
    
    if(!IsInRecvTable(recvNum, currentClientIt))
    {
        cout <<"*** Error: the pipe #"+to_string(recvNum)+"->#"+to_string((*currentClientIt).uid)+" does not exist yet. ***"  << endl;
        cout << "% ";
        fflush(stdout);
        return 0;
    }
    else
    {
        string recvfilename = to_string(recvNum)+"2"+to_string((*currentClientIt).uid);
        vector<ClientData>::iterator senderClientIt = GetClientByUid(recvNum);
        recvfilename = "../"+recvfilename; //
        arg_table.push_back(recvfilename); /* Push file name into arg. table , for exec()'s arg .*/
        
        vector<ClientTranInfo>::iterator itr = (*currentClientIt).recvTable.begin();
        while(itr != (*currentClientIt).recvTable.end())
        {
            if((*itr).SourceClientFd == recvNum)
            {
                (*currentClientIt).recvTable.erase(itr);
                break;
            }
            itr ++;
        }
        
        vector<ClientTranInfo>::iterator its = (*senderClientIt).sendTable.begin();
        while(its != (*senderClientIt).sendTable.end())
        {
            if((*its).TargetClientFd == (*currentClientIt).uid)
            {
                (*senderClientIt).sendTable.erase(its);
                break;
            }
            its ++;
        }
        removefilename = recvfilename;
        BroadCast(5, input, currentClientIt, recvNum);
        recvSucess = true;
        
    }
    return 1;
}


/* Wether a cmd which format is : [cmd] [message] */
bool MsgCmd(string cmd)
{
    if(cmd == "yell" || cmd == "tell" || cmd == "name")
        return true;
    return false;
}

bool HasClient(int uid)
{
    if(uidTable[uid] != 0)
        return true;
    return false;
}

int AssignId()
{
    for(int i=1 ; i < MAX_CLIENT_NUM+1 ; i ++)
    {
        if(uidTable[i] == 0)
        {
            uidTable[i] = 1 ;
            return i;
        }
    }
    return 0;
}

/* Erase env. variable info. in env. Table */
void EraseEnv(string envname,vector<ClientData>::iterator currentClient)
{
    vector<EnvVar>::iterator it = (*currentClient).envVarTable.begin();
    while(it != (*currentClient).envVarTable.end())
    {
        if(envname == (*it).name)
        {
            (*currentClient).envVarTable.erase(it);
            return;
        }
        it ++;
    }
}

string GetEnvValue(string envname,vector<ClientData>::iterator currentClient)
{
    vector<EnvVar>::iterator it = (*currentClient).envVarTable.begin();
    while(it != (*currentClient).envVarTable.end())
    {
        if(envname == (*it).name)
            return (*it).value;
        it ++;
    }
    return "";
}


vector<ClientData>::iterator GetClientByFd(int fd)
{
    vector<ClientData>::iterator it = clientTable.begin();
    while( it != clientTable.end())
    {
        if((*it).fdNum == fd)
            return  it;
        it ++;
    }
    return it;
}

vector<ClientData>::iterator GetClientByUid(int uid)
{
    vector<ClientData>::iterator it = clientTable.begin();
    while( it != clientTable.end())
    {
        if((*it).uid == uid)
            return  it;
        it ++;
    }
    return it;
}

void EraseExitClient(vector<ClientData>::iterator currentClientIt)
{
    clientTable.erase(currentClientIt);
}

/* 將 arg. table 中個元素串接起來 , 用於 yell , tell , name */
string StringArgTable(vector<string> arg_table , int startIndex)
{
    vector<string>::iterator it = arg_table.begin();
    string temp = "";
    while( it != arg_table.end())
    {
        if(startIndex != 0)
            startIndex -- ;
        else
            temp = temp + " " + (*it);
        it ++;
    }
    return temp.substr(1,temp.length()-1);
}


bool IsInSendTable(int receiverfd,vector<ClientData>::iterator sender)
{
    vector<ClientTranInfo>::iterator it = (*sender).sendTable.begin();
    while(it != (*sender).sendTable.end())
    {
        if((*it).TargetClientFd == receiverfd)
            return true;
        it ++;
    }
    return false;
}

bool IsInRecvTable(int senderfd,vector<ClientData>::iterator receiver)
{
    vector<ClientTranInfo>::iterator it = (*receiver).recvTable.begin();
    while(it != (*receiver).recvTable.end())
    {
        if((*it).SourceClientFd == senderfd)
            return true;
        it ++;
    }
    return false;
}

void Who(vector<ClientData>::iterator currentClient)
{
    cout <<left<<setw(10)<< "<ID>" << left << setw(20) << "<nickname>" << left <<setw(20)<<"<IP/port>"<<left<<setw(6)<<"\t<indicate me>" << endl;
    
    for (int i = 1 ; i < MAX_CLIENT_NUM+1 ; i ++)
    {
        if(uidTable[i] != 0)
        {
            vector<ClientData>::iterator it = GetClientByUid(i);
            cout <<left<<setw(10)<<(*it).uid <<left<<setw(20)<<(*it).name<<(*it).ip<<"/"<<(*it).port;
            fflush(stdout);
            if((*it).fdNum == (*currentClient).fdNum)
                cout << left << setw(6)<<"\t<- me" << endl;
            else
                cout << endl;
        }
    }
}

void Tell(string msg,int targetFd , vector<ClientData>::iterator currentClient)
{
    
    if(HasClient(targetFd))
    {
        msg =  "*** "+(*currentClient).name+" told you ***:  "+ msg +"\n";
        vector<ClientData>::iterator it = GetClientByUid(targetFd);
        write((*it).fdNum, msg.c_str(), msg.length());
    }
    else
        cout << "*** Error: user #"+to_string(targetFd)+" does not exist yet. ***"<< endl;
}


void Name(string name,vector<ClientData>::iterator currentClientIt)
{
    bool hasSameName = false;
    vector<ClientData>::iterator it = clientTable.begin();
    while( it != clientTable.end())
    {
        if((*it).name == name)
        {
            hasSameName = true;
            break;
        }
        
        it ++;
    }
    
    if(hasSameName)
    {
        cout << "*** User '"+name+"' already exists. ***" << endl;
    }
    else{
        (*currentClientIt).name = name;
        BroadCast(1, "", currentClientIt,-1);
    }
}

/* Parameters :
 *  action - 0 : yell ; 1 : name  ; 2 : enter ; 3 : leave
 *           4 : send (client to client) ;  5 : receive (client to client)
 *  msg - message for 'tell','yell' .
 *  targetfd - sender/receiver fd (for '>' , '<') .
 */
void BroadCast(int action,string msg,vector<ClientData>::iterator currentClient,int targetfd)
{
    int fd ;
    string temp ;
    vector<ClientData>::iterator targetClient ;
    for(fd = 0 ; fd < nfds ; fd ++)
    {
        temp = "";
        if (FD_ISSET(fd, &afds))
        {
            if (fd != msock)   /* Don't send to msock */
            {
                
                switch (action) {
                    case 0:
                        temp = "*** "+(*currentClient).name+" yelled ***: " + msg + "\n";
                        break;
                    case 1:
                        temp = "*** User from " +(*currentClient).ip+"/"+to_string((*currentClient).port)+" is named '"+(*currentClient).name+"'. ***\n";
                        break;
                    case 2:
                        temp = "*** User '(no name)' entered from "+(*currentClient).ip+"/"+to_string((*currentClient).port)+". ***\n";
                        break;
                    case 3:
                        temp = "*** User '"+(*currentClient).name+"' left. ***\n";
                        break;
                    case 4:
                        targetClient = GetClientByUid(targetfd);
                        temp = "*** "+(*currentClient).name+" (#"+to_string((*currentClient).uid)+") just piped '"+msg+"' to "+(*targetClient).name+" (#"+to_string((*targetClient).uid)+") ***\n";
                        break;
                    case 5:
                        targetClient= GetClientByUid(targetfd);
                        temp = "*** "+(*currentClient).name+" (#"+to_string((*currentClient).uid)+") just received from "+(*targetClient).name+" (#"+to_string((*targetClient).uid)+") by '"+msg+"' ***\n";
                        break;
                    default:
                        break;
                        
                }
                
                if (write(fd, temp.c_str(), temp.length()) == -1)
                    perror("send");
            }
        }
    }
}

void PrintWelcome(int sockfd)
{
    
    dup2(sockfd,1);
    cout << "****************************************" << endl;
    cout << "** Welcome to the information server. **" << endl;
    cout << "****************************************" << endl;
}


void ParseCmd(vector<string> &after_split_line , vector<string>::iterator &it_line , vector<string> &arg_table , string &cmd , int &pipeNum ,int &pipeErrNum , Sign &sign ,bool &PipeToNext, int &sendNum, int &recvNum,bool &HasNamePipeIn)
{
    char Pnum[5],Enum[5]; // 記錄 | , ! 後的數字 .
    char Snum[5],Rnum[5]; // 記錄 < , > 後的數字 .
    string temp ;
    bool isCmd = true;
    sign = NONE;
    
    /* 當遇到 "|" , "!" , ">" , "\n" , 則跳出 , 並開始執行該 cmd .
     * 若是MsgCmd , 其後均視為arg . e.g. yell ls > test.txt -> [ls > test.txt] 是參數 , 即要送出的訊息 .
     *
     */
    do
    {
        temp = *it_line;
        
        if(temp[0] == '|' && !MsgCmd(cmd))
        {
            sign = PIPE;
            if(temp.length() == 1)
            {
                PipeToNext = true;
                Pnum[0] = '1' ;
            }
            else{
                for(int i = 1 ; i < temp.length() ; i ++)
                {
                    Pnum[i-1] = temp[i];
                    Pnum[i] = '\0';
                }
            }
            
            break;
        }
        else if(temp[0] == '!' && !MsgCmd(cmd))
        {
            sign = ERRPIPE;
            
            if(temp.length() == 1)
            {
                PipeToNext = true;
                Enum[0] = '1' ;
            }
            else
            {
                for(int i = 1 ; i < temp.length() ; i ++)
                {
                    Enum[i-1] = temp[i];
                    Enum[i] = '\0';
                }
            }
            
            break;
        }
        else if(temp[0] == '>' && !MsgCmd(cmd))
        {
            if(temp.length() == 1)
            {
                sign = BRACKET;
                it_line ++;
                while(it_line != after_split_line.end())
                {
                    arg_table.push_back(*it_line);
                    it_line ++ ;
                }
                break;
            }
            else
            {
                sign = SEND_BRACKET;
                
                for(int i = 1 ; i < temp.length() ; i ++)
                {
                    Snum[i-1] = temp[i];
                    Snum[i] = '\0';
                }
                
            }
        }
        else if(temp[0] == '<' && !MsgCmd(cmd))
        {
            HasNamePipeIn = true;
            for(int i = 1 ; i < temp.length() ; i ++)
            {
                Rnum[i-1] = temp[i];
                Rnum[i] = '\0';
            }
            
        }
        else if(isCmd)
        {
            cmd = temp;
            isCmd = false;
        }
        else
        {
            arg_table.push_back(*it_line);
        }
        it_line ++ ;
        
    }while(it_line != after_split_line.end());
    
    if((sign == PIPE || sign == ERRPIPE))
    {
        if((it_line+1) != after_split_line.end()){
            it_line ++;
            temp = *it_line;
            if(temp[0] == '|')
            {
                sign = PIPE_ERRPIPE;
                for(int i = 1 ; i < temp.length() ; i ++)
                {
                    Pnum[i-1] = temp[i];
                    Pnum[i] = '\0';
                }
                it_line ++;
            }
            else if(temp[0] == '!')
            {
                sign = PIPE_ERRPIPE ;
                for(int i = 1 ; i < temp.length() ; i ++)
                {
                    Enum[i-1] = temp[i];
                    Enum[i] = '\0';
                }
                it_line ++;
            }
        }
        else
            it_line ++;
    }
    
    
    sendNum = atoi(Snum);
    recvNum = atoi(Rnum);
    pipeNum = atoi(Pnum);
    pipeErrNum = atoi(Enum);
    
}

void SubPipeCount(vector<pipeFd> &pipeTable,vector<ClientData>::iterator currentClient)
{
    vector<pipeFd>::iterator it = pipeTable.begin();
    
    while(it != pipeTable.end())
    {
        if((*it).ownerClientFd == (*currentClient).fdNum)
            (*it).count -- ;
        it ++;
    }
}


bool LegalCmd(string cmd,vector<string> pathTable)
{
    if(cmd == "printenv" || cmd == "setenv" || cmd == "exit" || cmd == "yell" || cmd =="name" || cmd =="tell" || cmd == "who")
        return true;
    
    // 根據路徑開檔 , 若失敗 , 則代表illegal cmd .
    string path ;
    vector<string>::iterator it = pathTable.begin();
    FILE* fd;
    
    while (it != pathTable.end()) {
        path = (*it);
        path = path + "/" + cmd;
        fd = fopen(path.c_str(), "r");
        if(fd != NULL)
        {
            fclose(fd);
            return true;
        }
        it ++;
    }
    
    return false;
    
}

// 當某個Pipe Count到0時 , 代表該Pipe任務已完成 , 可完全關閉 , 並從Pipe Table中移除 .
void EraseNonUsePipe(vector<pipeFd> &pipeTable , vector<ClientData>::iterator currentClient)
{
    vector<pipeFd>::iterator it = pipeTable.begin();
    while(it != pipeTable.end())
    {
        if((*it).count == 0 && (*it).ownerClientFd == (*currentClient).fdNum)
        {
            close((*it).EnterPipe);
            close((*it).OutPipe);
            pipeTable.erase(it);
        }
        else{
            it ++ ;
        }
    }
    
}

bool IsBuildIn(string cmd)
{
    if(cmd == "printenv" || cmd == "setenv" || cmd == "yell" || cmd == "name" || cmd == "tell" || cmd == "who")
        return true;
    return false;
}

// 判斷是否Pipe到往後同個cmd .
void PipeToSameSetting(vector<pipeFd> &pipeTable , int &pipeNum , int &pipeOut , bool &PipeToSame ,vector<ClientData>::iterator currentClient)
{
    vector<pipeFd>::iterator SamePipe;
    
    PipeToSame = false;
    SamePipe = pipeTable.begin();
    while(SamePipe != pipeTable.end())
    {
        if((*SamePipe).count == pipeNum && (*SamePipe).ownerClientFd == (*currentClient).fdNum)
        {
            pipeOut= (*SamePipe).EnterPipe;
            PipeToSame = true;
            break;
        }
        
        SamePipe ++ ;
    }
}

// 當count數到0時 , 代表Pipe到當下cmd .
bool HasInPipe(vector<pipeFd> pipeTable,vector<ClientData>::iterator currentClient)
{
    vector<pipeFd>::iterator it = pipeTable.begin();
    while(it != pipeTable.end())
    {
        if((*it).count == 0 && (*it).ownerClientFd == (*currentClient).fdNum){
            close((*it).EnterPipe); //  * 記得關  , 否則會以為仍有input
            return  true;
        }
        it ++;
    }
    return false;
}

int RunCmd(int infd,int outfd,int errfd,int sockfd,string cmd,vector<string> argtable,vector<string> pathTable)
{
    
    int child_pid;
    string path ;
    
    char ** arr = TranVecToCharArr(argtable, cmd); // 將 arg_table 轉成 char ** 形式 (exec要用到)
    
    child_pid = fork();
    if(child_pid < 0)
    {
        perror("fork error!");
        return -1;
    }
    else if(child_pid == 0)
    {
        //  * 注意關閉順序 , 因此先一次dup好 , 再關閉 .
        if(infd != sockfd)
            dup2(infd, 0);
        if(outfd != sockfd)
            dup2(outfd, 1);
        if(errfd != sockfd)
            dup2(errfd, 2);
        
        if(infd != sockfd)
            close(infd);
        if(outfd != sockfd)
            close(outfd);
        if(errfd != sockfd)
            close(errfd);
        
        
        vector<string>::iterator it = pathTable.begin();
        
        while (it != pathTable.end()) {
            path = (*it);
            path = path + "/" + cmd;
            if (execv(path.c_str(), arr) == -1)
                it ++;
        }
        perror("exec erro !");
        exit(0);
        
    }
    else
    {
        int status = 0;
        while (waitpid(child_pid, &status, 0) > 0);
        if (status != 0)
        {
            return -1;
        }
    }
    return 0;
    
}

void CreateNewPipe(int pipeNum ,int &enter , vector<pipeFd> &pipeTable,bool PipeToNext,vector<ClientData>::iterator currentClient)
{
    int pipefd[2];
    struct pipeFd pushback;
    
    if(pipe(pipefd) < 0){
        perror("pipe error");
        return ;
    }
    enter = pipefd[1];
    
    pushback.EnterPipe = pipefd[1];
    pushback.OutPipe = pipefd[0];
    pushback.count = pipeNum;
    pushback.toNext = PipeToNext;
    pushback.ownerClientFd = (*currentClient).fdNum;
    pipeTable.push_back(pushback);
    
}

void StdInSetting(int& infd,bool& hasInPipe, vector<pipeFd> pipeTable,vector<ClientData>::iterator currentClient)
{
    
    vector<pipeFd>::iterator inPipe;
    
    hasInPipe = HasInPipe(pipeTable,currentClient);
    if(hasInPipe)
    {
        inPipe = pipeTable.begin();
        while(inPipe!=pipeTable.end())
        {
            if((*inPipe).count == 0 && (*inPipe).ownerClientFd == (*currentClient).fdNum)
                break;
            inPipe ++;
        }
        infd = (*inPipe).OutPipe;
    }
}

void ArgtableReset(vector<string>& arg_table)
{
    arg_table.clear();
    // 完整的 free vector 之 mem space.
    vector<string> free_arg;
    arg_table.swap(free_arg);
}

vector<string> SplitWithSpace(const string &source)
{
    stringstream ss(source);
    vector<string> vec( (istream_iterator<string>(ss)), istream_iterator<string>() );
    return vec;
}


// 將vector轉成char**形式儲存 .
char** TranVecToCharArr(vector<string> arg_table ,string cmd)
{
    
    char ** arr = new char*[arg_table.size()+2];
    arr[0] = new char[cmd.size() + 1];
    strcpy(arr[0], cmd.c_str());
    for(int i = 0; i < arg_table.size(); i++){
        arr[i+1] = new char[arg_table[i].size() + 1];
        strcpy(arr[i+1], arg_table[i].c_str());
    }
    arr[arg_table.size()+1] = NULL;
    
    return arr;
    
}

// 將path(以':'為分割)切割 , 存在vector table內 .
vector<string> &SplitPath(const string &s, char delim, vector<string> &elems) {
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}





////////
// Testing func.
/*
 void printsplit(vector<string> &arg)
 {
 vector<string>::iterator it_i;
 cout << "split table" << endl;
 for(it_i=arg.begin() ; it_i != arg.end() ; it_i ++)
 {
 cout << *it_i << endl;
 }
 cout << "End!" << endl;
 
 }
 
 void printargtable(vector<string> &arg)
 {
 vector<string>::iterator it_i;
 cout << "arg table" << endl;
 for(it_i=arg.begin() ; it_i != arg.end() ; it_i ++)
 {
 cout << *it_i << endl;
 }
 cout << "End!" << endl;
 
 }
 
 void printpipetable(vector<pipeFd> &pt)
 {
 vector<pipeFd>::iterator it;
 cout << "pipe Table" << endl;
 for(it=pt.begin() ; it != pt.end() ; it ++)
 {
 cout << "Enterfd = " << (*it).EnterPipe<< endl;
 cout << "Outfd = " << (*it).OutPipe<< endl;
 cout << "Count = " << (*it).count<< endl;
 }
 cout << "end!" <<endl;
 }
 */

////////

int RunShell(int sockfd,string input,vector<ClientData>::iterator currentClientIt)
{
    
    string line;
    string cmd ;
    string path = getenv("PATH");
    string removefilename ;             // Keep receive success file name .
    vector<string> after_split_line;    // Keep cmd afrer split .
    vector<string>::iterator it_line;
    vector<string> pathTable;           // Keep Paths .
    vector<string> arg_table;           // Keep current cmd args .
    
    int pipeNum , pipeErrNum;
    int sendNum , recvNum ;             // Keep >[send FdNum] , <[receive FdNum] .
    int RecvNamePipeStatus ;            // 1 : success , 0 : failure .
    
    bool PipeToNext ;
    bool PipeToSame ;
    bool recvSucess ;
    bool HasNamePipeIn ;
    bool hasInPipe ;
    
    dup2(sockfd , 1);
    dup2(sockfd , 2);                   // both dup sockfd to stdout & stderr .
    
    SplitPath(path,':',pathTable);
    after_split_line = SplitWithSpace(input);
    it_line = after_split_line.begin();
    
    while(it_line != after_split_line.end() && *it_line != "\0")
    {
        /* Initialize */
        int infd = sockfd  , outfd = sockfd , errfd = sockfd;
        PipeToNext = false;
        recvSucess = false;
        HasNamePipeIn = false;
        hasInPipe = false;
        PipeToSame = false;
        Sign sign = NONE;
        
        ParseCmd(after_split_line, it_line, arg_table, cmd, pipeNum, pipeErrNum, sign,PipeToNext,sendNum,recvNum,HasNamePipeIn);
        
        if(!LegalCmd(cmd,pathTable))
        {
            EraseNonUsePipe(pipeTable,currentClientIt);
            ArgtableReset(arg_table);
            cout << "Unknown command: [" << cmd << "]." << endl;
            break;
        }
        
        if(sign == PIPE)
        {
            /* 判斷是否有Name Pipe In , 即從其他Client讀入訊息 */
            if(HasNamePipeIn)
            {
                RecvNamePipeStatus = PrepareNamePipeIn(recvNum, currentClientIt, arg_table, removefilename, input, recvSucess);
                if(RecvNamePipeStatus == 0)
                    return 0;
            }
            
            if(!PipeToNext)
                PipeToSameSetting(pipeTable, pipeNum, outfd, PipeToSame,currentClientIt);
            
            // * 記得 , 先判斷PipeToSame ,看是否要建新Pipe , 再設定StdIn(HasInPipe) . 因為HasInPipe會關fd , 會錯亂!?
            if(!PipeToSame)
                CreateNewPipe(pipeNum, outfd, pipeTable, PipeToNext,currentClientIt);
            
            StdInSetting(infd,hasInPipe, pipeTable,currentClientIt);
            
        }
        else if(sign == ERRPIPE)
        {
            if(HasNamePipeIn)
            {
                RecvNamePipeStatus = PrepareNamePipeIn(recvNum, currentClientIt, arg_table, removefilename, input, recvSucess);
                if(RecvNamePipeStatus == 0)
                    return 0;
            }
            if(!PipeToNext)
                PipeToSameSetting(pipeTable, pipeErrNum, errfd, PipeToSame,currentClientIt);
            
            if(!PipeToSame)
                CreateNewPipe(pipeErrNum, errfd, pipeTable,PipeToNext,currentClientIt);
            
            outfd = errfd ;
            StdInSetting(infd,hasInPipe, pipeTable,currentClientIt);
            
        }
        else if(sign == PIPE_ERRPIPE)
        {
            if(HasNamePipeIn)
            {
                RecvNamePipeStatus = PrepareNamePipeIn(recvNum, currentClientIt, arg_table, removefilename, input, recvSucess);
                if(RecvNamePipeStatus == 0)
                    return 0;
            }
            // for StdOutPipe.
            if(!PipeToNext)
                PipeToSameSetting(pipeTable, pipeNum, outfd, PipeToSame,currentClientIt);
            
            if(!PipeToSame)
                CreateNewPipe(pipeNum, outfd, pipeTable,PipeToNext,currentClientIt);
            
            // for ErrPipe.
            if(!PipeToNext)
                PipeToSameSetting(pipeTable, pipeErrNum, errfd, PipeToSame,currentClientIt);
            
            if(!PipeToSame)
                CreateNewPipe(pipeErrNum, errfd, pipeTable,PipeToNext,currentClientIt);
            
            
            StdInSetting(infd,hasInPipe, pipeTable,currentClientIt);
            
        }
        else if (sign == BRACKET)
        {
            string targetfile = arg_table[arg_table.size()-1];
            FILE * outfile;
            outfile = fopen(targetfile.c_str(), "w");
            if(outfile == NULL)
            {
                perror("Open file error!");
                return 0;
            }
            outfd = fileno(outfile);
            arg_table.pop_back();
            
            /* 判斷是Name Pipe In 還是 Normal Pipe In */
            if(HasNamePipeIn)
            {
                RecvNamePipeStatus = PrepareNamePipeIn(recvNum, currentClientIt, arg_table, removefilename, input, recvSucess);
                if(RecvNamePipeStatus == 0)
                    return 0;
            }
            else
                StdInSetting(infd,hasInPipe,pipeTable,currentClientIt);
            
            
            
        }
        else if(sign == SEND_BRACKET)
        {
            
            if(!HasClient(sendNum))
            {
                cout <<"*** Error: user #"+to_string(sendNum)+" does not exist yet. ***" << endl;
                cout << "% ";
                fflush(stdout);
                return 0;
            }
            
            
            if(IsInSendTable(sendNum, currentClientIt))
            {
                cout << "*** Error: the pipe #"+to_string((*currentClientIt).uid)+"->#"+to_string(sendNum)+" already exists. ***" <<endl;
                cout << "% ";
                fflush(stdout);
                return 0;
            }
            else
            {
                chdir("/net/gcs/105/0556087/NPHW02");
                FILE * sendfile;
                string sendfilename = to_string((*currentClientIt).uid)+"2"+to_string(sendNum);
                sendfile = fopen(sendfilename.c_str(), "w");
                if(sendfile == NULL)
                {
                    perror("Open file error!");
                    chdir("/net/gcs/105/0556087/NPHW02/spc");
                    return 0;
                }
                outfd = fileno(sendfile);
                if(HasNamePipeIn)
                    PrepareNamePipeIn(recvNum, currentClientIt, arg_table, removefilename, input, recvSucess);
                else
                    StdInSetting(infd,hasInPipe,pipeTable,currentClientIt);
                
                ClientTranInfo temp ;
                temp.SourceClientFd = (*currentClientIt).uid;
                temp.TargetClientFd = sendNum;
                
                vector<ClientData>::iterator receiverClientIt = GetClientByUid(sendNum);
                
                // Update ClientTable
                (*currentClientIt).sendTable.push_back(temp);
                (*receiverClientIt).recvTable.push_back(temp);
                BroadCast(4, input, currentClientIt, sendNum);
                chdir("/net/gcs/105/0556087/NPHW02/spc");
            }
        }
        else
        {
            
            if(IsBuildIn(cmd))
            {
                // Process builtin
                if(cmd == "printenv")
                {
                    string envvalue = GetEnvValue(arg_table[0], currentClientIt);
                    cout << arg_table[0] << "=" << envvalue << endl;
                }
                else if(cmd == "setenv")
                {
                    
                    EraseEnv(arg_table[0], currentClientIt);
                    struct EnvVar temp;
                    temp.name = arg_table[0];
                    temp.value = arg_table[1];
                    (*currentClientIt).envVarTable.push_back(temp);
                    
                    setenv(arg_table[0].c_str(), arg_table[1].c_str(), 1);
                    if(arg_table[0] == "PATH")
                    {
                        path = arg_table[1] ;
                        pathTable.clear() ;
                        SplitPath(path,':',pathTable);
                    }
                }
                else if(cmd == "yell")
                {
                    string yellMsg = StringArgTable(arg_table, 0);
                    BroadCast(0,yellMsg,currentClientIt,-1);
                }
                else if(cmd =="name")
                {
                    string nameMsg = StringArgTable(arg_table, 0);
                    Name(nameMsg, currentClientIt);
                }
                else if(cmd == "tell")
                {
                    string tellMsg = StringArgTable(arg_table, 1);
                    Tell(tellMsg,stoi(arg_table[0]),currentClientIt);
                }
                else if(cmd == "who")
                {
                    Who(currentClientIt);
                }
                
                EraseNonUsePipe(pipeTable,currentClientIt);
                ArgtableReset(arg_table);
                break;
            }
            
            if(cmd == "exit")
                return -1;
            
            if(HasNamePipeIn)
            {
                RecvNamePipeStatus = PrepareNamePipeIn(recvNum, currentClientIt, arg_table, removefilename, input, recvSucess);
                if(RecvNamePipeStatus == 0)
                    return 0;
            }
            else
                StdInSetting(infd,hasInPipe, pipeTable,currentClientIt);
            
        }
        
        int status = RunCmd(infd, outfd, errfd,sockfd, cmd, arg_table, pathTable);
        
        if(recvSucess)
            remove(removefilename.c_str());
        if(status == -1)
            return 0;
        if(hasInPipe)
            close(infd); // 該Pipe已完成任務,關閉出口(即完全關閉,入口已在HasInPipe()時關閉).
        
        //fclose(outfile);
        
        EraseNonUsePipe(pipeTable,currentClientIt);
        if(PipeToNext)
        {
            vector<pipeFd>::iterator it = pipeTable.begin();
            while(it != pipeTable.end())
            {
                if((*it).toNext == true)
                    (*it).count -- ;
                it++;
            }
        }
        
        
        ArgtableReset(arg_table);
    }
    
    SubPipeCount(pipeTable,currentClientIt);
    ArgtableReset(arg_table);
    cout << "% " ;    /* endl 等於 cout << "XXX\n" + fflush(stdout) ; */
    fflush(stdout);
    return 0;
    
}


int main(int argc, const char **argv) {
    // insert code here...
    
    fd_set rfds; /* read file descriptor set */
    struct sockaddr_in cli_addr,serv_addr;
    socklen_t clilen;
    int PORT = atoi(argv[1]);
    
    vector<ClientData>::iterator currentClientIt ;
    
    if((msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)  // 建立listen new client 專用的 sockfd .
        perror("server erro!");
    
    bzero((char*)&serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY : 可接受任何Client .
    serv_addr.sin_port = htons(PORT);
    
    // bind 前須加 '::' , 否則會和std的bind搞混 .
    if(::bind(msock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <0) // bind sock & port .
        perror("bind error");
    
    listen(msock, 5);           // listen client .
    signal(SIGCHLD, SIG_IGN);   // kill zombie  .
    nfds = getdtablesize();     // get fdtable size .
    FD_ZERO(&afds);
    FD_SET(msock, &afds);       // 在msock位置上打勾 -> Keep tracking 是否有client連入 .
    
    chdir("/net/gcs/105/0556087/NPHW02/spc"); // 改變執行時目錄 , demo時使用 .
    
    for(;;)
    {
        memcpy(&rfds, &afds, sizeof(rfds));
        int i;
        int ssock;
        
        /* 會停留在此行 , 除非Listen到有Client訊息(e.g. accept() , read() , close()  ...etc. )進入 */
        if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0,(struct timeval *)0) < 0)
            perror("select error!");
        
        for(i = 0; i < nfds; i++)
        {
            if (FD_ISSET(i, &rfds))
            {
                if (i == msock)
                {
                    // handle new connections
                    clilen = sizeof(cli_addr);
                    ssock = accept(msock, (struct sockaddr*)&cli_addr, &clilen);
                    
                    if (ssock == -1)
                        perror("accept");
                    else
                    {
                        /* New client in , set Info. */
                        struct ClientData temp ;
                        temp.fdNum = ssock;
                        temp.name = "(no name)";
                        temp.ip = inet_ntoa(cli_addr.sin_addr);
                        temp.port = ntohs(cli_addr.sin_port);
                        temp.sendMsg = "";
                        temp.uid = AssignId();
                        struct EnvVar tempEnv;
                        tempEnv.name = "PATH";
                        tempEnv.value = "bin:.";
                        temp.envVarTable.push_back(tempEnv);
                        
                        clientTable.push_back(temp);    // 會加在最後面 .
                        FD_SET(ssock, &afds);           // 新增到 master set
                        
                        PrintWelcome(ssock);
                        BroadCast(2,"",(--clientTable.end()),-1);
                        
                        send(ssock, "% ", 2,0);
                    }
                    
                }
                else
                {
                    // 處理來自 client 的資料
                    int nbytes;
                    char buf[BUFSIZE];
                    
                    if ((nbytes = recv(i, buf, sizeof(buf),0)) <= 0)
                    {
                        // got error or connection closed by client
                        if (nbytes == 0)
                        {
                            // 關閉連線
                            printf("selectserver: socket %d hung up\n", i);
                        }
                        else
                            perror("recv");
                        
                        BroadCast(3,"",currentClientIt,-1);
                        EraseExitClient( currentClientIt);
                        uidTable[(*currentClientIt).uid] = 0;
                        close(i);
                        FD_CLR(i, &afds); // 從 master set 中移除
                        
                    }
                    else
                    {
                        // 我們從 client 收到一些資料
                        string input;
                        currentClientIt = GetClientByFd(i);
                        // if currentClientIt == clientTable.end() => No this client!
                        
                        buf[nbytes-1] = '\0';
                        input = buf;
                        string path = GetEnvValue("PATH", currentClientIt);
                        setenv("PATH",path.c_str(),1);
                        
                        int status = RunShell(i,input,currentClientIt);
                        if(status == -1)
                        {
                            BroadCast(3,"",currentClientIt,-1);
                            uidTable[(*currentClientIt).uid] = 0;
                            EraseExitClient(currentClientIt);
                            
                            close(i);
                            close(1);  // 因為dup到stdout , 也要關 !! 否則 sock 仍存在 !
                            close(2);  // 同上
                            
                            dup2(0,1); // 先佔住 1 , 否則到時ssock會建立在1 !!
                            dup2(0,2); // 同上 .
                            
                            FD_CLR(i, &afds); // 從 master set 中移除
                        }
                    }
                }
            }
        }
    }
    
    
    
    return 0;
}
