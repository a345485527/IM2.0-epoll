#include <map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/errno.h>
#include "./server_handler.h"
#include "./process_protocol.h"
using std::cout;
using std::map;
using std::string;
using std::pair;
void init()
{
    pool_init(4);
    //init struct clientVector
    onlineVec.size=INIT_CLIENT_NUM;
    onlineVec.pOnlineClient=(struct client*)malloc(sizeof(struct client)*INIT_CLIENT_NUM);    
    for(int i=0;i<INIT_CLIENT_NUM;i++)
    {
        onlineVec.pOnlineClient[i].isUsed=false;
        onlineVec.pOnlineClient[i].name[0]='\0';
    }

    //init protocol handler array
    protocol_handler_array[P_C2S_LOGIN]=onCSLogin;
    protocol_handler_array[P_C2S_MES_ALL]=onCSMesAll;
    protocol_handler_array[P_C2S_LOGOUT]=onCSLogout;
    
    //init mutex
    pthread_mutex_init(&map_mutex, NULL);


    listenfd=socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd<0)
    {
        cout<<"socket error\n";
        exit(-1);
    }

    // init epoll
    epfd=epoll_create(256);
    // don't forget to set socket non block
    setnonblock(listenfd);
    ev.data.fd=listenfd;
    ev.events=EPOLLIN | EPOLLET;
    // register epoll events
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(9877);
    servaddr.sin_addr.s_addr=htonl(INADDR_ANY);

    if((bind(listenfd,(struct sockaddr*) &servaddr, sizeof(servaddr)))<0)
    {
        cout<<"bind error\n";
        exit(-1);
    }
    if((listen(listenfd, 5))<0)
    {
        cout<<"listen error\n";
        exit(-1);
    }
    
    cout<<"server start...\n";
}

void acceptClient()
{
    char buff[1024];
    socklen_t len;
    int n;
    for(;;)
    {
        // wait for epoll events
        nfds=epoll_wait(epfd, events, 20 , -1);
        if(nfds<0)
            continue;
        // handle events
        for(int i=0;i<nfds;++i)
        {
            int fd=events[i].data.fd;
            // if listen socket
            if(events[i].data.fd==listenfd)
            {
                len=sizeof(cliaddr);
                /* if two connection reach at the same time,we must handle all of them
                 * so,accept must call in a loop until the return value is -1 and errno 
                 * is marked as EWOULDBLOCK or EAGAIN.
                 * All this like read a socket in epoll ET mode
                 */
                while((sockfd=accept(listenfd,(struct sockaddr*) &cliaddr, &len))>0)
                {
                    setnonblock(sockfd);
                    cout<<"connect from "<<inet_ntop(AF_INET,&cliaddr.sin_addr,buff,
                            sizeof(buff ))
                        <<"port is "<<ntohs(cliaddr.sin_port)<<"\n";

                     //if the size of onlineVec full,realloc it and init new space
                    pthread_mutex_lock(&map_mutex);
                    if(sock_map.size()==onlineVec.size)
                    {
                        onlineVec.size+=10;
                        onlineVec.pOnlineClient=(struct client*)realloc(onlineVec.pOnlineClient, sizeof(struct client*)*onlineVec.size);
                        for(int i=sock_map.size();i<onlineVec.size;i++)
                        {
                            onlineVec.pOnlineClient[i].isUsed=false;
                            onlineVec.pOnlineClient[i].name[0]='\0';
                        }
                    }
                    pthread_mutex_unlock(&map_mutex);
                    
                    ev.data.fd=sockfd;
                    ev.events=EPOLLIN|EPOLLET;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
                }
                if(sockfd==-1&&errno==EWOULDBLOCK)
                    continue;
            }
            // if socket readable
            else if(events[i].events&EPOLLIN)
            {
                char buf[MAX_PACKET_LEN]={0};
                char* pBuf=buf;
                while(1){
                    // first,recv the packet type
                    if((n=recv(fd, buf, sizeof(int), 0))>0)
                    {
                        pBuf+=n;
                        // second,recv the total packet length
                        n=recv(fd,pBuf,sizeof(int),0);
                        *(pBuf+n)='\0';
                        int total_len=(int)*pBuf;
                        pBuf+=n;
                        recv(fd,pBuf,total_len-2*sizeof(int),0);
                        p_base *ptr=(p_base*)buf;
                    //    protocol_handler_array[ptr->pname](ptr,fd);
                        pool_add_work(protocol_handler_array[ptr->pname], ptr, fd); 
                        pBuf=buf;
                    }
                    // connection closed by client,
                    else if(n==0)
                    {
                        /*
                         * close the descriptor will make epoll
                         * remove it from the set of the descriptors
                         * which monitored
                         */
                        close(fd);
                        break;
                    }
                    else if(n==-1&&errno==EWOULDBLOCK)
                    {
                        break;
                    }
                }
             }
        }
    }
}




