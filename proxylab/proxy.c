#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


void doit(int fd);
void read_requesthdrs(rio_t *rp, char *buf);
void parse_url(char *url, char *host, char *port, char *uri);
void clienterror(int fd, char *cause, char *shortmsg, char *longmsg);
void *thread(void *vargp);

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

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))             //line:netp:doit:readrequest
        return;
    
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);      //line:netp:doit:parserequest
  
    strcpy(version, "HTTP/1.0");
    parse_url(url, host, port, uri);

    if ((clientfd = open_clientfd(host, port)) < 0) {
        clienterror(fd, url, "Not found",
		    "Proxy couldn't connect this web");
        return;
    }

    sprintf(buf, "%s %s %s\r\n", method, uri, version);
    sprintf(buf, "%s%s", buf, user_agent_hdr);
    sprintf(buf, "%sHOST: %s\r\n", buf, host);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    read_requesthdrs(&rio, buf);                   //line:netp:doit:readrequesthdrs

    /* Send request line and headers to server */
    Rio_writen(clientfd, buf, strlen(buf));        

    /* Read servers’ response, and forward to client */
    Rio_readinitb(&rio_s, clientfd);
    while(Rio_readnb(&rio_s, buf, MAXLINE)) {
        Rio_writen(fd, buf, MAXLINE);
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

    ptr = strstr(url, "http://"); 
    if (ptr) {
        strcpy(host, ptr + 7); 
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