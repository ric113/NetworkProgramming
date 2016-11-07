//
//  main.cpp
//  NP_HW01
//
//  Created by Ricky on 2016/10/16.
//  Copyright © 2016年 Ricky. All rights reserved.
//
// 修正 ： |n 是pipe到n行後 , 不是pipe到n個指令後

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

using namespace std;

// Pipe .
struct pipeFd{
    int EnterPipe ; // 入口
    int OutPipe ;   // 出口
    int count ;     // 記錄須pipe到往後第幾個cmd
    bool toNext ;
};

// 當下cmd之Sign .
enum Sign
{
    PIPE,
    ERRPIPE,
    BRACKET,
    PIPE_ERRPIPE,
    NONE
};

void PrintWelcome(int);
void RunShell(int);
void ParseCmd(vector<string>& , vector<string>::iterator& , vector<string>& , string& , int& ,int&, Sign&, bool&);
vector<string> SplitWithSpace(const string&);
void SubPipeCount (vector<pipeFd>&);
bool LegalCmd(string,vector<string>);
void EraseNonUsePipe(vector<pipeFd>&);
bool IsBuildIn(string);
void PipeToSameSetting(vector<pipeFd>&,int&,int&,bool&);
bool HasInPipe(vector<pipeFd>);
int RunCmd(int,int,int,int,string,vector<string>,vector<string>);
void CreateNewPipe(int ,int&, vector<pipeFd>&,bool);
void StdInSetting(int& ,bool&, vector<pipeFd>);
void ArgtableReset(vector<string>&);
char** TranVecToCharArr(vector<string>,string);
vector<string> &SplitPath(const string&, char, vector<string>&);



void PrintWelcome(int sockfd)
{
    
    cout << "****************************************" << endl;
    cout << "** Welcome to the information server. **" << endl;
    cout << "****************************************" << endl;
}


