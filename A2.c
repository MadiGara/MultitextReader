#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <linux/limits.h>
#include <errno.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define BUFFSIZE 4096
#define ALPHA_LETTERS 26

void sigHandler(int sigNum);
void calcHisto (int * histoArray, int fileDesc, int size, char * filename);

int ** pipes;
int * pids;

int main (int argc, char * argv[]) {
    pid_t childPid;
    int fileDesc;
    int size = 0;
    int childrenLeft = 1;

    //initalize all counts in histoArray to 0 - each index will correspond to a letter count
    //ex. histoArray[0] = a's count, histoArray[1] = b's count, etc.
    int * histoArray = calloc(ALPHA_LETTERS, sizeof(int));

    if (argc < 2) {
        fprintf(stderr, "No input file names entered\n");
        exit(1);
    }

    //allocate space for array of pipes
    pipes = (int **) malloc ((argc - 1) * sizeof(int *));
    for (int j = 0; j < (argc-1); j++) {
        pipes[j] = (int *) malloc (2 * sizeof(int));
    }

    //allocate space for array of pids
    pids = malloc((argc-1) * sizeof(int));

    //register handler for sigchld
    if (signal(SIGCHLD, sigHandler) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGCHLD");
        exit(1);
    }

    //for each file, fork a child and pass it a file name
    for (int i = 1; i < argc; i++) {

        //create child to parent pipe
        if (pipe(pipes[i-1]) == -1) {
            perror("Pipe error");
            exit(1);
        }

        /* execute child process code */
        if ((childPid = fork()) == 0) {

            //close pipes of every initialized pipe in the array
            for (int k = 1; k < (i-1); k++) {
                close(pipes[k][0]);
            }

            //close pipe's read side for child by its pipe's file descriptor
            close(pipes[i-1][0]);

            //open file for reading and get its size; remember first arg is 1
            if (strcmp(argv[i], "SIG") != 0) {
                fileDesc = open(argv[i], O_RDONLY);
                if (fileDesc < 0) {
                    fprintf(stderr, "Cannot open file %s\n", argv[i]);
                    close(pipes[i-1][1]);
                    exit(1);
                } 
                else {
                    //get size of file
                    size = lseek(fileDesc, 0, SEEK_END);
                    lseek(fileDesc, 0, SEEK_SET);
                    if (size == 0) {
                        printf("File is empty.\n");
                    }
                }

                //calculate histogram in array for file
                calcHisto(histoArray, fileDesc, size, argv[i]);

                //send histogram through child's write pipe to parent
                int wbytes = write(pipes[i-1][1], histoArray, (sizeof(int) * ALPHA_LETTERS));
                if (wbytes < 0) {
                    fprintf(stderr, "Write error to file\n");
                }

                //close write end of pipe
                close(pipes[i-1][1]);
                close(fileDesc);
            }

            //free memory in child
            for (int j = 0; j < (argc-1); j++) {
                free(pipes[j]);
            }
            free(pipes);
            free(pids);
            free(histoArray);

            //sleep to avoid race condition and allow memory clean up
            sleep(10 + (2 * i));
        
            exit(0);
        }

        //error handling for fork
        else if (childPid < 0) {
            perror("Fork error");
            exit(1);
        }

        /* execute parent process code */
        else {
            //set child's pid to the same index in pids array as the index its pipe pair is at
            pids[i-1] = childPid; 

            //handle SIG special input
            if (strcmp(argv[i], "SIG") == 0) {
                //give memory time to free before killing the child
                sleep(2);
                kill(childPid, SIGINT);   
            }
        }
    }

    //wait for handler to return
    for (;;) {
        pause();

        //make sure all children finished, if so break
        for (int i = 0; i < (argc - 1); i++) {
            int status = waitpid(0, NULL, WNOHANG);
            if (status == -1) {
                childrenLeft = 0;
                break;
            }
        }

        if (childrenLeft == 0) {
            break;
        }
    }

    //free memory in parent
    for (int j = 0; j < (argc-1); j++) {
        free(pipes[j]);
    }
    free(pipes);
    free(pids);
    free(histoArray);
    
    exit(0); 
}

