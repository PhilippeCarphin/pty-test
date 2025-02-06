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
    fprintf(stderr, "Signal %d(%s)\r\n", signal, sys_signame[signal]);
    child_finished = 1;
}

void ptySend(int fd, const char *text, size_t sz){
    size_t len = strlen(text);
    const char *p = text;
    for(int i = 0; i < len; i += sz, p += sz){
        struct timespec ts = {.tv_sec = 0, .tv_nsec = 50000000};
        nanosleep(&ts, NULL);
        size_t write_len = ( i+sz > len ? len-i : sz );
        if(write(fd, p, write_len) != write_len){
            fprintf(stderr, "Partial/failed write (masterFd)\n");
        }
    }
}

struct termios ttyOrig;

struct worker_args {
    int masterFd;
    int argc;
    char **argv;
    FILE *log_file;
};

void *worker(void *_args){

    struct worker_args *args = (struct worker_args*)_args;
    char buf[BUF_SIZE];
    int write_len;

    struct timespec ts = {.tv_sec = 4, .tv_nsec = 0};
    nanosleep(&ts, NULL);

    ptySend(args->masterFd, "unset PROMPT_COMMAND; PS1=\"PTY-TEST $ \"\n", 4);

    ptySend(args->masterFd, "bind \"set completion-query-items -1\"\n", 4);
    ts.tv_sec = 0; ts.tv_nsec = 500000000;
    nanosleep(&ts, NULL);

    ptySend(args->masterFd, "bind \"set completion-display-width 0\"\n", 4);

    ptySend(args->masterFd, "bind \"set page-completions off\"\n", 4);
    nanosleep(&ts, NULL);

    ptySend(args->masterFd, "echo \"YAYBOO YAYBOO it's lots of fun to do\"\n", 1);
    ptySend(args->masterFd, "echo \"if you like it holler YAY\"\n", 1);
    ptySend(args->masterFd, "echo \"and if you don't you holler BOO\"\n", 1);
    nanosleep(&ts, NULL);

    ptySend(args->masterFd, args->argv[2], 1);
    ts.tv_sec = 1;
    nanosleep(&ts, NULL);
    return NULL;
}

int main(int argc, char **argv)
{
    char slaveName[MAX_SNAME];
    char *shell;
    int masterFd, scriptFd;
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
        execvp(argv[3], &argv[3]);
        exit(8);
    }

    /*
     * PARENT
     */
    char *script_file = (argc > 1 ? argv[1] : "typescript");
    scriptFd = open(argc>1?argv[1]:"typescript",
                    O_WRONLY | O_CREAT | O_TRUNC,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if(scriptFd == -1){
        exit(1);
    }

    ttySetRaw(STDIN_FILENO, &ttyOrig);
    if(atexit(ttyReset) != 0){
        exit(88);
    }

    struct worker_args wa = {
        .masterFd = masterFd,
        .argc = argc,
        .argv = argv,
        .log_file = log_file
    };
    pthread_t async_writer;
    pthread_create(&async_writer, NULL, worker, &wa);

    fprintf(stderr, "masterFd=%d\r\n", masterFd);
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
            if(write(scriptFd, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (scriptFd)\n");
            }
        }
    }
    return 0;
}
