#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <linux/limits.h>
#include <libgen.h>
#include <sys/wait.h>

#define MAX_LINE 128

int numProcesses = 0;  //Counts the number of child processes 
int numSubProcesses = 0;  //Counts the number of grandchildren processes for each child process
int numCorruptedFiles = 0; // Counts the number of potential malecious files

const char *monitoredDirName; //Only stores the name of the monitored directory, not the full path

void ExploreDirectories(const char *path, int snapshotFd, char *isolatedPath);

void CreateSnapshot(char *path, char *outputDir, char *isolatedDir);

void PreviousSnapshotCompare(const char *outpuPpath, const char *snapshotFileName);

int CompareSnapshots(const char *outputDir, const char *currentSnapshotFile);

void CheckPermissionsAndAnalyze(const char *entryPath, struct stat fileStats, char *isolatedDir, int snapshotFd);

int AnalyzeFile(const char *entryPath, int pipeFd);

void HandleAnalysisResult(int pipeFd[2], const char *entryPath, char *isolatedDir, pid_t pid);

int main(int argc, char *argv[]) 
{
    write(STDOUT_FILENO, "\n", 1); // Write a newline character to stdout for formatting

    // Check if there are sufficient arguments provided
    if (argc < 6) 
    {
        // Print an error message to stderr and exit with failure status
        write(STDERR_FILENO, "error: Insufficient arguments! Exiting.\n", strlen("error: Insufficient arguments! Exiting.\n"));
        exit(EXIT_FAILURE);
    }

     // Initialize variables to store output path and isolated path
    char *outputPath = NULL;
    char *isolatedPath = NULL;

    // Parse command-line arguments to extract output and isolated paths
    for (int i = 1; i < argc; i++) 
    {
        if (strcmp(argv[i], "-o") == 0) 
        {
            // Set the output path from the next argument
            outputPath = argv[i + 1];
            i++; // Skip the next argument since it's the value for the option
        } 
        else if (strcmp(argv[i], "-s") == 0) 
        {
            // Set the isolated path from the next argument
            isolatedPath = argv[i + 1];
            i++; // Skip the next argument since it's the value for the option
        }
    }

    // Check if both output path and isolated path are provided
    if (outputPath == NULL || isolatedPath == NULL) 
    {
        // Print an error message to stderr and exit with failure status
        write(STDERR_FILENO, "error: Invalid arguments! Exiting.\n", strlen("error: Invalid arguments! Exiting.\n"));
        exit(EXIT_FAILURE);
    }

    pid_t pid;

    // Forking for each directory provided as argument
    for (int i = 1; i < argc; i++) 
    {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-s") == 0) 
        {
            i++; // Skip the next argument 
        } 
        else 
        {
            // Get the path of the directory to process
            char *path = argv[i];
            pid = fork(); // Fork a new process
            numProcesses++; // Increment the count of processes

            if (pid == 0) 
            {
                // Child process: perform snapshot creation and exit with success
                CreateSnapshot(path, outputPath, isolatedPath);
                fprintf(stdout, "Child Process %d terminated (PID: %d) - %d corrupted files found in \"%s\"\n", numProcesses, getpid(), numCorruptedFiles, monitoredDirName);
                return EXIT_SUCCESS;
            } 
            else if (pid < 0) 
            {
                // Fork failed: print an error message and exit with failure
                write(STDERR_FILENO, "Error: Fork failed!\n", strlen("Error: Fork failed!\n"));
                return EXIT_FAILURE;
            }
        }
    }

    // Waiting for all child processes to finish
    for (int i = 1; i < argc; i++) 
    {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-s") == 0) 
        {
            i++; // Skip the next argument
        } 
        else 
        {
            wait(NULL); // Parent process: wait for a child process to terminate
        }
    }

    write(STDOUT_FILENO, "\n", 1); // Write a newline character to stdout for formatting
    return EXIT_SUCCESS;  // Exit the main function with success status
}


