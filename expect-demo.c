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
    FILE *log_file = fopen("log.txt", "a");
    setbuf(log_file, NULL);
    if(log_file == NULL){
        fprintf(stderr, "ERROR opening log_file\n");
    }

    char * const pty_argv[] = { "bash", NULL };
    pty_t *eb = pty_spawnvp("bash", pty_argv, 40960);
    eb->log_file = log_file;

    // pty_t eb = {.inFds=&inFds, .masterFd=masterFd, .log_file=log_file};
    pty_send(eb, "unset PROMPT_COMMAND; PS1=XXPS1XX\n", 100);
    pty_expect(eb, "XXPS1XX");
    pty_expect(eb, "XXPS1XX");

    pty_send_discard(eb, "bind \"set completion-query-items -1\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set completion-display-width 0\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set page-completions off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set visible-stats off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set colored-completion-prefix off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set colored-stats off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"enable-bracketed-paste off\"\n", "XXPS1XX");

    pty_send_discard(eb, "stty echo\n", "XXPS1XX");


    pty_send(eb, "cat marker \n", 100);
    pty_expect(eb, "cat marker");
    pty_expect(eb, "YAYBOO");
    fprintf(log_file, "eb.before = '%s'\n", eb->before);
    fprintf(log_file, "eb.after = '%s'\n", eb->after);

    pty_expect(eb, "XXPS1XX");

    pty_send(eb, "rcd \t\t", 100);
    pty_expect(eb, "rcd ");
    pty_expect(eb, "XXPS1XX");
    fprintf(log_file, "eb.before = '%s'\n", eb->before);
    fprintf(log_file, "eb.after = '%s'\n", eb->after);
    pty_send(eb, "exit 0\n", 100);

    fprintf(stdout, "%s\n", eb->before);
    return 0;
}




