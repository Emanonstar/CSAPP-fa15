#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Node of dequeue, used to cache web objects */
typedef struct block {
    char url[MAXLINE];
    size_t payload_size;
    char* payload;
    struct block* prev;
    struct block* next;
} block;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Cache root */
static block* cache_root;

/* Current cache size, only counts payloads(cached web objects) */
static size_t cache_size;

sem_t mutex;

/*Prototypes of functions */
void doit(int fd);
void read_requesthdrs(rio_t *rp, char *buf);
void parse_url(char *url, char *host, char *port, char *uri);
void clienterror(int fd, char *cause, char *shortmsg, char *longmsg);
void *thread(void *vargp);
void cache_init(void);
void add_to_cache(char *url, char* payload, size_t payload_size);
block* get_from_cache(char *url);

int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    cache_init();               //init cache 
    Sem_init(&mutex, 0, 1);     //init mutex = 1

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp =  Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:proxy:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

/*
 * thread routine
 */
/* $begin thread routine */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    printf("thread: %d\n", connfd);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}
/* $end thread routine */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char uri[MAXLINE], host[MAXLINE], port[MAXLINE];
    rio_t rio, rio_s;
    block* cachedp;
    size_t object_zize;
    ssize_t added;
    char *OBJECT_BUFF;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))             //line:netp:doit:readrequest
        return;
    
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);      //line:netp:doit:parserequest
  
    strcpy(version, "HTTP/1.0");

    parse_url(url, host, port, uri);

    sprintf(buf, "%s %s %s\r\n", method, uri, version);
    sprintf(buf, "%s%s", buf, user_agent_hdr);
    sprintf(buf, "%sHOST: %s\r\n", buf, host);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    read_requesthdrs(&rio, buf);                   //line:netp:doit:readrequesthdrs

    /* Get web object from cache */
    P(&mutex);
    if ((cachedp = get_from_cache(url)) != NULL) {
        Rio_writen(fd, cachedp->payload, cachedp->payload_size);
        V(&mutex);
        return;
    }
    V(&mutex);

    /* Get web object from server */
    if ((clientfd = open_clientfd(host, port)) < 0) {
        clienterror(fd, url, "Not found",
		    "Proxy couldn't connect this web");
        return;
    }

    /* Send request line and headers to server */
    Rio_writen(clientfd, buf, strlen(buf));        

    /* Read servers’ response, and forward to client */
    Rio_readinitb(&rio_s, clientfd);
    OBJECT_BUFF = Malloc(MAX_OBJECT_SIZE);
    object_zize = 0;
    while((added = Rio_readnb(&rio_s, buf, MAXLINE)) > 0) {
        if (object_zize <= MAX_OBJECT_SIZE) {
            memcpy(OBJECT_BUFF + object_zize, buf, added);
            object_zize += added;
        }
        Rio_writen(fd, buf, added);
    }

    /* If the size of web object less or equal to MAX_OBJECT_SIZE, add it to cache */
    if (object_zize > MAX_OBJECT_SIZE) {
        Free(OBJECT_BUFF);
    } else {
        P(&mutex);
        add_to_cache(url, OBJECT_BUFF, object_zize);
        V(&mutex);
    }

    Close(clientfd);
}


/*
 * parse_url - parse URL into host, port and uri
 */
/* $begin parse_url */
void parse_url(char *url, char *host, char *port, char *uri) 
{
    char *ptr;                         

    ptr = strstr(url, "//"); 
    if (ptr) {
        strcpy(host, ptr + 2); 
    } else {
        strcpy(host, url);
    }
    
    if ((ptr = strchr(host, '/'))) {
        strcpy(uri, ptr);
        *ptr = '\0';
    } 
     
    if ((ptr = strchr(host, ':'))) {
        strcpy(port, ptr + 1);
        *ptr = '\0';
    } else {
        strcpy(port, "80");
    }
}
/* $end parse_url */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, char *hdr) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    strcat(hdr, buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        strcat(hdr, buf);
    }
    strcat(hdr, "\r\n");
    return;
}
/* $end read_requesthdrs */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s\r\n", shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s\r\n", shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */


/**********************************
 * Cache package
 **********************************/

/* cache_init */
void cache_init(void)
{
    cache_root = NULL;
    cache_size = 0;
}

/* Add payload of payload_size to cache */
void add_to_cache(char *url, char* payload, size_t payload_size)
{
    block* new_blockp;
    block* p;
    block* last_blockp;
    block* block_tofreep = NULL;
    block* nextp;
    size_t sum;
    size_t bytes_tofree;
    
    new_blockp = Malloc(sizeof(block));
    strcpy(new_blockp->url, url);
    new_blockp->payload = payload;
    new_blockp->payload_size = payload_size;
    
    if (cache_size + payload_size <= MAX_CACHE_SIZE) {
        cache_size += payload_size;
    } else {
        /* evict */
        for (p = cache_root; p != NULL; p = p->next) {
            last_blockp = p;
        }

        sum = 0;
        bytes_tofree = cache_size + payload_size - MAX_CACHE_SIZE;
        for (p = last_blockp; p != NULL; p = p->prev) {
            sum += p->payload_size;
            if (sum >= bytes_tofree) {
                block_tofreep = p;
                break;
            }
        }
        // free memory
        for (p = block_tofreep; p != NULL; p = nextp) {
            Free(p->payload);
            nextp = p->next;
            Free(p);
        }
        cache_size = cache_size - sum + payload_size;
        block_tofreep->prev->next = NULL;
    }
    new_blockp->next = cache_root;
    new_blockp->prev = NULL;
    cache_root = new_blockp;
}

/* Get block ptr with url, if no block matches, return NULL */
block* get_from_cache(char *url)
{
    block* p;
    for (p = cache_root; p != NULL; p = p->next) {
        if (!strcmp(url, p->url)) {
            if (p->prev) {
                (p->prev)->next = p->next;
                p->next->prev = p->prev;
                p->next = cache_root;
                p->prev = NULL;
                cache_root = p;
            }
            return p;
        }
    }
    return NULL;
}
