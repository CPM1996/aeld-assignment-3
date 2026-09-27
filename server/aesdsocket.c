#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <syslog.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#define SOCKETDATA_PATH "/var/tmp/aesdsocketdata"
#define AESDSOCKET_PORT_STR "9000"

int globalServerRunning = 1;

void signalHandler(int)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    globalServerRunning = 0;
}

int attendConn(int connSocketFd, int fileFd);
int processPacket(int connSocketFd, int fileFd, char *buf, int len);

int main(const int argc, const char *argv[])
{
    struct sigaction sigAction = {0};
    int isDaemon = 0;

    char sLogBuff[128];
    int ret;
    
    
    int socketfd;
    struct addrinfo hints = {0};
    struct addrinfo *pAddrInfo = NULL;
    // struct sockaddr_in bindSAddrIn = {0};
    
    int connSocketFd;
    struct sockaddr_in connSockAddr;
    socklen_t conSockAddrLen = sizeof(connSockAddr);

    int fileFd = -1;

    if(argc > 2)
    {
        printf("Error: only one argument is accepted\n");
        return -1;
    }

    if((argc == 2) && (!strcmp(argv[1], "-d")))
    {
        isDaemon = 1;
    }

    openlog("aesdsocket", 0, LOG_USER);

    sigAction.sa_handler = signalHandler;
    // sigAction.sa_flags = SA_RESTART;

    sigaction(SIGINT, &sigAction, NULL);
    sigaction(SIGTERM, &sigAction, NULL);

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0)
    {
        snprintf(sLogBuff, sizeof(sLogBuff), "Error while creating socket: %s", strerror(errno));
        syslog(LOG_ERR, sLogBuff);
        return -1;
    }

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    ret = getaddrinfo(NULL, AESDSOCKET_PORT_STR, &hints, &pAddrInfo);
    if(ret != 0)
    {
        snprintf(sLogBuff, sizeof(sLogBuff), "Error on getaddrinfo(): %s", gai_strerror(ret));
        syslog(LOG_ERR, sLogBuff);
        close(socketfd);
        return -1;
    }

    ret = bind(socketfd, (struct sockaddr *) pAddrInfo->ai_addr, pAddrInfo->ai_addrlen);
    freeaddrinfo(pAddrInfo);
    if(ret != 0)
    {
        snprintf(sLogBuff, sizeof(sLogBuff), "Error while binding socket: %s", strerror(errno));
        syslog(LOG_ERR, sLogBuff);

        close(socketfd);
        return -1;
    }

    ret = listen(socketfd, 16);
    if(ret != 0)
    {
        snprintf(sLogBuff, sizeof(sLogBuff), "Error on listen(): %s", strerror(errno));
        syslog(LOG_ERR, sLogBuff);

        close(socketfd);
        return -1;
    }

    //TODO fork()
    if(isDaemon)
    {
        ret = fork();
        if(ret <0)
        {
            snprintf(sLogBuff, sizeof(sLogBuff), "Error on listen(): %s", strerror(errno));
            syslog(LOG_ERR, sLogBuff);

            close(socketfd);
            return -1;
        }
        else if(ret > 0) // Parent process
        {
            return 0;
        }
    }

    fileFd = open(SOCKETDATA_PATH, O_RDWR | O_CREAT | O_APPEND, S_IRWXU);
    if(fileFd < 0)
    {
        snprintf(sLogBuff, sizeof(sLogBuff), "Error on open(): %s", strerror(errno));
        syslog(LOG_ERR, sLogBuff);

        close(socketfd);
        return -1;
    }

    while(globalServerRunning)
    {
        conSockAddrLen = sizeof(connSockAddr);

        connSocketFd = accept(socketfd, (struct sockaddr *) &connSockAddr, &conSockAddrLen);
        if(connSocketFd < 0)
        {
            if(errno != EINTR)
            {
                snprintf(sLogBuff, sizeof(sLogBuff), "Error on accept(): %s", strerror(errno));
                syslog(LOG_ERR, sLogBuff);
            }
            break;
        }

        snprintf(sLogBuff, sizeof(sLogBuff), "Accepted connection from %s",
            inet_ntoa(connSockAddr.sin_addr));
        syslog(LOG_INFO, sLogBuff);
        

        attendConn(connSocketFd, fileFd);
        
        close(connSocketFd);
        snprintf(sLogBuff, sizeof(sLogBuff), "Closed connection from %s",
            inet_ntoa(connSockAddr.sin_addr));
        syslog(LOG_INFO, sLogBuff);
    }

    if(fileFd != -1)
    {
        close(fileFd);
    }

    remove(SOCKETDATA_PATH);
    close(socketfd);

    return 0;
}

int attendConn(int connSocketFd, int fileFd)
{
    int recvSize;
    int recvCompleted = 0;
    char dataBuffer[256];

    char *fullDataBuffer = NULL;
    int fullDataBufferSize = 0, fullDataBufferLen = 0;
    int i;
    char *src, *dst;

    while(!recvCompleted)
    {
        recvSize = recv(connSocketFd, dataBuffer, sizeof(dataBuffer), SA_RESTART);
        if((recvSize < 0) && (errno != EINTR))
        {
            recvCompleted = 1;
            snprintf(dataBuffer, sizeof(dataBuffer), "Error on recv(): %s", strerror(errno));
            syslog(LOG_ERR, dataBuffer);
        }
        else if(recvSize == 0)
        {
            recvCompleted = 1;
        }
        else
        {
            if(fullDataBufferLen + recvSize > fullDataBufferSize)
            {
                fullDataBufferSize += (recvSize > 1024) ? recvSize+1 : 1024;
                fullDataBuffer = realloc(fullDataBuffer, fullDataBufferSize);
                if(fullDataBuffer == NULL)
                {
                    snprintf(dataBuffer, sizeof(dataBuffer), "Error on realloc(): %s", strerror(errno));
                    syslog(LOG_ERR, dataBuffer);
                    return -1;
                }
            }

            for(i = 0, src = dataBuffer, dst = &fullDataBuffer[fullDataBufferLen];
                i < recvSize;
                i++, fullDataBufferLen++, src++, dst++)
            {
                *dst = *src;
                if(*dst == '\n')
                {
                    processPacket(connSocketFd, fileFd, fullDataBuffer, fullDataBufferLen+1);
                    
                    fullDataBufferLen = 0;
                    dst = fullDataBuffer;
                }
            }
        }
    }

    return 0;
}

int processPacket(int connSocketFd, int fileFd, char *buf, int len)
{
    char readBuff[256];
    int readChars;
    int ret;

    lseek(fileFd, 0, SEEK_END);
    ret = write(fileFd, buf, len);
    if((ret < len) && (errno != EINTR))
    {
        return -1;
    }

    lseek(fileFd, 0, SEEK_SET);
    
    errno = 0;
    do
    {
        readChars = read(fileFd, readBuff, sizeof(readBuff));
        if((readChars > 0))
        {
            send(connSocketFd, readBuff, readChars, 0);
        }

        if(errno == EINTR)
        {
            readChars = 1;
            errno = 0;
        }
    }while(readChars > 0);
}