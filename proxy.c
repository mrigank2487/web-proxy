#include </tdio.h>
#include "csapp.h"

#define MAX_OBJECT_SIZE 7204056
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr = "Host: %s\r\n";
static const char *requestline_hdr = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void make_http_header(char *http_header,char *hostname,char *path,int port,
    rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);

int main(int argc,char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  /* Check Command-line args */
  if(argc != 2) {
    fprintf(stderr, "usage :%s <port> \n", argv[0]);
    exit(1);
  }
  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]);
  while(1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
    /* print accepted message */
    Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                port, MAXLINE, 0);
    printf("Accepted connection from (%s %s).\n", hostname, port);
    /* sequential handle the client transaction */
    doit(connfd);
    Close(connfd);
  }
}

/* handle the client HTTP transaction */
void doit(int connfd)
{
  /* the end server file descriptor */
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char server_http_header [MAXLINE];
  /* store the request line arguments */
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  /* rio is client's rio, server_rio is endserver's rio */
  rio_t rio_client;
  riot_t rio_server;
  Rio_readinitb(&rio_client, connfd);
  Rio_readlineb(&rio_client, buf, MAXLINE);
  /* read the client request line */
  sscanf(buf,"%s %s %s", method, uri, version);

  if(strcasecmp(method,"GET")){
    clienterror(connfd, method, "501", "Proxy does not implement the method");
    return;
  }

  /*parse the uri to GET hostname, file path, port*/
  parse_uri(uri, hostname, path, &port);
  /*build the http header which will send to the end server*/
  make_http_header(server_http_header, hostname, path, port, &rio_client);
  /*connect to the end server*/
  if (serverfd = connect_server(hostname, port, 
      server_http_header) < 0) {
    printf("connection failed\n");
    return;
  }
  Rio_readinitb(&rio_client, serverfd);
  /*write the http header to endserver*/
  Rio_writen(serverfd, server_http_header, 
          strlen(server_http_header));
  /*receive message from end server and send to the client*/
  size_t n;
  while((n = Rio_readlineb(&rio_server, buf, MAXLINE)) != 0) {
    printf("proxy received %d bytes, then sent to client\n",n);
    Rio_writen(connfd, buf, n);
  }
  Close(serverfd);
}

void make_http_header(char *http_header, char *hostname, char *path,
                  int port,rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE];
  char host_hdr[MAXLINE];
  /*request line*/
  sprintf(request_hdr, requestline_hdr, path);
  /*get other request header for client rio and change it */
  while(Rio_readlineb(client_rio, buf, MAXLINE) > 0)
  {
    if(strcmp(buf, endof_hdr) == 0) 
      break;/*EOF*/
    if(!strncasecmp(buf, host_key, strlen(host_key))) {/*Host:*/
      strcpy(host_hdr, buf);
    }
    if(!strncasecmp(buf, connection_key, strlen(connection_key))
      &&!strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
      &&!strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
    {
      strcat(other_hdr, buf);
    }
  }
  if(strlen(host_hdr) == 0)
    sprintf(host_hdr, host_hdr, hostname);
    sprintf(http_header,"%s%s%s%s%s%s%s",
      request_hdr,
      host_hdr,
      connection_hdr,
      proxy_connection_hdr,
      user_agent_hdr,
      other_hdr,
      endof_hdr);

  return ;
}

/*Connect to the end server*/
inline int connect_server(char *hostname, int port, char *http_header) {
  char portArr[100];
  sprintf(portArr, "%d", port);
  return Open_clientfd(hostname, postArr);
}

/*parse the uri to get hostname,file path ,port*/
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
  *port = 80;
  char* position = strstr(uri,"//");
  char *position2 = strstr(position, ":");

  position = position != NULL ? position + 2 : uri;
  if(position2 != NULL) {
    *position2 = '\0';
    sscanf(position2, "%s", hostname);
    sscanf(position2 + 1, "%d%s", port, path);
  }
  else {
    position2 = strstr(position,"/");
    if(position2 != NULL) {
      *position = '\0';
      sscanf(position, "%s", hostname);
      *position2 = '/';
      sscanf(position2, "%s", path);
    }
    else {
      sscanf(position, "%s", hostname);
    }
  }
  return;
}

