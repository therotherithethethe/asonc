#include <arpa/inet.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Minimal asynchronous io write example
// 
void liburing_helloworld() {
    struct io_uring ring;
    io_uring_queue_init(8, &ring, 0);

    char buf[] = "Hello, World!";

    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_write(sqe, 1, buf, sizeof(buf), 0);
    io_uring_submit(&ring);
    
    io_uring_wait_cqe(&ring, &cqe);
    io_uring_cqe_seen(&ring, cqe);

    io_uring_queue_exit(&ring);
}

//Common macros
#define ERROR(msg) fprintf(stderr, "ERROR: %s. %d. %s\n", msg, errno, strerror(errno)); exit(EXIT_FAILURE)
#define IP          "127.0.0.1"
#define BACKLOG     128
#define PORT        42067

//io_uring related macros
#define GROUP_ID    7
#define BUFS_NUM    1024
#define BUF_SZ      1024
#define IOU_ENTRIES 8

// // Request that we will restore 
// typedef struct {
//     int32_t fd;
//     int8_t event_type;
// } EventRequest;

// #define READ 1
// #define ACCEPT 2

void liburing_tcp() {
    struct io_uring ring;
    io_uring_queue_init(IOU_ENTRIES, &ring, 0); //TODO: adjust

    int buf_ring_err;
    struct io_uring_buf_ring *br = io_uring_setup_buf_ring(&ring, BUFS_NUM, GROUP_ID, 0, &buf_ring_err);
    if(buf_ring_err < 0)
    {
        fprintf(stderr, "ERROR: initialization of ring buffer failed. %d. %s\n", buf_ring_err, strerror(buf_ring_err));
        exit(EXIT_FAILURE);
    }

    const uint32_t iou_mask = io_uring_buf_ring_mask(BUFS_NUM);
    char *bufs_ring = malloc(BUFS_NUM * BUF_SZ);
    for(int i = 0; i < BUFS_NUM; ++i) {
        char* curr_arr_ptr = bufs_ring + i*BUF_SZ;
        io_uring_buf_ring_add(br, curr_arr_ptr, BUF_SZ, i, iou_mask, i);
    }
    io_uring_buf_ring_advance(br, BUFS_NUM);


    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    int tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(tcp_fd == -1)
    {
        ERROR("tcp_socket creating");
    }
    
    int optval = 1;
    if(setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        ERROR("Error on setting options for socket");
    }
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
        .sin_addr   = { .s_addr = inet_addr(IP) },
    };

    if(bind(tcp_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
    {
        ERROR("bind");
    }

    if(listen(tcp_fd, BACKLOG) == -1) {
        ERROR("listen");
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    // char client_buf[1024] = {0};

    // EventRequest *tcpfd_accept_req = malloc(sizeof(EventRequest));
    // tcpfd_accept_req->fd = tcp_fd;
    // tcpfd_accept_req->event_type = ACCEPT;

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_multishot_accept(sqe, tcp_fd, (struct sockaddr*)&client_addr, &client_addr_size, 0);
    sqe->user_data = (uint64_t)tcp_fd; //

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read_multishot(sqe, STDIN_FILENO, 0, 0, GROUP_ID);
    sqe->user_data = (uint64_t)STDIN_FILENO;
    io_uring_submit(&ring);

    for(;;) {
        io_uring_wait_cqe(&ring, &cqe);
        // EventRequest *current_req = (EventRequest *)cqe->user_data;
        int current_fd = cqe->user_data;
        // if(current_req->event_type == ACCEPT) {
        if(current_fd == tcp_fd) {
            int client_fd = cqe->res;
            if(client_fd == -1) {
                ERROR("client_fd");
            }
            
            // EventRequest *client_read_req_for_newfd = malloc(sizeof(EventRequest));
            // client_read_req_for_newfd->event_type = READ;
            // client_read_req_for_newfd->fd = client_fd;

            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_recv_multishot(sqe, client_fd, NULL, 0, 0);
            sqe->flags |= IOSQE_BUFFER_SELECT;
            sqe->buf_group = GROUP_ID;
            // sqe->user_data = (uint64_t)client_read_req_for_newfd;
            sqe->user_data = (uint64_t)client_fd;

            io_uring_submit(&ring);
        // } else if (current_req->event_type == READ) {
        } else if (current_fd == STDIN_FILENO) {
            uint32_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
            char *buf = bufs_ring + (size_t)bid * BUF_SZ;
            int32_t len = cqe->res;
            printf("%s\n", buf);

        } else {
            uint32_t bid = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
            char *buf = bufs_ring + (size_t)bid * BUF_SZ;
            int32_t len = cqe->res;

            if (len < 0) {
                fprintf(stderr, "%d.%s\n", -len, strerror(-len));
                exit(EXIT_FAILURE);
            }
            // if (write(current_req->fd, buf, len) == -1) {
            if (write(current_fd, buf, len) == -1) {
                fprintf(stderr, "%d\n", len);
                ERROR("client write");
            }
            
            io_uring_buf_ring_add(br, buf, BUF_SZ, bid, iou_mask, 0);
            io_uring_buf_ring_advance(br, 1);
        }
        // } else {
        //     fprintf(stderr, "Unexpected event type. Aborting...\n");
        //     exit(EXIT_FAILURE);
        // }

    io_uring_cqe_seen(&ring, cqe);

    }
    if(close(tcp_fd) == -1) {
        ERROR("closing tcp fd");
    }
    io_uring_queue_exit(&ring);
}

int main() {
    liburing_tcp();
    return 0;
}
