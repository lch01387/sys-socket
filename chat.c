#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <omp.h>
#include "myqueue.h"

#define IP "127.0.0.1"
#define PORT 3000
#define MAX_CLIENT 2
#define MAX_DATA 1024
#define MAX_EVENTS 10

#define BUF_SIZE 200
#define FILE_SIZE 10485760 // 10M

int launch_chat(int client_num);
int launch_server(void);
int get_server_status(void);
/* file_buf에서 한줄읽어 buf에 써주는 함수 */
int read_line(char* buf, char* file_buf, int* cursor);
void exit_server(int* client_fd, int serverSock);
void exit_client(int clientSock);
int setnonblocking(int fd);

char file_buf[FILE_SIZE];

int main(int argc, char *argv[])
{
    int ret = -1;
    int num_client;
    int client_num;

    if ((argc != 2) && (argc != 3)) {
usage:  fprintf(stderr, "usage: %s s|c client_num\n", argv[0]);
        goto leave;
    }
    if ((strlen(argv[1]) != 1))
        goto usage;
    switch (argv[1][0]) {
      case 's': // Launch Server
                ret = launch_server();
                break;
      case 'c': // Launch client
                client_num = strtol(argv[2], NULL, 10);
                ret = launch_chat(client_num);
                break;
      default:
                goto usage;
    }
leave:
    return ret;
}

int launch_chat(int client_num)
{
    int clientSock;
    struct sockaddr_in serverAddr;
    fd_set rfds, wfds, efds;
    int ret = -1;
    char rdata[MAX_DATA];
    int i, send_isready = 0;
    struct timeval tm;
    char buf[BUF_SIZE];
    int connect_isover = 0;

    int read_fd, write_fd, cursor = 0;
    char open_path[BUF_SIZE];
    char write_path[BUF_SIZE];
    char line_buf[BUF_SIZE];
    
    sprintf(open_path, "./test/input%d.txt", client_num);
    sprintf(write_path, "./test/output%d.txt", client_num);
    
    /* open file */
    if((read_fd = open(open_path, O_RDONLY)) == -1){
        perror("open error. put read_path");
        goto leave;
    }
    if((write_fd = open(write_path, O_WRONLY | O_CREAT, 0644)) == -1){
        perror("open error. put write_path");
        goto leave;
    }
    /* read file */
    if(read(read_fd, file_buf, FILE_SIZE) == -1){
        perror("read error");
        goto leave;
    }

    /* set socket */
    if ((ret = clientSock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        goto leave;
    }
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(IP);
    serverAddr.sin_port = htons(PORT);

    if ((ret = connect(clientSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)))) {
        perror("connect");
        goto leave1;
    }
    printf("[CLIENT] Connected to %s\n", inet_ntoa(*(struct in_addr *)&serverAddr.sin_addr));

    // start select version of chatting ...
    i = 1;
    ioctl(0, FIONBIO, (unsigned long *)&i);
    if ((ret = ioctl(clientSock, FIONBIO, (unsigned long *)&i))) {
        perror("ioctlsocket");
        goto leave1;
    }
    
#pragma omp parallel sections
    {
#pragma omp section
        {
            while(!connect_isover){
//                sleep(1);
                if(send_isready == 1){
                    if((ret = read_line(buf, file_buf, &cursor)) == -1){
                        printf("meet endline\n");
                        if ((ret = send(clientSock, "@", 1, MSG_DONTWAIT)) < 0){
                            perror("send error");
                            exit_client(clientSock);
                        }
                        printf("send @\n");
                        break;
                    }else{
                        printf("send line: ");
                        for(i=0; i<ret; i++){
                            printf("%c", buf[i]);
                        }
                        if ((ret = send(clientSock, buf, ret, MSG_DONTWAIT)) < 0){
                            perror("send error");
                            exit_client(clientSock);
                        }
                    }
                }
            }
        }
#pragma omp section
        {
            //입력을 계속해서 기다림.
            while (!connect_isover) {
//                sleep(1);
                FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
                FD_SET(clientSock, &rfds);
                FD_SET(clientSock, &efds);
                FD_SET(0, &rfds); // 0은 stdin
                
                if ((ret = select(clientSock + 1, &rfds, &wfds, &efds, &tm)) < 0) {
                    perror("select");
                    exit_client(clientSock);
                } else if (!ret)
                    continue;
                if (FD_ISSET(clientSock, &efds)) {
                    perror("Connection closed");
                    exit_client(clientSock);
                }
                if (FD_ISSET(clientSock, &rfds)) {
                    if ((ret = recv(clientSock, rdata, MAX_DATA, 0)) < 0) {
                        perror("Connection closed by remote host");
                        exit_client(clientSock);
                    }
                    
                    if(rdata[0] == '&'){
                        printf("received &. send file line by line.\n");
                        send_isready = 1;
                    }else{
                        if(rdata[0] == '%' || rdata[ret-1] == '%'){
                            ret--;
                            printf("received %. diconnect\n");
                            close(clientSock);
                            connect_isover = 1;
                        }
                        //client가 server에게서 입력받은 한줄을 그대로 파일에 적기
                        //%수신하면 서버와 연결끊고 fclose(fd)
                        if(ret>0){
                            printf("received: ");
                            printf("received length: %d\n", ret);
                            for(i=0; i< ret; i++){
                                printf("%c", rdata[i]);
                            }
                            write(write_fd, rdata, ret);
                        }
                    }
                    fflush(stdout);
                }
            }
        }
    }
    fflush(stdout);

leave1:
    close(clientSock);
leave:
    return -1;
}