void ExploreDirectories(const char *path, int snapshotFd, char *isolatedPath) 
{
    DIR *dir = opendir(path);
    struct dirent *dirEntry;

    char *entryPath = NULL; //Stores the name and path of every directory entry
    char *fileInfo = NULL; //Stores the information of every directory entry

    //Checks if the directory opening was sufccesful, if not, it printsan error messagew and exists the function
    if (!dir) 
    {
        fprintf(stderr, "Error: Failed to open directory \"%s\"\n", monitoredDirName);
        return;
    }

    //Enters a loop that reads each entry (file or subdirectory) within the opened directory.
    while ((dirEntry = readdir(dir)) != NULL) 
    {
        //does not print the entries "." & ".."  in the snapshot file
        if (strcmp(dirEntry->d_name, ".") == 0 || strcmp(dirEntry->d_name, "..") == 0)
            continue;

        //Allocates memory for the full path of the current directory entry (dirEntry). The +2 accounts for the potential separator ('/') and null terminator.
        entryPath = realloc(entryPath, strlen(path) + strlen(dirEntry->d_name) + 2);
        
        //If the memory alication fails, the loop will break and will not monitorise further
        if (!entryPath) 
        {
            fprintf(stderr, "Error: Memory allocation failed for path \"%s\"\n", path);
            free(entryPath);
            break;
        }

        sprintf(entryPath, "%s/%s", path, dirEntry->d_name); //Constructs the path for every entry

        struct stat st;

        //We get information with lstat and print the error message in case of failing
        if (lstat(entryPath, &st) == -1) 
        {
            fprintf(stderr, "Error: Failed to get information for \"%s\"\n", dirEntry->d_name);
            free(entryPath);
            break;
        }
        else 
          CheckPermissionsAndAnalyze(entryPath, st, isolatedPath, snapshotFd);

        //Gets the size of each line used for reallocaating memory 
        size_t dataLength = snprintf(NULL, 0, "Path: %s\nSize: %ld bytes\nPermissions: %c%c%c %c%c%c %c%c%c\nHard Links: %ld\n", entryPath, st.st_size, (st.st_mode & S_IRUSR) ? 'r' : '-', (st.st_mode & S_IWUSR) ? 'w' : '-', (st.st_mode & S_IXUSR) ? 'x' : '-', (st.st_mode & S_IRGRP) ? 'r' : '-', (st.st_mode & S_IWGRP) ? 'w' : '-', (st.st_mode & S_IXGRP) ? 'x' : '-', (st.st_mode & S_IROTH) ? 'r' : '-', (st.st_mode & S_IWOTH) ? 'w' : '-', (st.st_mode & S_IXOTH) ? 'x' : '-', st.st_nlink);

        //Reallocating memory for fileInfo then write in it
        fileInfo = realloc(fileInfo, dataLength + 1);
        
        //Verify if memory allocation works, if not, print error message
        if (!fileInfo) 
        {
            fprintf(stderr, "Error: Memory allocation failed for entry \"%s\"\n", dirEntry->d_name);
            free(fileInfo);
            break;
        }

        sprintf(fileInfo, "Path: %s\nSize: %ld bytes\nPermissions: %c%c%c %c%c%c %c%c%c\nHard Links: %ld\n", entryPath, st.st_size, (st.st_mode & S_IRUSR) ? 'r' : '-', (st.st_mode & S_IWUSR) ? 'w' : '-', (st.st_mode & S_IXUSR) ? 'x' : '-', (st.st_mode & S_IRGRP) ? 'r' : '-', (st.st_mode & S_IWGRP) ? 'w' : '-', (st.st_mode & S_IXGRP) ? 'x' : '-', (st.st_mode & S_IROTH) ? 'r' : '-', (st.st_mode & S_IWOTH) ? 'w' : '-', (st.st_mode & S_IXOTH) ? 'x' : '-', st.st_nlink);

        //Writing to the snapshot file the fileInfo and new line
        write(snapshotFd, fileInfo, strlen(fileInfo));
        write(snapshotFd, "\n", 1);

        //If an entry is a directory, call the function again recursively with a new path
        if (S_ISDIR(st.st_mode)) 
        {
            ExploreDirectories(entryPath, snapshotFd, isolatedPath);
        }
    }

    free(entryPath);
    free(fileInfo);
    closedir(dir);
}


