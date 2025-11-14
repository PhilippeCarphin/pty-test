#include <string.h> // strsignal
#define _XOPEN_SOURCE 600
#include "tlpi-pt.h"
#include <pthread.h>
#include <limits.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>

#define BUF_SIZE 256
#define MAX_SNAME 1000

#include <libgen.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include "phil-expect.h"


volatile int child_finished = 0;
void sigchld_handler(int signal){
#ifdef __APPLE__
    fprintf(stderr, "Signal %d(%s)\r\n", signal, sys_signame[signal]);
#else
    fprintf(stderr, "Signal %d\r\n", signal);
#endif
    child_finished = 1;
}

struct termios ttyOrig;

int main(int argc, char **argv)
{
    char slaveName[MAX_SNAME];
    char *shell;
    int masterFd;
    struct winsize ws;
    fd_set inFds;
    char buf[BUF_SIZE];
    ssize_t numRead;
    pid_t childPid;

    signal(SIGCHLD, sigchld_handler);

    if(tcgetattr(STDIN_FILENO, &ttyOrig) == -1){
        exit(1);
    }

    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0){
        exit(1);
    }

    childPid = ptyFork(&masterFd, slaveName, MAX_SNAME, &ttyOrig, &ws);
    if(childPid == -1){
        fprintf(stderr, "ERROR: ptyFork returned -1\n");
        exit(1);
    }

    FILE *log_file = fopen("log.txt", "a");
    if(log_file == NULL){
        exit(1);
    }
    setbuf(log_file, NULL);


    /*
     * CHILD
     */
    if(childPid == 0){
        for(int i=3; i < argc; i++){
            fprintf(log_file, ", %s", argv[i]);
        }
        fprintf(log_file, ")\n");
        execvp(argv[1], &argv[1]);
        exit(8);
    }

    /*
     * PARENT
     */
    ttySetRaw(STDIN_FILENO, &ttyOrig);
    if(atexit(ttyReset) != 0){
        exit(88);
    }

    for(;;){
        FD_ZERO(&inFds);
        FD_SET(STDIN_FILENO, &inFds);
        FD_SET(masterFd, &inFds);

        if(select(masterFd + 1, &inFds, NULL, NULL, NULL) == -1){
            exit(89);
        }

        if(child_finished){
            exit(99);
        }

        if(FD_ISSET(STDIN_FILENO, &inFds)){
            numRead = read(STDIN_FILENO, buf, BUF_SIZE);
            if(numRead < 0){
                exit(0);
            }
            int numWrite = write(masterFd, buf, numRead);
            if(numWrite < 0){
                fprintf(stderr, "Failed write (numRead == -1)\r\n");
                exit(0);
            }
            if(numWrite != numRead){
                fprintf(stderr, "Partial/failed write (masterFd)\r\n");
            }

        }

        if(FD_ISSET(masterFd, &inFds)) {
            numRead = read(masterFd, buf, BUF_SIZE);
            if(numRead < 0){
                exit(0);
            }
            if(write(STDOUT_FILENO, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (stdout)\n");
            }
        }
    }
    return 0;
}