int launch_server(void)
{
    struct epoll_event ev, events[MAX_EVENTS];
    int conn_sock, nfds, epollfd;
    
    int serverSock;
    struct sockaddr_in Addr;
    socklen_t AddrSize = sizeof(Addr);
    char data[MAX_DATA], *p;
    int ret, count, i;
    int num_client = 0;
    Queue queue;
    int flag;
    int n;
    char* buf; // client에게서 받은 문장
    int client_fd[MAX_CLIENT]; // client의 파일디스크립터 저장
    char* temp_buf;
    int num_fin = 0, num_closed = 0; // MAX_CLIENT가 되면 종료한다.
    int send_isover = 0;
    
    InitQueue(&queue);
    if ((ret = serverSock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        goto leave;
    }
    /* 소켓 옵션주기 */
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, (void *)&i, sizeof(i));
    
    /* 포트번호 할당, IP할당 등 */
    Addr.sin_family = AF_INET;
    Addr.sin_addr.s_addr = INADDR_ANY;
    Addr.sin_port = htons(PORT);
    /* bind함수로 주소 부여 */
    if ((ret = bind(serverSock, (struct sockaddr *)&Addr,sizeof(Addr)))) {
        perror("bind");
        goto error;
    }
    /* listen 대기중 */
    if ((ret = listen(serverSock, 5))) {
        perror("listen");
        goto error;
    }
    
    /* epoll_create로 epoll을 위한 자원을 준비한다. */
    epollfd = epoll_create(10);
    if(epollfd == -1){
        perror("epoll_create");
        exit(EXIT_FAILURE);
    }
    
    /* non-blocking */
    setnonblocking(serverSock);
    
    ev.events = EPOLLIN;
    ev.data.fd = serverSock;
    /* epoll_ctl로 감시할 채널들을 하나씩 직접 지정한다. */
    if(epoll_ctl(epollfd, EPOLL_CTL_ADD, serverSock, &ev) == -1){
        perror("epoll_ctl: serverSock");
    }
    
#pragma omp parallel sections
    {
#pragma omp section
        {
            // 모든 CLIENT가 아직 @를 전송하지 않았거나 보내줄 버퍼가 남아있다면
            while(num_fin < MAX_CLIENT || !IsEmpty(&queue)){
//                sleep(1);
                if(!IsEmpty(&queue)){
                    temp_buf = Dequeue(&queue);
                    printf("send length: %d\n", strlen(temp_buf));
                    printf("send buf: ");
                    for(i=0; i<strlen(temp_buf); i++)
                        printf("%c", temp_buf[i]);
                    for(i=0; i<MAX_CLIENT; i++){
                        if((ret = send(client_fd[i], temp_buf, strlen(temp_buf), 0)) < 0){
                            perror("send error");
                            exit_server(client_fd, serverSock);
                        }
                    }
                    free(temp_buf);
                }
            }
            send_isover = 1;
        }
#pragma omp section
        {
            for(;;){
//                sleep(1);
                // 마지막 timeout이 -1이면 계속 기다리는 block모드와 동일.
                if((nfds = epoll_wait(epollfd, events, MAX_EVENTS, 10)) == -1){
                    perror("epoll_pwait");
                    exit_server(client_fd, serverSock);
                }
                for(n=0;n<nfds;++n){
                    if(events[n].data.fd == serverSock){ // accept할 준비가 되었다.
                        conn_sock = accept(serverSock, (struct sockaddr *) &Addr, &AddrSize);
                        if(conn_sock == -1){
                            perror("accept error");
                            exit_server(client_fd, serverSock);
                        }
                        printf("connected\n");
                        
                        /* set non-blocking */
                        setnonblocking(conn_sock);
                        
                        ev.events = EPOLLIN | EPOLLET; // 읽을수있는지, edge trigger
                        // EPOLLIN: 수신할 데이터가 존재하는 이벤트
                        // EPOLLET: edge trigger 방식으로 감지, 디폴트는 level trigger
                        ev.data.fd = conn_sock;
                        if(epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1){
                            perror("epoll_ctl: conn_sock");
                            exit(EXIT_FAILURE);
                        }
                        client_fd[num_client] = conn_sock;
                        num_client++;
                        if(num_client >= MAX_CLIENT){
                            for(i=0; i<MAX_CLIENT; i++)
                                if ((ret = send(client_fd[i], "&", 1, 0)) < 0) {
                                    perror("sends");
                                    exit_server(client_fd, serverSock);
                                }
                            printf("successfully send & to clients\n");
                        }
                        printf("num_client: %d\n", num_client);
                        
                    }else{
                        /* receive buffer */
                        if (!(ret = count = recv(events[n].data.fd, data, MAX_DATA, 0))) {
                            fprintf(stderr, "Connect Closed by Client\n");
                            num_closed++;
                            if(num_closed >= MAX_CLIENT){
                                printf("all connection closed");
                                for(i=0; i<MAX_CLIENT; i++)
                                    close(client_fd[i]);
                            }
                            break;
                        }
                        if (ret < 0) {
                            perror("recv");
                            exit_server(client_fd, serverSock);
                        }
                        
                        /* print received buffer */
                        printf("received: ");
                        for(i=0; i< ret; i++){
                            printf("%c", data[i]);
                        }
                        
                        /* if server get @ */
                        if(data[count-1] == '@' || data[0] == '@'){
                            count--;
                            num_fin++;
                            if(num_fin >= MAX_CLIENT){
                                while(!send_isover){}
                                for(i=0; i<MAX_CLIENT; i++){
                                    if((ret = send(client_fd[i], "%", 1, 0)) < 0){
                                        perror("send error");
                                        exit_server(client_fd, serverSock);
                                    }
                                    printf("send c%d: %\n", i);
                                }
                            }
                        }
                        
                        /* if server get string */
                        if(count>0){
                            // buf를 새로 할당해서 data를 집어넣는다.
                            buf = (char*)malloc(sizeof(char) * BUF_SIZE);
                            printf("received length: %d\n", count);
                            for(i=0; i<MAX_CLIENT; i++){
                                if(client_fd[i] == events[n].data.fd){
                                    snprintf(buf, count+5 ,"c%d: %s\n", i+1, data);
                                    break;
                                }
                            }
                            // buf를 새로 할당받고 buf를 가리키는 포인터를 queue에 push해준다.
                            Enqueue(&queue, buf);
                        }
                        
                    }
                }
            }
        }
    }
error:
    for(i=0; i<MAX_CLIENT; i++)
        close(client_fd[i]);
    close(serverSock);
leave:
    return ret;
}

int read_line(char* buf, char* file_buf, int* cursor)
{
    int ch, count = 0;
    
    if(file_buf[*cursor] == '\n' || file_buf[*cursor] == '\0')
        return -1;
    
    do{
        ch = file_buf[*cursor+count];
        buf[count] = ch;
        count++;
    }while(ch != '\n' && ch!='\0');
    *cursor += count;
    buf[count] = '\0';
    return count;
}

int launch_clients(int num_client)
{
    return 0;
}

int get_server_status(void)
{
    return 0;
}

void exit_client(int clientSock){
    close(clientSock);
}

void exit_server(int* client_fd, int serverSock){
    int i;
    for(i=0; i<MAX_CLIENT; i++)
        close(client_fd[i]);
    close(serverSock);
    exit(EXIT_FAILURE);
}

int setnonblocking(int fd)
{
    int flags;
    
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}