void CreateSnapshot(char *path, char *outputDir, char *isolatedDir) 
{
    char *dirName = basename((char *)path);  //From libgen library, gets the name of the input directory
    monitoredDirName = dirName; //Storing the name in a global variable

    //Checks if the directory given as argument for monitoring exist. If the path is incorrect the error message will be printed to stderr and it will be skipped.
    DIR *dirCheck = opendir(path);
    if (!dirCheck) 
    {
        fprintf(stderr, "Error: Directory \"%s\" does not exist.\n", dirName);
        return;
    }
    closedir(dirCheck);

    //Checking if the output directory given as argument exists. If it does not exists, then the directory will be created.
    dirCheck=opendir(outputDir);
    if (!dirCheck) 
    {
        mkdir(outputDir, 0777);
    }
    closedir(dirCheck);

    //Checking if the isolated directory given as argument exists. If it does not exists, then the directory will be created.
    dirCheck=opendir(isolatedDir);
    if (!dirCheck) 
    {
        mkdir(isolatedDir, 0777);
    }
    closedir(dirCheck);

    char snapshotFilePath[PATH_MAX];  //Buffer for storing the name of the snapshot file
    time_t now = time(NULL);
    struct tm *timestamp = localtime(&now);
    char timestampStr[32];
    time(&now); 

    strftime(timestampStr, sizeof(timestampStr), "%Y.%m.%d_%H:%M:%S", timestamp);

    //Constructing the snapshot file name
    snprintf(snapshotFilePath, sizeof(snapshotFilePath), "%s/%s_Snapshot_%s.txt", outputDir, dirName, timestampStr);

    int snapshotFd = open(snapshotFilePath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    
    if (snapshotFd == -1) 
    {
        fprintf(stderr, "Error: Failed to open snapshot file \"%s\"\n", snapshotFilePath);
        return;
    }

    clock_t startTime = clock(); //Getting the cpu time used for ExploreDirectories function
    ExploreDirectories(path, snapshotFd, isolatedDir);
    clock_t endTime = clock();

    double duration = (double)(endTime - startTime) / CLOCKS_PER_SEC;

    fprintf(stdout, "Snapshot created successfully for \"%s\" in %.2f seconds.\n", dirName, duration);

    close(snapshotFd);
    PreviousSnapshotCompare(outputDir, snapshotFilePath);
}


void PreviousSnapshotCompare(const char *outpuPath, const char *snapshotFileName)
{
    DIR *d=opendir(outpuPath);
    struct dirent *dirEntry;

    //Checks if the directory opening was successful. If opening fails, it prints an error message to stderr and returns from the function.                                                                                
    if(!d)
    {
        fprintf(stderr, "Error: Failed to open the output directory  \"%s\"\n", monitoredDirName);
        return;
    }

    //Initializes variables used to track snapshot numbers and file names.
    int prevSnapshotNr=0;
    int currentSnapshotNo=1;
    char prevSnapshotFileName[FILENAME_MAX];

    //Iterates through each directory entry
    while((dirEntry = readdir(d)) != NULL)
    {
        // Check if the directory entry corresponds to a monitored directory and a snapshot file
        if(!(strstr(dirEntry->d_name, monitoredDirName) == dirEntry->d_name && strstr(dirEntry->d_name, "_Snapshot_") != NULL)) 
          continue;

        // Constructing the name of the previous snapshot file by copying and concatenating strings.
        if(prevSnapshotNr < currentSnapshotNo)
        {
            strcpy(prevSnapshotFileName, outpuPath);    
            strcat(prevSnapshotFileName, "/");            
            strcat(prevSnapshotFileName, dirEntry->d_name);
        }
        prevSnapshotNr++;   
    }
    
    //Checks if a valid previous snapshot file was found.If fewer than 2 snapshot files are found, it means no previous snapshot exists, and it prints a message before returning.
    if(prevSnapshotNr < 2)
    {
        fprintf(stdout, "No snapshots were previously created for  \"%s\"\n", monitoredDirName);
        return;
    }
        
    //Calls the CompareSnapshots function to compare the current snapshot with the previous snapshot 
    int IsDifferent=CompareSnapshots(prevSnapshotFileName, snapshotFileName);

    //Depending on the comparison result, it prints a message indicating whether differences were found between snapshots.
    if(!IsDifferent) //If no differences, it deletes the previous snapshot file.
    {   
        fprintf(stdout, "No differences found between the current and the previous snapshot for  \"%s\"\n", monitoredDirName);
        unlink(prevSnapshotFileName); 
    }
    else //If differences exist, it overrides the previous snapshot by renaming the current snapshot file.
    {
        fprintf(stdout, "Difference found between the current and the previous snapshot => Overriding the previous snapshot for  \"%s\"\n", monitoredDirName);
        unlink(prevSnapshotFileName);  
        rename(snapshotFileName, prevSnapshotFileName);  
    } 
    
    closedir(d);
}


int CompareSnapshots(const char *prevSnapshotFile, const char *currentSnapshotFile) 
{
    int snapshotFdCurrent=open(currentSnapshotFile, O_RDONLY, S_IRUSR); // Open the current snapshot file for reading
    
    // Print error message if failed to open current snapshot file
    if(snapshotFdCurrent == -1)
    {
        fprintf(stderr, "Error: Failed to open the current snapshot file for  \"%s\"\n", monitoredDirName);
        return -1;
    }

    int snapshotFdPrev=open(prevSnapshotFile, O_RDONLY, S_IRUSR); // Open the previous snapshot file for reading

    // Print error message if failed to open previous snapshot file                                                      
    if(snapshotFdPrev == -1)
    {
        fprintf(stderr, "Error: Failed to open the previous snapshot file for  \"%s\"\n", monitoredDirName);
        return -1;
    }

    //Initializes variables
    char currentLine[MAX_LINE]; //store lines read from snapshot files
    char prevLine[MAX_LINE]; //store lines read from snapshot files

    int IsDifferent=0; //track whether differences are found between snapshot files
    
    ssize_t currentRead, prevRead; //store the number of bytes read from each file

    do
    { 
        //Reads lines from both snapshot files until the end of either file is reached
        currentRead=read(snapshotFdCurrent, currentLine, sizeof(currentLine)-1);
        prevRead=read(snapshotFdPrev, prevLine, sizeof(prevLine)-1);

        currentLine[currentRead]='\0';
        prevLine[prevRead]='\0';

        //Checks if the number of bytes read are different.Compares the content of the read lines using strcmp
        if((currentRead!=prevRead) || strcmp(currentLine, prevLine)!=0)
        {
            IsDifferent=1; //If differences are found (line lengths differ or content is different), sets IsDifferent to 1 and breaks out of the loop.
            break;
        }
    }
    while(currentRead > 0 && prevRead > 0); //Reading and comparing untill end of files

    //Closes the file descriptors of both snapshot files after reading and comparison are completed
    close(snapshotFdCurrent);
    close(snapshotFdPrev);

    return IsDifferent; //Returns IsDifferent (0 if no differences were found, 1 if differences were found) to the calling function (PreviousSnapshotCompare).
}


void CheckPermissionsAndAnalyze(const char *entryPath, struct stat filePermission, char *isolatedDir, int snapshotFd) 
{
    pid_t pid;
    int pipe_fd[2];

    // Create a pipe for inter-process communication
    if (pipe(pipe_fd) == -1) 
    {
        // If pipe creation fails, print an error message and return
        write(STDERR_FILENO, "Error: Pipe creation failed!\n", strlen("Error: Pipe creation failed!\n"));
        return;
    }

    // Check if file has no access rights (read, write, execute)
    if (!(filePermission.st_mode & S_IXUSR) && !(filePermission.st_mode & S_IRUSR) && !(filePermission.st_mode & S_IWUSR) &&
        !(filePermission.st_mode & S_IRGRP) && !(filePermission.st_mode & S_IWGRP) && !(filePermission.st_mode & S_IXGRP) &&
        !(filePermission.st_mode & S_IROTH) && !(filePermission.st_mode & S_IWOTH) && !(filePermission.st_mode & S_IXOTH)) 
    {
         // Print message indicating no access rights and perform syntactic analysis
        fprintf(stdout, "No access rights for \"%s\" in \"%s\" => Performing Syntactic Analysis.\n", basename((char *)entryPath), monitoredDirName);

        int fileStatus;
        pid = fork(); // Create a new process (child process)
        numSubProcesses++; // Create a new process (child process)

        if (pid == 0) 
        {
            // Child process: close unnecessary file descriptors and execute file analysis
            close(snapshotFd); // Close the file descriptor associated with the snapshot
            close(pipe_fd[0]); // Close the read end of the pipe

            fileStatus = AnalyzeFile(entryPath, pipe_fd[1]); // Execute file analysis and write results to pipe

            close(pipe_fd[1]); // Close the write end of the pipe
            exit(fileStatus); // Exit the child process with the file analysis status
        } 
        else if (pid < 0) 
        {
            // Fork failed: print an error message and return
            write(STDERR_FILENO, "Error: Fork failed for child process!\n", strlen("Error: Fork failed for child process!\n"));
            return;
        } 
        else 
        {
            // Parent process: handle analysis result from the child process
            HandleAnalysisResult(pipe_fd, entryPath, isolatedDir, pid);
        }

        // Print termination message for the subprocess
        fprintf(stdout, "Subprocess %d.%d (PID: %d) terminated with exit code %d for \"%s\" in \"%s\"\n", numProcesses, numSubProcesses, getpid(), pid, basename((char *)entryPath), monitoredDirName);
        write(STDOUT_FILENO, "\n", 1);  // Write a newline to stdout
    }
}


int AnalyzeFile(const char *entryPath, int pipeFd) 
{
    int giveAccess = chmod(entryPath, S_IRUSR); // Grant read permission to the file using chmod

    // Create a command string to execute a shell script for analyzing file for malicious content
    char scriptCommand[100];
    snprintf(scriptCommand, sizeof(scriptCommand), "./verify_for_malicious.sh \"%s\"", entryPath);

    int fileStatus = system(scriptCommand); // Execute the shell script command and capture its exit status
    write(pipeFd, &fileStatus, sizeof(fileStatus)); // Write the exit status of the shell script to the pipe for communication with the parent process
    close(pipeFd); // Close the write end of the pipe

    giveAccess = chmod(entryPath, 0); // Revoke read permission from the file
    close(giveAccess); // Close the file descriptor associated with the permission change

    return fileStatus; // Return the exit status of the shell script (indicating analysis result)
}

void HandleAnalysisResult(int pipeFd[2], const char *entryPath, char *isolatedDir, pid_t pid) 
{
    close(pipeFd[1]); // Close the write end of the pipe (not needed for reading)

    // Read the analysis result (fileStatus) from the pipe
    int fileStatus;
    read(pipeFd[0], &fileStatus, sizeof(fileStatus));
    close(pipeFd[0]); // Close the read end of the pipe after reading

    // Check the analysis result 
    if (fileStatus != 0) 
    {
        // Prepare isolated directory path for moving the corrupted/malicious file
        if (isolatedDir[strlen(isolatedDir) - 1] != '/')
            strcat(isolatedDir, "/");

        strcat(isolatedDir, basename((char *)entryPath));
        rename(entryPath, isolatedDir); // Move the corrupted/malicious file to the isolated directory

        numCorruptedFiles++; // Increment the count of corrupted/malicious files
        
        // Print a message indicating the file is malicious or corrupted and has been moved
        fprintf(stdout, "\"%s\" in \"%s\" is malicious or corrupted => Moved to isolated directory.\n", basename((char *)entryPath), monitoredDirName);
    } 
    else 
    {
        // Print a message indicating the file is safe
        fprintf(stdout, "\"%s\" in \"%s\" is safe.\n", basename((char *)entryPath), monitoredDirName);
    }
}
