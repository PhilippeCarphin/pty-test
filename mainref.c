#include "tlpi-pt.h"
#include "phil-expect.h"

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pthread.h>
#include <sys/select.h> // fd_set, FD_ISSET, ...
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>


#define BUF_SIZE 256

int fnprintchars(FILE *stream, size_t n, const char *s){
    const char *c = s;
    for(int i = 0; i<n; i++, c++){
        int ci = *c;
        if(32 <= ci && ci<= 126) {
            fputc(*c, stream);
        } else {
            fprintf(stderr, "0x%02x", *c);
        }
    }
    return 0;
}
    

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

    // ptySend(args->masterFd, "unset PROMPT_COMMAND; PS1=\"PTY-TEST $ \"\n", 1);

    ptySend(args->masterFd, "bind \"set completion-query-items -1\"\n", 1);
    ts.tv_sec = 0; ts.tv_nsec = 500000000;
    nanosleep(&ts, NULL);

    // ptySend(args->masterFd, "bind \"set completion-display-width 0\"\n", 1);

    ptySend(args->masterFd, "bind \"set page-completions off\"\n", 1);
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

    if(tcgetattr(STDIN_FILENO, &ttyOrig) == -1){
        exit(1);
    }

    fprintf(stderr, "main(): Getting window size\n");
    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0){
        exit(1);
    }
    fprintf(stderr, "main(): Window size: %dx%d\n", ws.ws_row, ws.ws_col);

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
        fprintf(stderr, "PARENT: ERROR: Could not open script file '%s'\n", script_file);
        exit(1);
    }

    ttySetRaw(STDIN_FILENO, &ttyOrig);
    if(atexit(ttyReset) != 0){
        exit(88);
    }

#if 1
    struct worker_args wa = {.masterFd = masterFd, .argc = argc, .argv = argv, .log_file = log_file};
    pthread_t async_writer;
    pthread_create(&async_writer, NULL, worker, &wa);

    for(;;){
        FD_ZERO(&inFds);
        FD_SET(STDIN_FILENO, &inFds);
        FD_SET(masterFd, &inFds);

        if(select(masterFd + 1, &inFds, NULL, NULL, NULL) == -1){
            exit(89);
        }

        if(FD_ISSET(STDIN_FILENO, &inFds)){
            numRead = read(STDIN_FILENO, buf, BUF_SIZE);
            if(numRead < 0){
                exit(0);
            }
            // fprintf(log_file, "Read '");
            // fwrite(buf, numRead, 1, log_file);
            // fprintf(log_file, "from STDIN_FILENO\n");
            // fprintf(log_file, "Writing to masterFd\n");
            size_t numWrite = write(masterFd, buf, numRead);
            if(numWrite == -1){
                fprintf(stderr, "Failed write to masterFd\r\n");
                exit(0);
            }
            if(numWrite != numRead){
                fprintf(stderr, "Partial/failed write (%ld !+ %ld) (masterFd)\r\n", numWrite, numRead);
                fprintf(stderr, "read: buf(0:%ld)='", numRead);
                fnprintchars(stderr, numRead, buf);
                fprintf(stderr, "')\r\n");
            }

        }

        if(FD_ISSET(masterFd, &inFds)) {
            numRead = read(masterFd, buf, BUF_SIZE);
            if(numRead < 0){
                exit(0);
            }
            // fprintf(log_file, "Read '");
            // fwrite(buf, numRead, 1, log_file);
            // fprintf(log_file, "from masterFd\n");
            // fprintf(log_file, "Writing to STDOUT_FILENO\n");
            if(write(STDOUT_FILENO, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (STDOUT_FILENO)\r\n");
            }
            // fprintf(log_file, "Writing to scriptFd\n");
            if(write(scriptFd, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (scriptFd)\r\n");
            }
        }
    }
#else
    pty_t eb = {.inFds=&inFds, .masterFd=masterFd, .log_file=log_file};
    eb.buffer = malloc(40960);
    eb.before = malloc(40960);
    eb.after = malloc(40960);
    fprintf(log_file, "calling ptySend(masterFd, \"unset PROMPT_COMMAND; PS1=XXPS1XX\\n\"\n");
    ptySend(masterFd, "unset PROMPT_COMMAND; PS1=XXPS1XX\n", 100);
    expect(&eb, "XXPS1XX");
    expect(&eb, "XXPS1XX");

    send_discard(&eb, "bind \"set completion-query-items -1\"\n", "XXPS1XX");
    send_discard(&eb, "bind \"set completion-display-width 0\"\n", "XXPS1XX");
    send_discard(&eb, "bind \"set page-completions off\"\n", "XXPS1XX");
    send_discard(&eb, "bind \"set visible-stats off\"\n", "XXPS1XX");
    send_discard(&eb, "bind \"set colored-completion-prefix off\"\n", "XXPS1XX");
    send_discard(&eb, "bind \"set colored-stats off\"\n", "XXPS1XX");
    send_discard(&eb, "bind \"enable-bracketed-paste off\"\n", "XXPS1XX");

    ptySend(masterFd, "cat marker\n", 1);
    expect(&eb, "cat marker\n");
    expect(&eb, "YAYBOO");
    fprintf(log_file, "eb.before = '%s'\n", eb.before);
    fprintf(log_file, "eb.after = '%s'\n", eb.after);

    expect(&eb, "XXPS1XX");

    ptySend(masterFd, "rcd \t\t", 100);
    expect(&eb, "rcd ");
    expect(&eb, "XXPS1XX");
    fprintf(log_file, "eb.before = '%s'\n", eb.before);
    fprintf(log_file, "eb.after = '%s'\n", eb.after);
    ptySend(masterFd, "exit 0\n", 100);

    ttyReset();
    fprintf(stdout, "%s\n", eb.before);
#endif
    return 0;
}




