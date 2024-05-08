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

int countProcesses=0; 
int countGrandchildProcesess=0; 
int countCorrupted=0; 



void exploreDirectories(const char *path, int snapshotFd, const char *baseDirName, char *isoPath) 
{
    DIR *dir = opendir(path);
    if (!dir) 
    {
        fprintf(stderr, "Error: Failed to open directory \"%s\"\n", path);
        return;
    }

    struct dirent *entry;

    char *fullPath = NULL;
    char *entryInfo = NULL;

    char *current_dir_name = basename((char *)path);

    while ((entry = readdir(dir)) != NULL) 
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        // Construct full path of the directory entry
        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);
        fullPath = realloc(fullPath, path_len + name_len + 2); // +2 for '/' and '\0'

        if (!fullPath) 
        {
            fprintf(stderr, "Error: Memory allocation failed for path \"%s\"\n", path);
            break;
        }
        sprintf(fullPath, "%s/%s", path, entry->d_name);

        // Get file information
        struct stat st;
        if (lstat(fullPath, &st) == -1) 
        {
            fprintf(stderr, "Error: Failed to get information for path \"%s\"\n", entry->d_name);
            break;
        }

        // Process directory entry
        checkPermissionsAndAnalyze(fullPath, st, baseDirName, isoPath, snapshotFd);

        // Format entry information
        size_t info_length = snprintf(NULL, 0, "Path: %s\nSize: %ld bytes\nAccess Rights: %c%c%c %c%c%c %c%c%c\nHard Links: %ld\n",
                                      fullPath, st.st_size,
                                      (st.st_mode & S_IRUSR) ? 'r' : '-',
                                      (st.st_mode & S_IWUSR) ? 'w' : '-',
                                      (st.st_mode & S_IXUSR) ? 'x' : '-',
                                      (st.st_mode & S_IRGRP) ? 'r' : '-',
                                      (st.st_mode & S_IWGRP) ? 'w' : '-',
                                      (st.st_mode & S_IXGRP) ? 'x' : '-',
                                      (st.st_mode & S_IROTH) ? 'r' : '-',
                                      (st.st_mode & S_IWOTH) ? 'w' : '-',
                                      (st.st_mode & S_IXOTH) ? 'x' : '-',
                                      st.st_nlink);

        entryInfo = realloc(entryInfo, info_length + 1); // +1 for '\0'

        if (!entryInfo) 
        {
            fprintf(stderr, "Error: Memory allocation failed for entry \"%s\"\n", entry->d_name);
            break;
        }

        sprintf(entryInfo, "Path: %s\nSize: %ld bytes\nAccess Rights: %c%c%c %c%c%c %c%c%c\nHard Links: %ld\n",
                fullPath, st.st_size,
                (st.st_mode & S_IRUSR) ? 'r' : '-',
                (st.st_mode & S_IWUSR) ? 'w' : '-',
                (st.st_mode & S_IXUSR) ? 'x' : '-',
                (st.st_mode & S_IRGRP) ? 'r' : '-',
                (st.st_mode & S_IWGRP) ? 'w' : '-',
                (st.st_mode & S_IXGRP) ? 'x' : '-',
                (st.st_mode & S_IROTH) ? 'r' : '-',
                (st.st_mode & S_IWOTH) ? 'w' : '-',
                (st.st_mode & S_IXOTH) ? 'x' : '-',
                st.st_nlink);

        // Write entry info to snapshot file
        write(snapshotFd, entryInfo, strlen(entryInfo));
        write(snapshotFd, "\n", 1);

        // Recursively explore subdirectories
        if (S_ISDIR(st.st_mode))
        {
            exploreDirectories(fullPath, snapshotFd, baseDirName, isoPath);
        }
    }

    free(fullPath);
    free(entryInfo);
    closedir(dir);
}



