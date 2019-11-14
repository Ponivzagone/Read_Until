#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <utility>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cstring>

#define size 32768

int readn(int s, char *__restrict bp, size_t len)
{
    int cnt;
    int rc;

    cnt = len;

    while (cnt > 0)
    {
        rc = recv(s, bp, cnt, 0);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (rc == 0)
            return len - cnt;

        bp += rc;
        cnt -= rc;
    }
    return len;
}

std::pair<size_t, bool> partial_search(
    const char *__restrict review, size_t size1,
    const char *__restrict required, size_t size2)
{
    for (size_t i = 0; i < size1; ++i)
    {
        size_t try_i1 = i;
        size_t try_i2 = 0;
        for (;; ++try_i1, ++try_i2)
        {
            if (try_i2 == size2)
                return std::make_pair(i, true);
            if (try_i1 == size1)
            {
                if (try_i2 != 0)
                    return std::make_pair(i, false);
                else
                    break;
            }
            if (review[try_i1] != required[try_i2])
                break;
        }
    }
    return std::make_pair(size1, false);
}

size_t read_until(int s,
                  char *__restrict buffers,
                  size_t size1,
                  const char *__restrict delim,
                  size_t size2)
{
    size_t end_size = 0;
    size_t search_position = 0;
    for (;;)
    {
        size_t end = end_size - search_position;

        std::pair<size_t, bool> result = partial_search(
            buffers + search_position, end, delim, size2);
        if (result.first != end)
        {
            if (result.second)
            {
                return result.first + size2;
            }
            else
            {
                search_position = result.first;
            }
        }
        else
        {
            search_position = end_size;
        }

        if (size1 == search_position)
        {
            return -1;
        }

        size_t bytes_to_read = std::min<size_t>(4096, (size - search_position));
        if ((end_size = readn(s, buffers, bytes_to_read)) < 0)
            return end_size;
    }
}

int main(int argc, char **argv)
{
    int port;
    std::string ip;
    std::string url;
    if (argc != 3)
    {
        ip = "128.30.52.21";
        url = "/HTTP/ChunkedScript";
        port = 80;
        std::cout << "Usage: sync_client <128.30.52.21> <80> </HTTP/ChunkedScript>\n";
    }
    else
    {
        std::stringstream ss;
        ss << argv[2];
        ss >> port;
        ip = argv[1];
        url = argv[2];
    }
    std::stringstream ss;

    ss << "GET " << url << " HTTP/1.1\r\n";
    ss << "Host: " << ip << "\r\n";
    ss << "Accept: text/html\r\n";
    ss << "Connection: keep-alive\r\n";
    ss << "\r\n";

    char buff[size];

    int sock;
    struct sockaddr_in addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("connect");
        exit(2);
    }

    if (send(sock, ss.str().c_str(), ss.str().length(), 0) == -1)
    {
        perror("send");
    }
    printf("Send errno: ");
    printf("%d\n", errno);

    int len;
    len = read_until(sock, buff, size, "\r\n\r\n", 4);

    //...
    // Parse Header;
    //...
    char tmp = buff[len];
    buff[len] = '\0';
    printf("errno: %d\n", errno);
    printf("receive %d\n%s\n", len, buff);

    buff[len] = tmp;
    printf("add response\n%s\n", buff + len);

    close(sock);

    return 0;
}
