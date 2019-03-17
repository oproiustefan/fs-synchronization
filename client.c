#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "common.h"
#include "client.h"

char *ip;
int port;
int sockfd;

struct sockaddr_in server_address;
char *root;

char *formatdate(time_t val)
{
	char *str;

	if ((str = (char *) malloc(48)) == NULL)
		displayError("malloc() error.");

    strftime(str, 48, "%Y%m%d%H%M.%S", localtime(&val));
    return str;	
}

int adjustTimestamp(char *filepath, char *timestamp)
{
	char cmd[PATH_MAX + 32];
    int status, exitcode;

    snprintf(cmd, sizeof(cmd), "touch -a -m -t %s \"%s\"", timestamp, filepath);

    status = system(cmd);
    exitcode = WEXITSTATUS(status);

    return exitcode;
}

int getIndexFromServerFiles(fm *server_files, int count, char *path)
{
	int i;

	for (i = 0; i < count; i++)
	{
		if (!strcmp(server_files[i].path, path))
			return i;
	}

	return -1;
}

int removedir(char *dirpath)
{
	char cmd[PATH_MAX + 64];
	int status, exitcode;

    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", dirpath);

    status = system(cmd);
    exitcode = WEXITSTATUS(status);

    return exitcode;
}

int removefile(char *path)
{
	char cmd[PATH_MAX + 64];
	int status, exitcode;

    snprintf(cmd, sizeof(cmd), "rm \"%s\"", path);

    status = system(cmd);
    exitcode = WEXITSTATUS(status);

    return exitcode;
}

void handle()
{
	char buff[1024];
	char newpath[PATH_MAX];
	int server_files_count;
	int r = 0, i, size;

	fm *server_files;

	/*
	* Receiving files count from the server.
	*/

	r = read(sockfd, buff, 10);
	buff[r] = 0;

	server_files_count = atoi(buff);

	#if defined DEBUG_MODE
		printf("[debug - client]: server tells that are %d files there\n", server_files_count);
	#endif

	/*
	* Allocate memory for the server files to check if there are outdated/files that need to be deleted.
	*/

	if ((server_files = (fm *) malloc(server_files_count * sizeof(fm))) == NULL)
		displayError("malloc error.");

	/*
	* Reading them from the socket.
	*/

	for (i = 0; i < server_files_count; i++)
		read(sockfd, &server_files[i], sizeof(fm));

	/*
	* Getting files that need to be updated/deleted.
	*/

	getFilesToUpdate(server_files, server_files_count);
	getFilesToDelete(server_files, server_files_count);

	/*
	* Telling the server how many files need updated by the client.
	*/

	#if defined DEBUG_MODE
		printf("[debug - client]: %d files need updated/added\n", ftu_count);
	#endif

	snprintf(buff, 10, "%d", ftu_count);
	write(sockfd, buff, 10);

	for (i = 0; i < ftu_count; i++)
	{
		#if defined DEBUG_MODE
			printf("[debug - client]: sending '%s' to server\n", files_to_update[i].path);
		#endif

		write(sockfd, files_to_update[i].path, PATH_MAX);
	}

	/*
	* Updating the files from the server.
	*/

	for (i = 0; i < ftu_count; i++)
	{
		int idx;
		int serverFileIdx = getIndexFromServerFiles(server_files, server_files_count, files_to_update[i].path);

		/*
		* Building the absolute path.
		*/

		snprintf(newpath, PATH_MAX, "%s/%s", root, files_to_update[i].path);

		#if defined DEBUG_MODE
			printf("[debug - client]: file '%s' is being updated.\n", newpath);
		#endif

		if (!files_to_update[i].is_regular_file) // is a directory
		{
			mkdir(newpath, ACCESSPERMS);
		}
		else // is a file, get data from socket and write into it
		{
			int fd = open(newpath, O_CREAT | O_TRUNC | O_WRONLY, 0744);
			off_t size;

			if (fd < 0)
				displayError("open() error.");

			read(sockfd, buff, 12);
			size = strtol(buff, NULL, 10);

			read(sockfd, buff, size);
			write(fd, buff, size);

			if (close(fd) < 0)
				displayError("close() error.");
		}

		char *date = formatdate(server_files[serverFileIdx].timestamp);
		adjustTimestamp(newpath, date);

		#if defined DEBUG_MODE
			printf("[debug - client]: timestamp=%s for file %s\n", date, files_to_update[i].path);
		#endif

		if ((idx = getPathIndex(newpath)) == -1) // file/dir does not exists in the list => add it
		{
			strcpy(own_files[paths_count].path, newpath);

			own_files[paths_count].size = server_files[serverFileIdx].size;
			own_files[paths_count].timestamp = server_files[serverFileIdx].timestamp;

			paths_count ++;
		}
		else // update its stats
		{
			own_files[idx].size = server_files[serverFileIdx].size;
			own_files[idx].timestamp = server_files[serverFileIdx].timestamp;
		}

		#if defined DEBUG_MODE
			printf("[debug - client]: file '%s' successfully updated.\n", newpath);
		#endif
	}

	/*
	* Deleting the files that are on the clients' machine but not on the server.
	*/

	for (i = 0; i < ftd_count; i++)
	{
		int idx, j;

		snprintf(newpath, PATH_MAX, "%s/%s", root, files_to_delete[i].path);

		#if defined DEBUG_MODE
			printf("[debug - client]: file '%s' is being deleted.\n", newpath);
		#endif

		if (!files_to_delete[i].is_regular_file) // is a directory
		{
			removedir(newpath);
		}
		else // is a file, remove it
		{
			if (strlen(newpath) != strlen(root) + strlen(files_to_delete[i].path) + 1)
				removefile(newpath);
		}

		if ((idx = getPathIndex(newpath)) != -1)
		{
			for (j = idx; j < paths_count - 1; j++)
				own_files[j] = own_files[j + 1];

			paths_count --;
		}

		#if defined DEBUG_MODE
			printf("[debug - client]: file '%s' successfully deleted.\n", newpath);
		#endif
	}
}

void clientSetup()
{
	if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		exit(EXIT_FAILURE);
	}

	server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(ip);
    server_address.sin_port = htons(port);

    if (connect(sockfd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
    {
    	exit(EXIT_FAILURE);
    }

    getAllFilesPaths(root);
    getAllFilesMetadata(root);

    handle();
}

int main(int argc, char *argv[])
{
	if (argc != 3)
    {
        printf("USAGE: %s <config_file_path> <root>.\n", argv[0]);
        exit(1);
    } 

    if (argv[2][strlen(argv[2]) - 1] == '/')
    {
    	printf("[ERROR]: Root shouldn't end with the '/' character.\n");
    	exit(1);
    }

    read_params(argv[1]);

    root = argv[2];
    clientSetup();

    return 0;
}