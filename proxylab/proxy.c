#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
sem_t log_mutex;

typedef struct vargp{
    int connfd;
    struct sockaddr_in clientaddr;
} vargp_t;

// typedef struct {
//     char uri[MAXLINE];
//     char cache_obj[MAX_OBJECT_SIZE];
//     int used = 0;
// } Cache;

// Cache cache[10];

// void cache_write(char *uri, char *data, int size);
// void cache_read(char *uri, char *buf);
void *thread(void* vargp);
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
int proxy_send(int clientfd, rio_t *conn_rio, char *requestheader, size_t content_length, char *method, char *pathname, char *version);
size_t proxy_receive(int connfd, rio_t *client_rio);
void doit(int connfd, struct sockaddr_in *clientaddr);


int main(int argc, char **argv)
{
    int listenfd;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    pthread_t tid;
    vargp_t *vargp;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    Sem_init(&log_mutex, 0, 1);
    Signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        vargp = Malloc(sizeof(vargp_t));
        vargp->connfd = Accept(listenfd, (SA*)&(vargp->clientaddr), &clientlen);
        Getnameinfo((SA*) &(vargp->clientaddr), clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        Pthread_create(&tid, NULL, thread, vargp);
    }
    Close(listenfd);
    exit(0);
}

void *thread(void* vargp) {
    Pthread_detach(Pthread_self());
    vargp_t *vargp_self = (vargp_t *) vargp;
    doit(vargp_self->connfd, &(vargp_self->clientaddr));                                             
    Close(vargp_self->connfd); 
    Free(vargp_self); 
    return NULL; 
}


void doit(int connfd, struct sockaddr_in *clientaddr) {
    char requestheader[MAXLINE];
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], pathname[MAXLINE];
    int clientfd;
    rio_t conn_rio, client_rio;
    size_t n, byte_size = 0, content_length = 0;

    /* connect to server */
    Rio_readinitb(&conn_rio, connfd);
    if (!Rio_readlineb(&conn_rio, buf, MAXLINE)) { //line:netp:doit:readrequest
        fprintf(stderr, "error: read empty line.\n");
        return;
    }

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        fprintf(stderr, "error: mismatched parameters.\n");
        return;
    }
    if (parse_uri(uri, hostname, pathname, port) != 0) {
        fprintf(stderr, "error: parseuri failed.\n");
        return;
    }

    /* Request header */
    sprintf(requestheader, "%s /%s /%s\r\n", method, pathname, version);
    while ((n = Rio_readlineb(&conn_rio, buf, MAXLINE)) != 0) {
        if (!strncasecmp(buf, "Content-Length", 14)) {
            sscanf(buf+15, "%zu", &content_length);
        }
        sprintf(requestheader, "%s%s", requestheader, buf);
        if (!strncmp(buf, "\r\n", 2)) break;
    }
    if (n == 0) return;

    /* Open clientfd */
    while ((clientfd = open_clientfd(hostname, port)) < 0) {
        fprintf(stderr, "error: openclient fd wrong\n");
        fprintf(stderr, "error: hostname-%s\n", hostname);
        fprintf(stderr, "error: port-%s\n", port);
        return;
    }
    Rio_readinitb(&client_rio, clientfd);

    // Send By Proxy from client to server
    if (proxy_send(clientfd, &conn_rio, requestheader, content_length, method, pathname, version) != -1) {
        // receive by Proxy from server to client
        byte_size = proxy_receive(connfd, &client_rio);   
    }

    format_log_entry(buf, clientaddr, uri, byte_size);
    P(&log_mutex);
    printf("%s\n", buf);
    V(&log_mutex);
    Close(clientfd);
}

int proxy_send(int clientfd, rio_t *conn_rio, char *requestheader, size_t content_length, char *method, char *pathname, char *version) {
    char buf[MAXLINE];

    Rio_writen(clientfd, requestheader, strlen(requestheader));

    /* Request Body */
    if (strcasecmp(method, "GET")) {
        for (int i=0; i<content_length; i++) {
            if (Rio_readnb(conn_rio, buf, 1) <= 0) return -1;
            Rio_writen(clientfd, buf, 1);
        }
    }
    return 0;
}

size_t proxy_receive(int connfd, rio_t *client_rio) {
    char buf[MAXLINE];
    // char data[MAX_OBJECT_SIZE];
    size_t n, byte_size = 0, content_length = 0;

    /* Response Header */
    while ((n = Rio_readlineb(client_rio, buf, MAXLINE)) != 0) {
        // if (n <= MAXLINE) {
        //     memcpy(data+byte_size, buf, n);
        // }
        byte_size += n;
        if (!strncasecmp(buf, "Content-Length: ", 14)) {
            sscanf(buf+15, "%zu", &content_length);
        }
        Rio_writen(connfd, buf, strlen(buf));
        if (!strncmp(buf, "\r\n", 2)) break;
    }
    // printf("byte_size: %x\n", byte_size);
    // if (byte_size <= MAX_OBJECT_SIZE) {
    //     cache_write(uri, data, byte_size);
    // }
    if (n == 0) return 0;

    /* Response Body */
    for (int i=0; i<content_length; i++) {
        if (Rio_readnb(client_rio, buf, 1) == 0) return 0;
        byte_size ++;
        Rio_writen(connfd, buf, 1);
    }

    return byte_size;
}

/*
 * parse_uri 
 */
int parse_uri(char* uri, char* hostname, char* pathname, char* port) {
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL) {
        return -1;
    }
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* extract the port number */
    if (*hostend == ':') {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}


void format_log_entry(char *logstring, struct sockaddr_in *addr, char* uri, size_t size) {
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    char a, b, c, d;

    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    host = ntohl(addr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    sprintf(logstring, "%s: %d.%d.%d.%d %s %zu", time_str, a, b, c, d, uri, size);
}

// void cache_write(char *uri, char *data, int size) {
//     for (int i=0; i<10; i++) {
//         if (cache[i].used == 0) {
//             memcpy(cache[i].cache_obj, data, size);
//             cache[i].used = 1;
//             memcpy(cache[i].uri, uri, MAXLINE);
//             return;
//         }
//     }
// }

// void cache_read(char *uri, char *buf) {
//     for (int i = 0; i < 10; i++) {
//         if (strcmp(cache[i].uri, uri) == 0) {
//             memcpy(buf, cache[i].cache_obj, MAX_OBJECT_SIZE);
//             return;
//         }
//     }
// }