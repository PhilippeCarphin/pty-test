#include "phil-expect.h"
#include "tlpi-pt.h"
#include <regex.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

pty_t *pty_spawnvp(const char *file, char *const argv[], size_t buf_size){
    char slaveName[MAX_SNAME];
    int masterFd;
    struct winsize ws;
    pid_t childPid;

    pty_t *p = malloc(sizeof(*p));;
    p->buffer = malloc(buf_size);
    p->before = malloc(buf_size);
    p->after = malloc(buf_size);


    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &p->ws) < 0){
        fprintf(stderr, "%s(): Error getting window size\n", __func__);
        exit(1);
    }

    childPid = ptyFork(&p->masterFd, slaveName, MAX_SNAME, &ttyOrig, &p->ws);
    if(childPid == -1){
        fprintf(stderr, "ERROR: ptyFork returned -1\n");
        exit(1);
    }

    /*
     * CHILD
     */
    if(childPid == 0){
        execvp(file, argv);
    }

    return p;
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

int pty_send(pty_t *eb, const char *text, size_t chunk_size){
    fprintf(eb->log_file, "%s() text='%s'\n", __func__, text);
    size_t len = strlen(text);
    const char *p = text;
    for(int i = 0; i < len; i += chunk_size, p += chunk_size){
        size_t write_len = ( i+chunk_size > len ? len-i : chunk_size );
        if(write(eb->masterFd, p, write_len) != write_len){
            fprintf(stderr, "%s(): Partial/failed write (masterFd)\n", __func__);
        }
    }
    return 0;
}


int pty_send_discard(pty_t *eb, const char *cmd, const char *prompt){
    pty_send(eb, cmd, 100);
    pty_expect(eb, prompt);
    return 0;
}

int pty_expect(pty_t *eb, const char *re_str)
{
    // fprintf(eb->log_file, "%s(eb=%p, re_str='%s')\n",__func__, eb, re_str);
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
        FD_ZERO(&eb->inFds);
        FD_SET(STDIN_FILENO, &eb->inFds);
        FD_SET(eb->masterFd, &eb->inFds);

        if(select(eb->masterFd + 1, &eb->inFds, NULL, NULL, NULL) == -1){
            exit(89);
        }

        if(FD_ISSET(eb->masterFd, &eb->inFds)) {
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
            if(err == 0){ // REG_NOERROR not available on mac
                size_t before_len = rm[0].rm_so;
                strncpy(eb->before, eb->buffer, before_len);
                eb->before[before_len] = '\0';

                size_t match_len = rm[0].rm_eo - rm[0].rm_so;
                strncpy(eb->after, (eb->buffer + rm[0].rm_so), match_len);
                eb->after[match_len] = '\0';

                // fprintf(eb->log_file, "%s(): Match found '%c'\n", __func__, *cursor);
                return 0;
            } else if(err != REG_NOMATCH){
                fprintf(eb->log_file, "Error using regex\n");
                return 1;
            }

            // TODO: There is no check that we have reached the buffer size,
            // - Add a check
            // - Think about turning it into a circular buffer so that chars
            // - recorded earlier get overwritten and we don't need ...
            //   circular buffer is too much.  The only reason I'm coding this
            //   in C is to learn.  There is almost no way I would ever use
            //   this library.
            cursor++;
        }

    }
    return 0;
}