void createSnapshot(const char *path, const char *outputPath, const char *isolatedPath) 
{
    char *dirName = basename((char *)path); // Get the name of the input directory

    DIR *dirCheck = opendir(path); // Check if the directory for monitoring exists

    if (!dirCheck) 
    {
        fprintf(stderr, "Error: The provided directory for monitoring does not exist \"%s\"\n", dirName);
        return;
    }
    closedir(dirCheck);

    // Check and create the output directory if it doesn't exist
    dirCheck = opendir(outputPath);

    if (!dirCheck) 
    {
        mkdir(outputPath, 0777);
    }
    closedir(dirCheck);

    // Check and create the isolated directory if it doesn't exist
    dirCheck = opendir(isolatedPath);

    if (!dirCheck) 
    {
        mkdir(isolatedPath, 0777);
    }
    closedir(dirCheck);

    // Construct the snapshot file name
    char snapshotFileName[FILENAME_MAX];
    snprintf(snapshotFileName, sizeof(snapshotFileName), "%s/%s_Snapshot.txt", outputPath, dirName);

    int snapshotFd = open(snapshotFileName, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    
    if (snapshotFd == -1) 
    {
        fprintf(stderr, "Error: Failed to open the snapshot file for \"%s\"\n", dirName);
        return;
    }

    // Call function to read directories and create snapshot
    exploreDirectories(path, snapshotFd, dirName, isolatedPath);

    // Output a success message
    fprintf(stdout, "Snapshot created successfully for \"%s\"\n", dirName);

    close(snapshotFd);

    // Call function to compare with previous snapshot
    getPreviousSnapshotThenCompare(outputPath, dirName, snapshotFileName);
}



void getPreviousSnapshotThenCompare(const char *outputPath, const char *dirName, const char *currentSnapshotFileName) 
{
    DIR *dir = opendir(outputPath);

    if (!dir) 
    {
        fprintf(stderr, "Error: Failed to open the output directory \"%s\"\n", dirName);
        return;
    }

    int prevSnapshotNo = 0;
    char prevSnapshotFileName[FILENAME_MAX] = "";

    struct dirent *dirEntry;
    
    while ((dirEntry = readdir(dir)) != NULL) 
    {
        const char *fileName = dirEntry->d_name;

        // Check if the file matches the snapshot naming pattern for the directory
        if (strstr(fileName, dirName) == fileName && strstr(fileName, "_Snapshot_") != NULL) 
        {
            prevSnapshotNo++;

            // Save the name of the latest previous snapshot file
            if (prevSnapshotNo == 1) 
            {
                snprintf(prevSnapshotFileName, sizeof(prevSnapshotFileName), "%s/%s", outputPath, fileName);
            }
        }
    }

    closedir(dir);

    // If no previous snapshot is found, print a message and return
    if (prevSnapshotNo < 2) 
    {
        fprintf(stdout, "(Comparing) No previous snapshots found for \"%s\"\n", dirName);
        return;
    }

    // Compare the current snapshot with the latest previous snapshot
    int isDifferent = compareSnapshots(prevSnapshotFileName, currentSnapshotFileName, dirName);

    if (!isDifferent) 
    {
        // No differences found
        fprintf(stdout, "(Comparing) No differences found between the current and previous snapshot for \"%s\"\n", dirName);
        unlink(prevSnapshotFileName); // Delete the previous snapshot file
    } 
    else 
    {
        // Differences found
        fprintf(stdout, "(Comparing) Differences found between the current and previous snapshot => Overriding the previous snapshot for \"%s\"\n", dirName);
        unlink(prevSnapshotFileName);               // Delete the previous snapshot file
        rename(currentSnapshotFileName, prevSnapshotFileName); // Rename the current snapshot to the previous one
    }
}



int compareSnapshots(const char *prevSnapshotFileName, const char *currentSnapshotFileName, const char *dirName) 
{
    int snapshotFdCurrent = open(currentSnapshotFileName, O_RDONLY, S_IRUSR);

    if (snapshotFdCurrent == -1) 
    {
        fprintf(stderr, "Error: Failed to open the current snapshot file for \"%s\"\n", dirName);
        return -1;
    }

    int snapshotFdPrev = open(prevSnapshotFileName, O_RDONLY, S_IRUSR);

    if (snapshotFdPrev == -1) 
    {
        fprintf(stderr, "Error: Failed to open the previous snapshot file for \"%s\"\n", dirName);
        close(snapshotFdCurrent);
        return -1;
    }

    char currentLine[MAX_LINE];
    char prevLine[MAX_LINE];
    int isDifferent = 0;

    ssize_t currentRead, prevRead;

    do 
    {
        currentRead = read(snapshotFdCurrent, currentLine, sizeof(currentLine) - 1);
        prevRead = read(snapshotFdPrev, prevLine, sizeof(prevLine) - 1);

        if (currentRead == -1 || prevRead == -1) 
        {
            fprintf(stderr, "Error: Failed to read snapshot files for \"%s\"\n", dirName);
            isDifferent = -1;
            break;
        }

        currentLine[currentRead] = '\0';
        prevLine[prevRead] = '\0';

        if ((currentRead != prevRead) || strcmp(currentLine, prevLine) != 0) 
        {
            isDifferent = 1;
            break;
        }
    } 
    while (currentRead > 0 && prevRead > 0);

    close(snapshotFdCurrent);
    close(snapshotFdPrev);

    return isDifferent;
}


void checkPermissionsAndAnalyze(const char *dirEntryPath, struct stat permissions, const char *dirName, char *isolatedPath, int snapshotFd) 
{
    pid_t pid;

    int pipeFd[2];

    if (pipe(pipeFd) == -1) 
    {
        fprintf(stderr, "Error: pipe() failed for \"%s\"\n", basename((char *)dirEntryPath));
        return;
    }

    // Check if all permissions are missing
    if (!(permissions.st_mode & S_IXUSR) && !(permissions.st_mode & S_IRUSR) && !(permissions.st_mode & S_IWUSR) &&
        !(permissions.st_mode & S_IRGRP) && !(permissions.st_mode & S_IWGRP) && !(permissions.st_mode & S_IXGRP) &&
        !(permissions.st_mode & S_IROTH) && !(permissions.st_mode & S_IWOTH) && !(permissions.st_mode & S_IXOTH)) 
    {

        fprintf(stdout, "(Checking Permissions) \"%s\" from \"%s\" has no access rights => Performing Syntactic Analysis!\n",
                basename((char *)dirEntryPath), dirName);

        int fileStatus;
        pid = fork();
        countGrandchildProcesess++;

        if (pid == 0) 
        {
            // Child process
            close(snapshotFd); // Close snapshot file descriptor
            close(pipeFd[0]);  // Close read end of the pipe

            // Analyze the file and write results to the pipe
            fileStatus = analyzeFile(dirEntryPath, dirName, pipeFd[1]);

            close(pipeFd[1]);  // Close write end of the pipe
            exit(fileStatus);  // Exit with analysis result
        } 
        else if (pid < 0) 
        {
            fprintf(stderr, "Error: fork() for child failed for \"%s\"\n", basename((char *)dirEntryPath));
            return;
        } 
        else 
        {
            // Parent process
            resultOfAnalysis(pipeFd, dirEntryPath, isolatedPath, pid, dirName);
            
            fprintf(stdout, "Grandchild Process %d terminated with PID %d for file \"%s\" from \"%s\"\n",pid, getpid(), basename((char *)dirEntryPath), dirName);
            write(STDOUT_FILENO, "\n", 1);
        }
    }
}



int analyzeFile(const char *filePath, const char *directoryName, int pipeFd) 
{
    // Give read access to the file
    int giveAccess = chmod(filePath, S_IRUSR);

    if (giveAccess == -1) 
    {
        fprintf(stderr, "Error: Failed to grant read access to \"%s\"\n", filePath);
        return -1;
    }

    // Prepare the command to run the analysis script
    char command[100];
    snprintf(command, sizeof(command), "./verifyForMalicious.sh \"%s\"", filePath);

    // Execute the analysis script
    int fileStatus = system(command);
    if (fileStatus == -1) 
    {
        fprintf(stderr, "Error: Failed to execute analysis script for \"%s\"\n", filePath);
        return -1;
    }

    // Write the analysis result to the pipe
    write(pipeFd, &fileStatus, sizeof(fileStatus));
    close(pipeFd);

    // Revoke read access to the file
    giveAccess = chmod(filePath, 0);
    if (giveAccess == -1) 
    {
        fprintf(stderr, "Error: Failed to revoke read access to \"%s\"\n", filePath);
        return -1;
    }
    close(giveAccess);

    return fileStatus;
}



void resultOfAnalysis(int pipeFd[2], const char *filePath, char *isolatedPath, pid_t pid, const char *directoryName) 
{
    close(pipeFd[1]); // Close write end of the pipe

    int fileStatus;
    read(pipeFd[0], &fileStatus, sizeof(fileStatus)); // Read result from the pipe
    close(pipeFd[0]); // Close read end of the pipe

    if (fileStatus != 0) 
    {
        // File is malicious or corrupted, move it to the isolated directory
        char isolatedFilePath[PATH_MAX];
        snprintf(isolatedFilePath, sizeof(isolatedFilePath), "%s/%s", isolatedPath, basename((char *)filePath));

        if (rename(filePath, isolatedFilePath) == 0) 
        {
            fprintf(stdout, "(Syntactic Analysis) \"%s\" from \"%s\" is malicious or corrupted => Moved to the isolated directory!\n", basename((char *)filePath), directoryName);
            countCorrupted++;
        } 
        else 
        {
            fprintf(stderr, "Error: Failed to move \"%s\" to the isolated directory\n", basename((char *)filePath));
        }
    } 
    else 
    {
        fprintf(stdout, "(Syntactic Analysis) \"%s\" from \"%s\" is SAFE!\n", basename((char *)filePath), directoryName);
    }
}



int main(int argc, char *argv[]) 
{
    write(STDOUT_FILENO, "\n", 1);

    if (argc < 6) 
    {
        fprintf(stderr, "Error: Not enough arguments! => Exiting program!\n");
        exit(EXIT_FAILURE);
    }

    char *outputPath = NULL;
    char *isolatedPath = NULL;
    int oCount = 0, sCount = 0;

    // Parse through all the arguments for error handling
    for (int i = 1; i < argc; i++) 
    {
        if (strcmp(argv[i], "-o") == 0) 
        {
            oCount++;
            outputPath = argv[i + 1];
        } 
        else if (strcmp(argv[i], "-s") == 0) 
        {
            sCount++;
            isolatedPath = argv[i + 1];
        }

        // Checking if "-o" and "-s" arguments are consecutive
        if ((strcmp(argv[i], "-o") == 0 && i + 1 < argc && strcmp(argv[i + 1], "-s") == 0) ||
            (strcmp(argv[i], "-s") == 0 && i + 1 < argc && strcmp(argv[i + 1], "-o") == 0)) 
        {
            fprintf(stderr, "Error: Both \"-o\" and \"-s\" arguments cannot be provided consecutively! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }

        // Checking if argument "-o" or "-s" is not written in terminal
        if (oCount < 1 && i + 1 == argc) 
        {
            fprintf(stderr, "Error: The argument \"-o\" was not detected in the terminal! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }
        if (sCount < 1 && i + 1 == argc) 
        {
            fprintf(stderr, "Error: The argument \"-s\" was not detected in the terminal! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }

        // Checking if argument "-o" or "-s" is more than once
        if (oCount > 1) 
        {
            fprintf(stderr, "Error: The argument \"-o\" was detected more than once in the terminal! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }
        if (sCount > 1) 
        {
            fprintf(stderr, "Error: The argument \"-s\" was detected more than once in the terminal! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }

        // Checking if argument "-o" or "-s" is the last argument
        if (strcmp(argv[i], "-o") == 0 && i + 1 == argc) 
        {
            fprintf(stderr, "Error: \"-o\" cannot be the last argument! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }

        if (strcmp(argv[i], "-s") == 0 && i + 1 == argc) 
        {
            fprintf(stderr, "Error: \"-s\" cannot be the last argument! => Exiting program!\n");
            exit(EXIT_FAILURE);
        }
    }

    // Parse again through all the arguments
    for (int i = 1; i < argc; i++) 
    {
        if ((strcmp(argv[i], "-o") == 0 && i + 1 < argc) || (strcmp(argv[i], "-s") == 0 && i + 1 < argc)) 
        {
            i++; // Skip next argument (-o or -s)
        } 
        else 
        {
            // The rest of the arguments are directories to be monitored
            pid_t pid = fork();
            countProcesses++;

            if (pid == 0) 
            {
                char *path = argv[i];
                createSnapshot(path, outputPath, isolatedPath);
                fprintf(stdout, "Child Process %d terminated with PID %d and %d files with potential danger for \"%s\"\n", getpid(), countCorrupted, basename(path));
                return EXIT_SUCCESS;
            } 
            else if (pid < 0) 
            {
                fprintf(stderr, "Error: fork() for child failed!\n");
                return EXIT_FAILURE;
            }
        }
    }

    // Wait for all child processes to complete
    for (int i = 1; i < argc; i++) 
    {
        if ((strcmp(argv[i], "-o") == 0 && i + 1 < argc) || (strcmp(argv[i], "-s") == 0 && i + 1 < argc)) 
        {
            i++; // Skip next argument (-o or -s)
        } 
        else 
        {
            write(STDOUT_FILENO, "\n", 1);
            wait(NULL);
        }
    }

    write(STDOUT_FILENO, "\n", 1);
    return EXIT_SUCCESS;
}
