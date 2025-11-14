#include <stdio.h>
#include <unistd.h> // pid_t
#include <sys/select.h> // fd_set
#include <sys/ioctl.h>

typedef struct pty {
    char *buffer;
    char *cursor;
    char *before;
    char *after;
    int masterFd;
    FILE *log_file;
    FILE *pty_output;
    fd_set inFds;
    pid_t child_pid;
    size_t buf_size;
    struct winsize ws;
} pty_t;

int pty_expect(pty_t *eb, const char *re_str);
int pty_send_discard(pty_t *eb, const char *cmd, const char *prompt);
void ptySend(int fd, const char *text, size_t sz);

pty_t *pty_spawnvp(const char *file, char *const argv[], size_t buf_size);
int pty_send(pty_t *eb, const char *text, size_t chunk_size);
int pty_log(pty_t * eb, const char *fmt, ...);

#define pty_debug(eb, fmt, ...) \
    pty_log(eb, "%s():%d: " fmt, __func__, __LINE__, __VA_ARGS__)