/* handler for sigchld signal - only runs in parent */
void sigHandler(int sigNum) {   
    int exitData = 0;
    int childPid = 0;
    int fd = 0;
    int pidIndex = 0;
    int nbytes = 0;
    int currentPos = 0;
    char letter = 'a';
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    int * readHistoArray = malloc(sizeof(int) * ALPHA_LETTERS);
    char * writeStr = malloc(BUFFSIZE);
    char * filename = malloc(BUFFSIZE);
    char * stringCount = malloc(BUFFSIZE);

    //handle error
    if (signal(SIGCHLD, sigHandler) == SIG_ERR) {
        fprintf(stderr, "Can't catch SIGCHLD");
    }
    else {
        //process the signal
        if (sigNum == SIGCHLD) {
            childPid = waitpid(-1, &exitData, WNOHANG);
            printf("Caught SIGCHLD PID %d\n", childPid);
        }
        else {
            fprintf(stderr, "Caught signal %d\n", sigNum);
        }

        //check if child terminated properly
        if (WIFSIGNALED(exitData)) {
            fprintf(stderr, "Child exited abnormally with status = %d\n", WTERMSIG(exitData));

            free(readHistoArray);
            free(writeStr);
            free(filename);
            free(stringCount);
        }
        
        //if child exited normally, read from its histogram array
        if (WIFEXITED(exitData) && WEXITSTATUS(exitData) == 0) {
            printf("Child exited normally with status = %d\n", WEXITSTATUS(exitData));

            //get pipe to close's index by finding the child 
            while (pids[pidIndex] != childPid) {
                pidIndex++;
            }

            //close write side of pipe in parent
            close(pipes[pidIndex][1]);

            //read in histogram from read end of pipe, then close it
            nbytes = read(pipes[pidIndex][0], readHistoArray, (sizeof(int) * ALPHA_LETTERS));
            if (nbytes < 0){
                fprintf(stderr, "Read error from pipe %d: %s\n", pipes[pidIndex][0], strerror(errno));
                close(pipes[pidIndex][0]);
            } 
            else {
                close(pipes[pidIndex][0]);

                //get readHistoArray for file into proper formatting (to, format, from)
                for (int i = 0; i < ALPHA_LETTERS; i++) {
                    sprintf(writeStr+currentPos, "%c %d\n", letter, readHistoArray[i]); 
                    //convert integer count to string for size incrementing
                    sprintf(stringCount, "%d", readHistoArray[i]);
                    letter += 1;
                    currentPos = currentPos + (sizeof(char) * 3) + strlen(stringCount); 
                }

                //change size of buffer to suit size of writeStr
                writeStr = realloc(writeStr, currentPos);

                //establish file name with child's pid
                sprintf(filename, "file%d.hist", childPid);

                //open file for writing formatted string to it
                fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, mode);
                if (fd < 0) {
                    fprintf(stderr, "Cannot open file %s\n", filename);
                } else {
                    if (write(fd, writeStr, currentPos-1) != currentPos-1) {
                        fprintf(stderr, "Write error to file\n");
                    }
                }
                close(fd);
            }
            free(readHistoArray);
            free(writeStr);
            free(filename);
            free(stringCount);
        }
    }
}

/* calculate histogram of file fed to child, save to histoArray */
void calcHisto (int * histoArray, int fileDesc, int size, char * filename) {
    int rBytes = 0;
    unsigned char * buffer = malloc(size * sizeof(char));

    //read file character by character
    while ((rBytes = read(fileDesc, buffer, 1)) > 0) {
        if (rBytes < 0) {
            fprintf(stderr, "Read error from %s\n", filename);
            free(buffer);
        }

        //count alphabetical characters only
        else if (isalpha(buffer[0]) != 0) {

            buffer[0] = tolower(buffer[0]);

            switch(buffer[0]) {
                case 'a':
                    histoArray[0] += 1;
                    break;
                case 'b':
                    histoArray[1] += 1;
                    break;
                case 'c':
                    histoArray[2] += 1;
                    break;
                case 'd':
                    histoArray[3] += 1;
                    break;
                case 'e':
                    histoArray[4] += 1;
                    break;
                case 'f':
                    histoArray[5] += 1;
                    break;
                case 'g':
                    histoArray[6] += 1;
                    break;
                case 'h':
                    histoArray[7] += 1;
                    break;
                case 'i':
                    histoArray[8] += 1;
                    break;
                case 'j':
                    histoArray[9] += 1;
                    break;
                case 'k':
                    histoArray[10] += 1;;
                    break;
                case 'l':
                    histoArray[11] += 1;
                    break;
                case 'm':
                    histoArray[12] += 1;
                    break;
                case 'n':
                    histoArray[13] += 1;
                    break;
                case 'o':
                    histoArray[14] += 1;
                    break;
                case 'p':
                    histoArray[15] += 1;
                    break;
                case 'q':
                    histoArray[16] += 1;
                    break;
                case 'r':
                    histoArray[17] += 1;
                    break;
                case 's':
                    histoArray[18] += 1;
                    break;
                case 't':
                    histoArray[19] += 1;
                    break;
                case 'u':
                    histoArray[20] += 1;
                    break;
                case 'v':
                    histoArray[21] += 1;
                    break;
                case 'w':
                    histoArray[22] += 1;
                    break;
                case 'x':
                    histoArray[23] += 1;
                    break;
                case 'y':
                    histoArray[24] += 1;
                    break;
                case 'z':
                    histoArray[25] += 1;
                    break;
                default:
                    break;
            }
        }

    }
    free(buffer);
}