void ParseCmd(vector<string> &after_split_line , vector<string>::iterator &it_line , vector<string> &arg_table , string &cmd , int &pipeNum ,int &pipeErrNum , Sign &sign, bool &PipeToNext)
{
    char Pnum[5],Enum[5]; // 記錄 | , ! 後的數字 .
    string temp ;
    bool isCmd = true;
    sign = NONE;
    
    // 當遇到 "|" , "!" , ">" , "\n" , 則跳出 , 並開始執行該 cmd .
    do
    {
        temp = *it_line;
        
        if(temp[0] == '|')
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
        else if(temp[0] == '!')
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
        else if(temp[0] == '>')
        {
            sign = BRACKET;
            it_line ++;
            while(it_line != after_split_line.end())
            {
                arg_table.push_back(*it_line);
                it_line ++ ;
            }
            return;
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
    
    
    pipeNum = atoi(Pnum);
    pipeErrNum = atoi(Enum);
    
}

void SubPipeCount(vector<pipeFd> &pipeTable)
{
    vector<pipeFd>::iterator it = pipeTable.begin();
    
    while(it != pipeTable.end())
    {
        (*it).count -- ;
        it ++;
    }
}


bool LegalCmd(string cmd,vector<string> pathTable)
{
    if(cmd == "printenv" || cmd == "setenv" || cmd == "exit")
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
void EraseNonUsePipe(vector<pipeFd> &pipeTable)
{
    vector<pipeFd>::iterator it = pipeTable.begin();
    while(it != pipeTable.end())
    {
        if((*it).count == 0)
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
    if(cmd == "printenv" || cmd == "setenv")
        return true;
    return false;
}

// 判斷是否Pipe到往後同個cmd .
void PipeToSameSetting(vector<pipeFd> &pipeTable , int &pipeNum , int &pipeOut , bool &PipeToSame)
{
    vector<pipeFd>::iterator SamePipe;
    
    PipeToSame = false;
    SamePipe = pipeTable.begin();
    while(SamePipe != pipeTable.end())
    {
        if((*SamePipe).count == pipeNum)
        {
            pipeOut= (*SamePipe).EnterPipe;
            PipeToSame = true;
            break;
        }
        
        SamePipe ++ ;
    }
}

// 當count數到0時 , 代表Pipe到當下cmd .
bool HasInPipe(vector<pipeFd> pipeTable)
{
    vector<pipeFd>::iterator it = pipeTable.begin();
    while(it != pipeTable.end())
    {
        if((*it).count == 0){
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

void CreateNewPipe(int pipeNum ,int &enter , vector<pipeFd> &pipeTable, bool PipeToNext)
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
    pipeTable.push_back(pushback);
    
}

void StdInSetting(int& infd,bool& hasInPipe, vector<pipeFd> pipeTable)
{
    
    vector<pipeFd>::iterator inPipe;
    
    hasInPipe = HasInPipe(pipeTable);
    if(hasInPipe)
    {
        inPipe = pipeTable.begin();
        while(inPipe!=pipeTable.end())
        {
            if((*inPipe).count == 0)
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

void RunShell(int sockfd)
{
    
    string line;
    string path = getenv("PATH");
    vector<string> after_split_line;    // Keep cmd afrer split .
    vector<string>::iterator it_line;
    
    string cmd ;
    vector<string> arg_table;  // Keep current cmd args .
    int pipeNum , pipeErrNum;
    
    bool PipeToSame ;
    
    
    vector<pipeFd> pipeTable; // Keep Pipes Info.
    vector<string> pathTable; // Keep Paths .
    
    
    do{
        cout << "% ";
        
        getline(cin,line);
        SplitPath(path,':',pathTable);
        
        after_split_line = SplitWithSpace(line);
        it_line = after_split_line.begin();
        
        while(it_line != after_split_line.end() && *it_line != "\0")
        {
            int infd = sockfd  , outfd = sockfd , errfd = sockfd;
            bool PipeToNext = false;
            Sign sign = NONE;
            
            ParseCmd(after_split_line, it_line, arg_table, cmd, pipeNum, pipeErrNum, sign, PipeToNext);
            
            
            if(!LegalCmd(cmd,pathTable))
            {
                EraseNonUsePipe(pipeTable);
                //SubPipeCount(pipeTable);
                ArgtableReset(arg_table);
                cout << "Unknown command: [" << cmd << "]." << endl;
                break;
            }
            
            bool hasInPipe = false;
            
            
            if(sign == PIPE)
            {
                // *先判斷pipesame,在判斷hasinpipe : (假設目前pipe1 : enter=4,out=3)
                //    因為若有hasinpipe , 會先close掉入口(4) , 然後剛好又沒有pipesame , 新pipe pipe2出口將會在4(enter=5,out=4)
                //    接著最後會Run EraseNonUsePipe , pipe1 會被erase且close其fd , 所以3,4被close => 關到pipe2的出口4 !
                
                if(!PipeToNext)
                    PipeToSameSetting(pipeTable, pipeNum, outfd, PipeToSame);
                
                if(!PipeToSame)
                    CreateNewPipe(pipeNum, outfd, pipeTable,PipeToNext);
                
                StdInSetting(infd,hasInPipe, pipeTable);
                
            }
            else if(sign == ERRPIPE)
            {
                if(!PipeToNext)
                    PipeToSameSetting(pipeTable, pipeErrNum, errfd, PipeToSame);
                
                if(!PipeToSame)
                {
                    CreateNewPipe(pipeErrNum, errfd, pipeTable,PipeToNext);
                    outfd = errfd ;
                }
                
                
                StdInSetting(infd,hasInPipe, pipeTable);
                
            }
            else if(sign == PIPE_ERRPIPE)
            {
                // for StdOutPipe.
                if(!PipeToNext)
                    PipeToSameSetting(pipeTable, pipeNum, outfd, PipeToSame);
                
                if(!PipeToSame)
                    CreateNewPipe(pipeNum, outfd, pipeTable, PipeToNext);
                
                // for ErrPipe.
                if(!PipeToNext)
                    PipeToSameSetting(pipeTable, pipeErrNum, errfd, PipeToSame);
                
                if(!PipeToSame)
                    CreateNewPipe(pipeErrNum, errfd, pipeTable, PipeToNext);
                
                
                StdInSetting(infd,hasInPipe, pipeTable);
                
            }
            else if (sign == BRACKET)
            {
                // infd = sock
                string targetfile = arg_table[arg_table.size()-1];
                FILE * outfile;
                outfile = fopen(targetfile.c_str(), "w");
                if(outfile == NULL)
                {
                    perror("Open file error!");
                    return;
                }
                outfd = fileno(outfile);
                
                StdInSetting(infd,hasInPipe,pipeTable);
                
                arg_table.pop_back();
                
            }
            else
            {
                if(IsBuildIn(cmd))
                {
                    // Process builtin
                    if(cmd == "printenv")
                    {
                        if (arg_table.size() >= 1)
                            path = getenv(arg_table[0].c_str());
                        if (path != "" )
                            cout << "PATH=" << path << endl;
                    }
                    else if(cmd == "setenv")
                    {
                        setenv(arg_table[0].c_str(), arg_table[1].c_str(), 1);
                        path = getenv("PATH");
                        pathTable.clear();
                        SplitPath(path,':',pathTable);
                    }
                    
                    EraseNonUsePipe(pipeTable);
                    //SubPipeCount(pipeTable);
                    ArgtableReset(arg_table);
                    break;
                }
                
                if(cmd == "exit")
                    return;
                
                StdInSetting(infd,hasInPipe, pipeTable);
                
            }
            
            int status = RunCmd(infd, outfd, errfd,sockfd, cmd, arg_table, pathTable);
            
            if(status == -1)
                return;
            
            if(hasInPipe)
                close(infd); // 該Pipe已完成任務,關閉出口(即完全關閉,入口已在HasInPipe()時關閉).
            
            //fclose(outfile);
            
            EraseNonUsePipe(pipeTable);
            
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
        SubPipeCount(pipeTable);
        ArgtableReset(arg_table);
        
    }while(1);
    
}


int main(int argc, const char **argv) {
    // insert code here...
    int sockfd,newsockfd,childpid;
    struct sockaddr_in cli_addr,serv_addr;
    socklen_t clilen;
    int PORT = atoi(argv[1]);
    
    
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)  // 建立sockfd .
        perror("server erro!");
    
    bzero((char*)&serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY : 可接受任何Client .
    serv_addr.sin_port = htons(PORT);
    
    // bind 前須加 '::' , 否則會和std的bind搞混 .
    if(::bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) <0) // bind sock & port .
        perror("bind error");
    
    listen(sockfd, 5); // listen client .
    signal(SIGCHLD, SIG_IGN); // kill zombie
    
    for(;;)
    {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen); // accept , 並建立新fd來服務該client .
        
        if (newsockfd < 0)
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
            // 新的(child)process配上新的sock來服務client .
            setenv("PATH","bin:.",1);
            //chdir("/net/gcs/105/0556087/ras"); // 改變執行時目錄 , demo時使用 .
            //先將sockfd導向stdin , out ,err .
            dup2(newsockfd, 0);
            dup2(newsockfd, 1);
            dup2(newsockfd , 2);
            
            close(sockfd);

            PrintWelcome(newsockfd);
            RunShell(newsockfd);
            
            close(newsockfd); // 整個結束再關,因為若在RunShell()前關,此時newsockfd位置就被釋放出來,可被pipefd所用,可能導致
            exit(0);          //   之後誤判(將Pipefd判斷成sockfd),造成錯誤！
        }
        else
        {
            close(newsockfd);
        }
    }
    
    
    
    return 0;
}