void createListenCmd()
{
    pthread_t tid;
    pthread_create(&tid, NULL, listenCmd, (int*)listenfd);
}

void * listenCmd(void* arg)
{
    char cmd[10];
    int sockfd=(int)arg;
    while(1)
    {
        read(STDIN_FILENO, cmd, sizeof(cmd));
        if((strncmp(cmd,"exit",4))==0)
        {
            pthread_mutex_destroy(&map_mutex);
            free(onlineVec.pOnlineClient);
            pool_destroy();
            close(sockfd);
            close(epfd);
            // all threads will be exit,close their own socked auto
            exit(0);
        }
    }
}
void loginHelp(p_cs_login* login_ptr,string name,int sockfd)
{

    pthread_mutex_lock(&map_mutex);

        for(int i=0;i<onlineVec.size;i++)
        {
            //this space is not used,put the client name here
           if(onlineVec.pOnlineClient[i].isUsed==false)
           {
               strncpy(onlineVec.pOnlineClient[i].name,login_ptr->name , MAX_NAME_LEN);
               onlineVec.pOnlineClient[i].name[MAX_NAME_LEN-1]='\0';
               onlineVec.pOnlineClient[i].isUsed=true;
               break;
           }
        }

    p_sc_update_friendList *update_packet_ptr;
    update_packet_ptr=(struct p_sc_update_friendList*)
        malloc(sizeof(struct p_sc_update_friendList)+sizeof(client)*onlineVec.size);
    update_packet_ptr->plen=sizeof(struct p_sc_update_friendList)+
        sizeof(client)*onlineVec.size;
    update_packet_ptr->size=onlineVec.size;
    update_packet_ptr->pname=P_S2C_UPDATE_FRIENDLIST;
    memcpy(update_packet_ptr->onlineClient, onlineVec.pOnlineClient,
            sizeof(client)*onlineVec.size);

    // send the online client to the new login client;
    int n= send(sockfd, update_packet_ptr, update_packet_ptr->plen, 0);
    free(update_packet_ptr);


    // send the new login client to the online client
    p_sc_new_login new_login_packet;
    for(map<string,int>::iterator iter=sock_map.begin();iter!=sock_map.end();iter++)
    {
        if(iter->second!=sockfd)
        {
            strncpy(new_login_packet.name, name.c_str(), name.size());
            new_login_packet.name[name.size()]='\0';
            new_login_packet.name[MAX_NAME_LEN-1]='\0';
            send(iter->second, &new_login_packet, new_login_packet.plen, 0);
        }
    }
    pthread_mutex_unlock(&map_mutex);
}


void logoutHelp(string name,int sockfd)
{
    p_sc_logout logout_packet;
    strncpy(logout_packet.name, name.c_str(), name.size());
    logout_packet.name[name.size()]='\0';
    logout_packet.name[MAX_NAME_LEN-1]='\0';

    pthread_mutex_lock(&map_mutex);

    // find the closed client socket,and delete it from map,and tell other online client
    map<string,int>::iterator del_iter=sock_map.find(name);

    if(del_iter==sock_map.end())
        return;
    sock_map.erase(del_iter);
    for(map<string,int>::iterator iter=sock_map.begin();iter!=sock_map.end();iter++)
    {
        send(iter->second, &logout_packet, logout_packet.plen, 0);
    }

    // delete it from onlineVec
    for(int i=0;i<onlineVec.size;i++)
    {
        if( (strncmp( name.c_str(), onlineVec.pOnlineClient[i].name, name.size() ) )==0 )        
        {
            onlineVec.pOnlineClient[i].isUsed=false;
            break;
        }
    }
    pthread_mutex_unlock(&map_mutex);
    cout<<name<<" logout\n";
}


void setnonblock(int sockfd)
{
    int val;
    if((val=fcntl(sockfd, F_GETFL,0))<0)
    {
        cout<<"fcntl error\n";
        exit(0);
    }
    val|=O_NONBLOCK;
    if(fcntl(sockfd, F_SETFL,val)<0)
    {
        cout<<"fcntl error\n";
        exit(0);
    }
}
