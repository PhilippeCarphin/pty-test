#define _XOPEN_SOURCE 600
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

#define BUF_SIZE 256
#include <libgen.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>


#define MAX_SNAME 1000
struct expect_buffer {
    char *buffer;
    char *cursor;
    char *before;
    char *after;
    int masterFd;
    FILE *log_file;
    fd_set *inFds;
};

int expect(struct expect_buffer *eb, const char *re_str)
{
    fprintf(eb->log_file, "%s(eb=%p, re_str='%s')\n",__func__, eb, re_str);
    regex_t re;
    int err = regcomp(&re, re_str, 0);
    if(err){
        return err;
    }
    char *cursor = eb->buffer;
    int numRead;
    memset(eb->buffer, 0, 40960);

    regmatch_t rm[re.re_nsub+1];

    for(;;){
        FD_ZERO(eb->inFds);
        FD_SET(STDIN_FILENO, eb->inFds);
        FD_SET(eb->masterFd, eb->inFds);

        if(select(eb->masterFd + 1, eb->inFds, NULL, NULL, NULL) == -1){
            exit(89);
        }

        if(FD_ISSET(eb->masterFd, eb->inFds)) {
            char c;
            numRead = read(eb->masterFd, &c, 1);
            if(numRead < 0){
                exit(0);
            }
            // Can't have '\r' in the re_str now: because of this, our buffer
            // will never have a '\r' put in it.
            switch(c){
                case '\r':
                    continue;
                default:
                    *cursor = c;
            }

            int err = regexec(&re, eb->buffer, re.re_nsub+1, rm, 0);
            if(err == REG_NOERROR){
                size_t before_len = rm[0].rm_so;
                strncpy(eb->before, eb->buffer, before_len);
                eb->before[before_len] = '\0';

                size_t match_len = rm[0].rm_eo - rm[0].rm_so;
                strncpy(eb->after, (eb->buffer + rm[0].rm_so), match_len);
                eb->after[match_len] = '\0';

                fprintf(eb->log_file, "%s(): Match found '%c'\n", __func__, *cursor);
                return 0;
            } else if(err != REG_NOMATCH){
                fprintf(eb->log_file, "Error using regex\n");
                return 1;
            }
            cursor++;
        }

    }
    return 0;
}

struct termios ttyOrig;

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


static void ttyReset(void)
{
    if(tcsetattr(STDIN_FILENO, TCSANOW, &ttyOrig) == -1){
        exit(77);
    }
}

int ttySetRaw(int fd, struct termios *prefTermios)
{
    struct termios t;

    if(tcgetattr(fd, &t) == -1){
        return -1;
    }

    if(prefTermios != NULL){
        *prefTermios = t;
    }

    t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO);
    t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INPCK | ISTRIP | IXON | PARMRK);

    t.c_oflag &= ~OPOST;

    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;

    if(tcsetattr(fd, TCSAFLUSH, &t) == -1){
        return -1;
    }

    return 0;
}


// #include "pty_master_open.h"
int ptyMasterOpen(char *slaveName, size_t snLen)
{
    int masterFd, savedErrno;
    char *p;

    /*
     * Open pyt master
     */
    masterFd = posix_openpt(O_RDWR | O_NOCTTY);
    if(masterFd == -1){
        return -1;
    }

    if(grantpt(masterFd) == -1){
        goto err_close;
    }

    if(unlockpt(masterFd) == -1){
        goto err_close;
    }

    p = ptsname(masterFd);
    if(p == NULL){
        goto err_close;
    }

    if(strlen(p) > snLen) {
        errno = EOVERFLOW;
        goto err_close;
    }

    strncpy(slaveName, p, snLen);

    return masterFd;

err_close:
    savedErrno = errno;
    close(masterFd);
    errno = savedErrno;
    return -1;
}

pid_t ptyFork(int *masterFd, char *slaveName, size_t snLen, const struct termios *slaveTermios, const struct winsize *slaveWS)
{
    int mfd, slaveFd, savedErrno;
    pid_t childPid;
    char slname[MAX_SNAME];

    mfd = ptyMasterOpen(slname, MAX_SNAME);
    if(mfd == -1){
        fprintf(stderr, "ptyMasterOpen returned -1\n");
        return -1;
    }

    if(slaveName != NULL) { 
        if(strlen(slname) > snLen){
            errno = EOVERFLOW;
            goto err_close;
        }

        strncpy(slaveName, slname, snLen);
    }

    childPid = fork();

    if(childPid == -1){
        goto err_close;
    }

    if(childPid != 0){
        *masterFd = mfd;
        return childPid;
    }

    /*
     * We are the child
     */
    if(setsid() == -1){
        exit(1);
    }

    close(mfd);

    slaveFd = open(slname, O_RDWR);
    if(slaveFd == -1){
        exit(1);
    }

// #ifdef TIOCSCTTY
//     fprintf(stderr, "TIOCSCTTY is defined: we are on BSD\n");
//     if(ioctl(slaveFd, TIOCSCTTY, 0) == -1){
//         exit(1);
//     }
// #endif

    if(slaveTermios != NULL){
        if(tcsetattr(slaveFd,TCSANOW, slaveTermios) == -1){
            fprintf(stderr, "tcsetattr(slaveFd, TCSANOW, slaveTermios) -> -1\r\n");
            exit(1);
        }
    }

    if(slaveWS != NULL){
        int err = ioctl(slaveFd, TIOCSWINSZ, slaveWS);
        if(err == -1){
            fprintf(stderr, "ptyFork(): ioctl(slaveFd, TIOCSWINSZ, slaveWS) -> %d\r\n", err);
            exit(1);
        }
    }


    if(dup2(slaveFd, STDIN_FILENO) != STDIN_FILENO){
        fprintf(stderr, "ptyFork(): ERROR: dup2(slaveFd, STDIN_FILENO)\r\n");
        exit(1);
    }
    if(dup2(slaveFd, STDOUT_FILENO) != STDOUT_FILENO){
    fprintf(stderr, "ptyFork(): ERROR: dup2(slaveFd, STDOUT_FILENO)\r\n");
        exit(1);
    }
    if(dup2(slaveFd, STDERR_FILENO) != STDERR_FILENO){
        fprintf(stderr, "ptyFork(): ERROR: dup2(slaveFd, STDERR_FILENO)\r\n");
        exit(1);
    }

    return childPid;

err_close:
    savedErrno = errno;
    close(mfd);
    errno = savedErrno;
    return -1;
}

int send_discard(struct expect_buffer *eb, const char *cmd, const char *prompt){
    ptySend(eb->masterFd, cmd, 100);
    expect(eb, prompt);
    return 0;
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
            if(write(masterFd, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (masterFd)\n");
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
                fprintf(stderr, "Partial/failed write (masterFd)\n");
            }
            // fprintf(log_file, "Writing to scriptFd\n");
            if(write(scriptFd, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (masterFd)\n");
            }
        }
    }
#else
    struct expect_buffer eb = {.inFds=&inFds, .masterFd=masterFd, .log_file=log_file};
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

    // ptySend(masterFd, "rcd \t\t", 100);
    // expect(&eb, "rcd ");
    // expect(&eb, "XXPS1XX");
    // fprintf(log_file, "eb.before = '%s'\n", eb.before);
    // fprintf(log_file, "eb.after = '%s'\n", eb.after);
    // ptySend(masterFd, "exit 0\n", 100);

    // fprintf(stdout, "%s\n", eb.before);
#endif
    return 0;
}




