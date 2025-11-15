#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

//Common macros
#define ERROR(msg) \
    fprintf(stderr, "ERROR: %s. %d. %s\n", msg, errno, strerror(errno)); \
    exit(EXIT_FAILURE)

#define PORT    42067
#define IP      "127.0.0.1"
#define BACKLOG 128

//Epoll related macros
#define EVENT_COUNT   512
#define CLIENT_BUFFSZ 512

// Dynamic array of file descriptors. Used for tracking fds and close them properly.
typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} Fds;

Fds fds_create() {
    const int capacity = 8;
    int *fds = (int *)malloc(capacity * sizeof(int));

    if(fds == NULL) {
        ERROR("malloc, creating fds");
    }

    return (Fds) {
        .items = fds,
        .count = 0,
        .capacity = capacity,
    };
}

void fds_add(Fds *list, const int fd) {
    if(list->count >= list->capacity) {
        const int new_capacity = list->capacity * 2;
        int *new_ptr = (int *)malloc(new_capacity * sizeof(int));
        if(new_ptr == NULL) {
            ERROR("malloc, fds adding new element. buy ram lol");
        } 

        for(size_t i = 0; i < list->count; ++i) {
            new_ptr[i] = list->items[i];
        }

        free(list->items);

        list->capacity = new_capacity;
        list->items = new_ptr;
    }
    list->items[list->count++] = fd;
}

void fds_remove(Fds *list, int element) {
    size_t el_i;
    for(el_i = 0; el_i < list->count; ++el_i) {
        if(list->items[el_i] == element) {
            break;
        }

    }
    if(el_i == list->count) return;
    else if(el_i != list->count - 1) {
        for(size_t i = el_i + 1; i < list->count; ++i) {
            list->items[i - 1] = list->items[i];
        }
    }
    --list->count;
}

void fds_free(const Fds *list) {
    free(list->items);
}

