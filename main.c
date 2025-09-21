#include <string.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT            2505
#define IP              "192.168.9.40"
#define BACKLOG         128
#define SOCKET_PROTOCOL AF_INET
#define ERROR(msg)      perror(msg); exit(EXIT_FAILURE)
#define EXIT_COMMAND    "exit"
#define EPEV_COUNT      10
#define WELCOME_MESSAGE "Hello, my dear friend!\n"
#define READ_BUFSZ      512
#define CFDS_COUNT      1024*1024

void epoll()
{
    const int epfd = epoll_create1(0);
    if(epfd == -1)
    {
        ERROR("Error on creating epoll file descriptor");
    }

    const int tcp_fd = socket(SOCKET_PROTOCOL, SOCK_STREAM, 0);
    if(tcp_fd == -1)
    {
        ERROR("Error opening socket");
    }

    struct epoll_event epev = {
        .events  = EPOLLIN,
        .data.fd = tcp_fd,
    };

    if(epoll_ctl(epfd, EPOLL_CTL_ADD, tcp_fd, &epev) == -1)
    {
        ERROR("Error on setuping epoll instance");
    }

    epev.events  = EPOLLIN;
    epev.data.fd = STDIN_FILENO;
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &epev) == -1)
    {
        ERROR("Error on setuping epoll instance");
    }

    struct sockaddr_in addr = {
        .sin_family = SOCKET_PROTOCOL,
        .sin_port   = htons(PORT)
    };

    if(inet_aton(IP, &addr.sin_addr) == 0)
    {
        fprintf(stderr, "Error on assigning %s ip address", IP);
        exit(EXIT_FAILURE);
    }

    {
        int opt = 1;
        if(setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        {
            ERROR("Error on setting options for socket");
        }
    }

    struct epoll_event events[EPEV_COUNT] = {0}; // TODO: check if event number will be low


    if(bind(tcp_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        ERROR("Error on binding socket to address");
    }

    if(listen(tcp_fd, BACKLOG) == -1)
    {
        ERROR("Error on listening tcp socket");
    }
    fprintf(stderr, "All initialization went good, so I'm accepting clients\n");

    char buf[READ_BUFSZ] = {0};
    bool is_interrupt = false;
    for (;;) {
        if(is_interrupt) break;

        int ev_count = epoll_wait(epfd, events, EPEV_COUNT, -1);
        if (ev_count == -1) {
            ERROR("Error on waiting epoll. Incorrect event count");
        }

        for (int i = 0; i < ev_count; ++i) {
            if (events[i].data.fd == tcp_fd) {
                struct sockaddr_in peer_addr = {0};
                socklen_t addr_size = sizeof(peer_addr);

                int cfd = accept(tcp_fd, (struct sockaddr *)&peer_addr, &addr_size);
                if (cfd == -1) {
                  ERROR("error on accepting client");
                }

                epev.events = EPOLLIN | EPOLLET;
                epev.data.fd = cfd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &epev) == -1) {
                  ERROR("Error on setuping client file descriptor");
                }

                // cfds[++cfds_len] = cfd;
            } else if (events[i].data.fd == STDIN_FILENO) {
                //Handling server commands
                ssize_t bread = read(events[i].data.fd, buf, READ_BUFSZ);

                if (bread == -1) {
                  ERROR("Internal error");
                } else if (bread == 0) {
                    abort();
                }

                buf[bread - 1] = '\0';

                if(strcmp(buf, "x") == 0) {
                    is_interrupt = true;
                    break;
                }
                
            } else {
                ssize_t bread = read(events[i].data.fd, buf, READ_BUFSZ);

                if (bread == -1) {
                  ERROR("Internal error");
                } else if (bread == 0) {
                    abort();
                }

                buf[bread - 1] = '\0';
                puts(buf);
                printf("%zd bytes read\n", bread);

                for (ssize_t i = 0; i < bread; ++i) {
                  printf("%d ", buf[i]);
                }
                printf("\n");
                
            }
        }
    }
    close(epfd);
    exit(EXIT_SUCCESS);
}

void io_uring()
{
    fprintf(stderr, "Unimplemented\n");
    abort();
}

int main()
{
    epoll();
    //io_uring
}
