#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

int ip, port;

void read_params(char *path)
{
    FILE *f = fopen(path, "r");
    char *s_ip = (char *)malloc(20), *s_port = (char *)malloc(20);
    fscanf(f, "ip=%s\nport=%s", s_ip, s_port);
    if (f)
        fclose(f);
    ip = atoi(s_ip);
    port = atoi(s_port);
    printf("%d %d\n", ip, port);
}

void server() {
    
}

int main(int argc, char *args[])
{
    if (argc != 3)
    {
        printf("Usage %s <config_file_path> <root>.\n", args[0]);
        exit(1);
    }
    read_params(args[1]);
    return 0;
}