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
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:proxy:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);                                             //line:netp:proxy:doit
        Close(connfd);                                            //line:netp:proxy:close
    }
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    int clientfd;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char uri[MAXLINE], host[MAXLINE], port[MAXLINE];
    rio_t rio, rio_s;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);       //line:netp:doit:parserequest
    strcpy(version, "HTTP/1.0");
    parse_url(url, host, port, uri);

    sprintf(buf, "%s %s %s\r\n", method, uri, version);
    sprintf(buf, "%s%s", buf, user_agent_hdr);
    sprintf(buf, "%sHOST: %s\r\n", buf, host);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);

    read_requesthdrs(&rio, buf);                              //line:netp:doit:readrequesthdrs
    
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio_s, clientfd);
    Rio_writen(clientfd, buf, strlen(buf));

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

    ptr = strchr(url, '/'); 
    strcpy(host, ptr + 2); 

    ptr = strchr(host, '/');
    strcpy(uri, ptr);
    *ptr = '\0';

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