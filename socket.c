/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/*
 * Build a client side socket connect with a specified server and port
 * 建立一个客户端侧的套接字, 和给定的服务器server的一个端口连接
 * @param: [host] the server address, either in ipv4 or url
 *         服务器地址, ipv4地址或者网址皆可
 * @param: [clientPort] the port to connect
 *         服务器端口
 * @ret  : -1 on failure or the connected socket num
 *         若成功则返回连接的套接字id, 失败则返回-1
 */
int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;
    struct hostent *hp;

    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;

    // here try to translate the host address from char to unsigned long type
    // 试图将字符串形式的服务器地址转换为long类型
    inaddr = inet_addr(host);
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        // the host might be in 'www.google.com' form, do DNS lookup
        // host地址可能是网址形式的, 进行DNS查看
        hp = gethostbyname(host);
        if (hp == NULL)
            return -1;
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
    // translate port num from host order to network order
    // 将端口号由主机序转换为网络序
    ad.sin_port = htons(clientPort);

    // build a TCP ipv4 socket and connect with server
    // 搭建一个TCP的ipv4套接字并和服务器连接
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return sock;
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
    return sock;
}

