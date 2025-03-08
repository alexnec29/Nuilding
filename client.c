#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>

extern int errno;

int port;

int main(int argc, char *argv[])
{
    int sd;
    struct sockaddr_in server;
    int nr = 0;
    char buf[512];

    if (argc != 3)
    {
        printf("Syntax: %s <server_address> <port>\n", argv[0]);
        return -1;
    }

    port = atoi(argv[2]);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Error at socket().\n");
        return errno;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]Error at connect() to server.\n");
        return errno;
    }
    //blocul de citire/primire client
    while (1) {
        bzero(buf, sizeof(buf));
        printf("NUILDING > ");
        fflush(stdout);
        read(0, buf, sizeof(buf));
        buf[strcspn(buf, "\n")] = '\0';
        
        int write_bytes = write(sd, buf, strlen(buf));

        if(write_bytes < 0) 
        {
            perror("[client]Error at write() to server.\n");
            return errno;
        }
        else if(write_bytes == 0) 
        {
            perror("[client]No command. Disconnecting...\n");
            return errno;
        }

        char buffer[512];
        bzero(buffer, sizeof(buffer));

        int n;
        while ((n = read(sd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[n] = '\0';
            printf("%s", buffer);
            if (n < sizeof(buffer) - 1) break;
            bzero(buffer, sizeof(buffer));
        }

        if (n < 0) 
        {
            perror("[client]Error at read() from server.\n");
            exit(1);
        }
    }
    close(sd);
}

