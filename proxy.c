#include <stdio.h>
#include "csapp.h"

#define MAX_OBJECT_SIZE 7204056
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *requestline_hdr = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd );
void parse_uri(char *uri,char *hostname,char *path,int *port);
void make_http_header(char *http_header,char *hostname,char *path,int port,
    rio_t *client_rio);
int connect_server(char *hostname,int port,char *http_header);
void clienterror(int fd, char *cause, char *errnum, 
    char *shortmsg, char *longmsg); 

/* ------ Cache that stores the most recently-used Web object in memory -----
   It contains the components of:
   1. url - that stores the uri that needs to be cached
   2. response_hdr - that stores the response headers that need to be cached
   3. destination_buf - the final buffer in which we cache the file content
   4. cache_bytes - keeps track of the number of bytes cached
*/
typedef struct cache_block{
  char url[MAXLINE]; 
  char response_hdr[MAXLINE];
  char *destination_buf;
  int cache_bytes;
}cache_block;

cache_block cache;

/* Pretty much the same "main" as tiny.c */
int main(int argc,char **argv)
{ 
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Initialize the cache url and the response headers to be empty */
  strcpy(cache.url, "");
  strcpy(cache.response_hdr, "");

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

/* -------------- Handle the client HTTP transaction ------------------
   1. Create two rio - one for the client and one for the server. 
   2. Also create a temporary buffer of size MAX_OBJECT_SIZE that will store  
      the file content.
   3. We first read it the client request line in three parts: method, uri and 
      version. Since we are only taking care of the method GET, if it is 
      not GET, we report an error.
   4. Now we parse the uri. The reason I have made a copy of the uri before 
      parsing is that my parse uri function changes the uri and hence after 
      returning I restore the original uri by copying the uri copy back into 
      the uri.
   5. We then call the make_http_header that builds the request header that 
      needs to be sent to the server.
   6. We then check if the uri is already in the cache. If it is, we do not 
      connect to the server and if it is not, we connect to the server and 
      store it into the cache for future use.
*/
void doit(int connfd)
{
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char server_http_header [MAXLINE];
  
  /* Store the request line arguments */
  char hostname[MAXLINE], path[MAXLINE];
  int port;
  
  /* rio_client is client's rio
     rio_server is server's rio */
  rio_t rio_client, rio_server;
  rio_readinitb(&rio_client, connfd);
  
  if (rio_readlineb(&rio_client, buf, MAXLINE) < 0)
    printf("Error\n");

  char cache_buf[MAX_OBJECT_SIZE];

  /* Read the client request line */
  sscanf(buf,"%s %s %s", method, uri, version);

  if(strcasecmp(method,"GET")) {
    clienterror(connfd, method, "501", "Not Implemented", 
        "Proxy does not implement this method");
    return;
  }

  /* Parse the uri to GET hostname, file path, port */
  char uri_copy[MAXLINE];
  strcpy(uri_copy, uri);
  parse_uri(uri, hostname, path, &port);
  strcpy(uri, uri_copy);

  /* Build the http header which we then send to the server */
  make_http_header(server_http_header, hostname, path, port, &rio_client);
  
  /* If the uri matches the cache url, i.e. if the uri has already been seen 
     before and is in the cache, we do not connect to server. Instead, we 
     directly send to client the appropriate response headers and file content 
     directly from the cache. In this way we avoid making a connection to the 
     server everytime the same service is requested.
  */
  if (strcmp(uri, cache.url) == 0) {
    if (rio_writen(connfd, cache.response_hdr, strlen(cache.response_hdr) < 0))
      printf("Error\n");
    if (rio_writen(connfd, cache.destination_buf, cache.cache_bytes) < 0)
      printf("Error\n");
  }
  /* If the uri and cache url do not match, i.e. it is a new request that is 
     not in the cache, we first connect to the server and read. In order to 
     put these new reponse headers and file contents into the cache, we have 
     two loops that respectively store them into the cache.
  */
  else {
    serverfd = connect_server(hostname, port, server_http_header);
    if(serverfd < 0) {
      printf("connection failed\n");
      return;
    }
    rio_readinitb(&rio_server, serverfd);
    /* Write the http header to server */
    if (rio_writen(serverfd, server_http_header, strlen(server_http_header))<0)
      printf("Error\n");
    size_t n;
    int size = 0;

    /* -------- Response Header ---------
    This loop reads line by line from the server and then writes. We also 
    store the response headers into the cache.
    */
    while(strcmp(buf, "\r\n") > 0) {
      if((n = rio_readlineb(&rio_server, buf, MAXLINE)) < 0)
        printf("Error\n");
      if (rio_writen(connfd, buf, n) < 0)
        printf("Error\n");
      strcat(cache.response_hdr, buf);
    }
    /* --------- File Content -----------
    This loop reades n bytes from the server and then based on whether or not 
    the total bytes read is less than or equal to MAX_OBJECT_SIZE we store it 
    into our cache. We set a counter "size" that keeps track of the total 
    bytes. If the total is less than our maximum limit, we store them into 
    the temporary cache buffer.
    */
    while((n = rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
      if((size+n) <= MAX_OBJECT_SIZE) {
        memcpy(cache_buf + size, buf, n);
        size+=n;
      }
      if (rio_writen(connfd, buf, n) < 0)
        printf("Error\n");
    }
    /* If we are sure that we have not read more than our limit, we store 
       those many bytes of file content as well as the uri into our cache
    */
    if (size <= MAX_OBJECT_SIZE) {
      cache.destination_buf = (char*)malloc(size);
      memcpy(cache.destination_buf, cache_buf, size);
      strcpy(cache.url, uri);
    }
    cache.cache_bytes = size;
    Close(serverfd);
  }
}

/* Build the http header which we then send to the server */
void make_http_header(char *http_header, char *hostname, char *path,
    int port,rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE];
  char host_hdr[MAXLINE];
  
  /* Request line and get other request header for client rio and change it */
  sprintf(request_hdr, requestline_hdr, path);
  while(rio_readlineb(client_rio, buf, MAXLINE) > 0)
  {
    /* End of file */
    if(strcmp(buf, endof_hdr) == 0) 
      break;
    /*Host:*/
    if(!strncasecmp(buf, host_key, strlen(host_key))) {
      strcpy(host_hdr, buf);
    }
    if(!strncasecmp(buf, connection_key, strlen(connection_key))
        &&!strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
        &&!strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
    {
      strcat(other_hdr, buf);
    }
  }
  /* Print the headers */
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
  printf ("%s", http_header);
  return ;
}

/* This function connects to the end server by using the hostname and port */
int connect_server(char *hostname, int port, char *http_header) {
  char portArr[100];
  sprintf(portArr, "%d", port);
  return Open_clientfd(hostname, portArr);
}

/* ------ Parse the uri to get hostname, file path and port ------- 
1. Set port to be 80 if not given
2. Set two pointers. One at // and the other at :
3. If the first pointer is not null, then we move it ahead by 2. Otherwise 
   set it to the uri
4. If the second pointer is not null, we add a terminator and read the 
   hostname to the first pointer while reading the poth and path to the 
   second pointer.
5. If it is null, we set it to start at /.
6. Now if it not null, we again add a terminator and read the hostname into 
   the first pointer. But this time for the second pointer, only the path, 
   since it is after the /. Otherwise there is no path.
Note: Since I have pointers, the value of uri changes. Hence I have made a 
copy of it before calling this function
*/
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
  *port = 80;
  char* position = strstr(uri,"//");
  char *position2 = strstr(position, ":");

  if (position != NULL)
    position += 2;
  else
    position = uri;
  
  if(position2 != NULL) {
    *position2 = '\0';
    sscanf(position, "%s", hostname);
    sscanf(position2 + 1, "%d%s", port, path);
  }
  else {
    position2 = strstr(position,"/");
    if(position2 != NULL) {
      *position2 = '\0';
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

/* clienterror - returns an error message to the client */
void clienterror(int fd, char *cause, char *errnum, 
    char *shortmsg, char *longmsg) 
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  sprintf(buf, "%sContent-type: text/html\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}