int main(void)
{
    // TCP socket initialization and configuration.
    
    // Creating file descriptor for tcp socket. Later clients will connect to this one.
    // 
    // Parameters:
    // AF_INET     - IPv4 internet protocol.
    // SOCK_STREAM - TCP.
    // 0           - type of protocol.
    const int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_fd == -1) {
        ERROR("tcp_socket creating");
    }

    // sockaddr_in is a subtype of supertype sockaddr.
    // As we specified AF_INET IPv4 protocol in TCP socket, we have to use exactly this subtype.
    // Also there is a couple of functions for converting ip and port into binary form (network byte order).
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
        .sin_addr   = { .s_addr = inet_addr(IP) },
    };

    // If for some reason program won't close TCP socket then we can't use address specified previously.
    // So we use this function to set SO_REUSEADDR for reusing previously not closed address.
    // 
    // Also this is generic functions, so we need to pass suitable `optval` and size for option for recognizing option
    // for current option (for `SO_REUSEADDR` it has to be int).
    //
    // Parameters:
    // tcp_fd         - socket file descriptor. fd to configure.
    // SOL_SOCKET     - option level. For socket it has to be SOL_SOCKET.
    // SO_REUSEADDR   - allow reuse of local address (IP + port).
    // &optval        - pointer to option value (1 = enable).
    // sizeof(optval) - size of option value in bytes.
    const int optval = 1;
    if(setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        ERROR("setting options for socket");
    }

    // Binding current address to the socket.
    // Also generic function, so we need to cast subtype to his supertype.
    if(bind(tcp_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        ERROR("binding socket");
    }

    // Socket starts to listen and accepting clients.
    if(listen(tcp_fd, BACKLOG) == -1) {
        ERROR("listening tcp socket");
    }

    // EPOLL - IO event notification interface.
    // This is how we manage 'async' i/o in our program.
    // Returns file descriptor that will watch on file descriptors events.
    //
    // Have to create it with zero
    const int epfd = epoll_create1(0);
    if(epfd == -1) {
        ERROR("creating epoll file descriptor");
    }

    // Epoll instance for configuring our TCP fd to be notificable by epoll,
    // So we can process events on TCP socket in events queue, like accepting client.
     struct epoll_event epev = {
        .events  = EPOLLIN,
        .data.fd = tcp_fd,
    };
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, tcp_fd, &epev) == -1) {
        ERROR("setting up epoll instance");
    }

    // Configuring out standart input for be watcheable by epoll instance.
    epev.events  = EPOLLIN;
    epev.data.fd = STDIN_FILENO;
    if(epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &epev) == -1) {
        ERROR("setting up epoll instance");
    }

    // Our kernel will be adding events on watchable file descriptos in this array.
    struct epoll_event events[EVENT_COUNT] = {0};
    
    // We are going to add client file descriptors to properly release resources.
    Fds fds = fds_create();

    fprintf(stderr, "All initialization went good, so I'm accepting clients\n");

    //Our accepted file descriptors will write messages in this array.
    char buf[CLIENT_BUFFSZ] = {0};
    bool is_interrupt = false;
    for (;;) {
        if(is_interrupt) break;

        // Waiting on any events on any file descriptor.
        //
        // -1 is for timeout - wait until event.
        int ev_count = epoll_wait(epfd, events, EVENT_COUNT, -1);
        if (ev_count == -1) {
            ERROR("waiting epoll");
        }

        // We got events so this loop if going to process them.
        for (int i = 0; i < ev_count; ++i) {
            int current_fd = events[i].data.fd;

            // Got event on TCP socket. The only event we configured for TCP fd is accepting client.
            // So this if branch will accept new connection.
            if (current_fd == tcp_fd) {
                struct sockaddr_in connected_addr = {0};
                socklen_t addr_size = sizeof(connected_addr);

                // Accepting new connection.
                int new_client_fd = accept(tcp_fd, (struct sockaddr *)&connected_addr, &addr_size);
                if (new_client_fd == -1) {
                    ERROR("accepting new client");
                }

                // Configure events on new client.
                epev.events = EPOLLIN;
                epev.data.fd = new_client_fd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, new_client_fd, &epev) == -1) {
                    ERROR("setting up client file descriptor");
                }

                fds_add(&fds,  new_client_fd);

                // Got event on stdin.
            } else if (current_fd == STDIN_FILENO) {
                ssize_t bytes_read = read(current_fd, buf, CLIENT_BUFFSZ);

                if (bytes_read <= 0) {
                    fprintf(stderr, "ERROR: unexpected read bytes count for standart input\n. Aborting\n");
                    exit(EXIT_FAILURE);
                }

                buf[bytes_read] = '\0';

                // To finish our program and release resources.
                if(strncmp(buf, "x", 1) == 0) {
                    is_interrupt = true;
                    break;
                }
                
                // Got event on client file descriptor.
            } else {
                ssize_t bytes_read = read(current_fd, buf, CLIENT_BUFFSZ);

                if (bytes_read == -1) {
                    fprintf(stderr, "ERROR: unexpexted error during processing client (read bytes = -1)\n");
                    exit(EXIT_FAILURE);
                } // End-of-file, must close socket
                else if (bytes_read == 0) {
                    fds_remove(&fds, current_fd);
                    if(epoll_ctl(epfd, EPOLL_CTL_DEL, current_fd, events) == -1) {
                        ERROR("removing connected client from epoll");
                    }
                    if(close(current_fd) == -1) {
                        ERROR("closing connected client");
                    }
                    break;
                }

                // Write message back.
                buf[bytes_read] = '\0';
                if(write(current_fd, buf, bytes_read) == -1) {
                    ERROR("writing to connected fd");
                }
            }
        }
    }

    for(size_t i = 0; i < fds.count; ++i) {
        close(fds.items[i]);
    }

    if(close(tcp_fd) == -1) {
        ERROR("closing tcp fd");
    }
    if(close(epfd) == -1) {
        ERROR("closing epoll instance");
    }
    fds_free(&fds);
    return 0;
}
