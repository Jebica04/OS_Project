#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

//Description: The proposed project combines functionalities for monitoring a directory to manage differences between two captures (snapshots) of it.
// The user will be able to observe and intervene in the changes in the monitored directory.

//Directory Monitoring:
//The user can specify the directory to be monitored as an argument in the command line, and the program will track
// changes occurring in it and its subdirectories, parsing recursively each entry from the directory.
//With each run of the program, the snapshot of the directory will be updated, storing the metadata of each entry.

//FUNCTION TO READ THE DIRECTORY PUT AS ARGUMENT IN TERMINAL & RECURSIVELY TRAVERSE EVERY 
//SUB_DIRECTORY FROM IT. IT SAVES IN A SNAPSHOT.TXT THE PATH AND NAME OF EVERY FILE

void listFilesRecursively(const char *path, int snapshot_fd)
{
    DIR *d = opendir(path);
    struct dirent *dir_file;
    char *new_path=NULL;
    if(d)
    {
        while((dir_file = readdir(d)) != NULL)
        {
            if(strcmp(dir_file->d_name, ".") == 0 || strcmp(dir_file->d_name, "..") == 0)
            {
                continue; //all the directories have the entries "." & ".." 
                          //i'm not printing them in the Snapshot file. 
            }

            new_path=realloc(new_path,strlen(path)+strlen(dir_file->d_name) +2); //+2 is for '/' and null terminator

            if(new_path==NULL)
            {                                             
                write(STDERR_FILENO, "error: Failed to allocate memory for path\n", strlen("error: Failed to allocate memory for path\n"));
                exit(EXIT_FAILURE);
            }

            sprintf(new_path,"%s/%s", path, dir_file->d_name); //constructing the path

            struct stat st;                            
            if (lstat(new_path, &st) == -1) 
            {                                                                                       
                 //get file information with lstat
                write(STDERR_FILENO, "error: Failed to get information for\n", strlen("error: Failed to get information for\n"));    
                //& print error message in case of failing
                free(new_path);                  
                continue;
            }

            write(snapshot_fd, new_path, strlen(new_path)); //writing to the snapshot file
            write(snapshot_fd, "\n", 1);

            if(S_ISDIR(st.st_mode))
            {       
                //if an entry is a directory
                //recursively call again the function with the new path     
                listFilesRecursively(new_path, snapshot_fd);
            }
        }

        free(new_path);
        closedir(d);
    }
    else
    {
        write(STDERR_FILENO, "error: Failed to open the directory\n", strlen("error: Failed to open the directory\n"));
        exit(EXIT_FAILURE);
    } 
}

void createSnapshot(const char *basePath) 
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char filename[100];
    sprintf(filename, "snapshot_%d-%02d-%02d_%02d-%02d-%02d.txt", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    int snapshotFile = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (snapshotFile == -1) 
    {
        perror("open");
        exit(EXIT_FAILURE);
    }

    listFilesRecursively(basePath, snapshotFile);

    close(snapshotFile);
    printf("Snapshot created: %s\n", filename);
}

int main(int argc, char *argv[])
{
    
    if(argc == 2)
    {
        char *path = argv[1];
        createSnapshot(path);
        write(STDERR_FILENO, "Snapshot created successfully!\n", strlen("Snapshot created successfully!\n"));
    }
    else
    {
        write(STDERR_FILENO, "error: There should be only one argument!\n", strlen("error: There should be only one argument!\n"));
        exit(EXIT_FAILURE);
    }
    return 0;
}