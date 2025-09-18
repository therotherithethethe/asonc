#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT    2505
#define IP      "127.0.0.1"
#define BACKLOG 128

#define ERROR(msg) perror(msg); exit(EXIT_FAILURE)

int main()
{
    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_fd == -1)
    {
        ERROR("Error opening socket");
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if(inet_aton(IP, &addr.sin_addr) == 0)
    {
        ERROR("Error on creating address");
    }

    int opt = 1;
    if(setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        ERROR("Error on setting options for socket");
    }

    if(bind(tcp_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        ERROR("Error binding socket to address");
    }

    if(listen(tcp_fd, BACKLOG) == -1)
    {
        ERROR("Error to listen sockets");
    }
    fprintf(stderr, "I'm listening for clients!\n");

    struct sockaddr_in peer_addr = {0};
    socklen_t addr_size = sizeof(peer_addr);
    int cfd = accept(tcp_fd, (struct sockaddr *)&peer_addr, &addr_size);
    if(cfd == -1)
    {
        ERROR("Error on accepting client");
    }
    write(cfd,"Hello, my dear friend!\n", 24);

    size_t count = 128;
    char buf[count];

    while(strncmp(buf, "exit", 4) != 0) {
        ssize_t bread = read(cfd, buf, count);
        if(bread == -1) {
            ERROR("Internal error");
        } else if(bread == 0) {
            printf("That's it, no clients left\n");
            break;
        }
        buf[bread - 1] = '\0';
        puts(buf);
        printf("%zd bytes read\n", bread);
        for (ssize_t i = 0; i < bread; ++i) {
            printf("%d ", buf[i]);
        }
        printf("\n");
    }

    if(close(tcp_fd) == -1)
    {
        ERROR("Error on closing socket");
    }

    if(close(cfd) == -1)
    {
        ERROR("Error on closing socket");
    }

    return EXIT_SUCCESS;
}
