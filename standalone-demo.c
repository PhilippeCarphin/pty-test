
#include <string.h> // sys_signame and others.
#define _XOPEN_SOURCE 600
#include <stdlib.h> // exit, posix_openpt, grantpt, unlockpt, ptsname, atexit
#include <fcntl.h> // O_RDWR, O_NOCTTY
#include <pthread.h> // pthread_t, pthread_create
#include <unistd.h> // STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, setsid, close, dup2, fork, execvp, read, write
#include <stdio.h>

#include <termios.h> // struct termios, tcgetattr, tcsetattr, flags like ICANON, TCSANOW, ...
#include <sys/ioctl.h> // ioctl
#include <signal.h>

#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>

#define BUF_SIZE 256
#define MAX_SNAME 1000

FILE *log_file;
struct termios tty_orig;
volatile int child_finished = 0;

void tty_reset(void);
int start_worker(int master_fd, int argc, char *argv[]);
void sigchld_handler(int signal);
int tty_set_raw(int fd);

/*
 * Normally it would be functions so I don't have to write this description of
 * what main() does but I noticed the entire process fits inside one function
 * and it's easier to understand this way.
 */

/*
 * - Initialization
 * - Create a pseudoterminal device
 * - Fork
 *   - Child:
 *     - closes master_fd and opens slave device by name
 *     - Set terminal and file descriptor things
 *     - Exec something
 *   - Parent
 *     - Set terminal to raw and atexit to restore
 *     - Start worker that will send keys to the slave
 *     - Run the main loop
 */
int main(int argc, char **argv)
{
    struct winsize ws;

    /*
     * Initialization
     */
    signal(SIGCHLD, sigchld_handler);

    if(tcgetattr(STDIN_FILENO, &tty_orig) == -1){
        exit(1);
    }

    if(ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0){
        exit(1);
    }

    log_file = fopen("log.txt", "a");
    if(log_file == NULL){
        exit(1);
    }
    setbuf(log_file, NULL);

    /*
     * Create pseudoterminal
     */
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if(master_fd < 0){
        perror("posix_openpt()");
        exit(1);
    }

    if(grantpt(master_fd)){
        perror("grantpt()");
        exit(1);
    }

    if(unlockpt(master_fd)){
        perror("unlockpt()");
        exit(1);
    }

    /*
     * Fork a child process
     */
    pid_t child_pid = fork();

    /*
     * CHILD
     * - Get name of slave device
     * - Become session leader
     * - Close master_fd
     * - Open slave device
     * - Set terminal properties
     * - "connect" stdin, stdout, stderr to slaveFd
     * - Exec some other program
     */
    if(child_pid == 0){
        char slaveName[1000];
        char *p = ptsname(master_fd);
        if(p == NULL){
            perror("ptsname()");
            exit(1);
        }
        strcpy(slaveName, p);

        if(setsid() == -1){
            perror("setsid()");
            exit(1);
        }

        if(close(master_fd)){
            perror("close()");
            exit(1);
        }

        int slaveFd = open(slaveName, O_RDWR);
        if(slaveFd == -1){
            exit(1);
        }

#ifdef TIOCSCTTY
        if(ioctl(slaveFd, TIOCSCTTY, 0) == -1){
            fprintf(log_file, "CHILD: ERROR: ioctl(slaveFd, TIOCSCTTY, 0) -> -1\r\n");
            exit(1);
        }
#endif

        if(tcsetattr(slaveFd,TCSANOW, &tty_orig) == -1){
            fprintf(log_file, "CHILD: ERROR: tcsetattr(slaveFd, TCSANOW, &tty_orig) -> -1\r\n");
            exit(1);
        }

        int err = ioctl(slaveFd, TIOCSWINSZ, &ws);
        if(err == -1){
            fprintf(log_file, "CHILD: ioctl(slaveFd, TIOCSWINSZ, &ws) -> %d\r\n", err);
            exit(1);
        }

        if(dup2(slaveFd, STDIN_FILENO) != STDIN_FILENO){
            fprintf(log_file, "CHILD: ERROR: dup2(slaveFd, STDIN_FILENO)\r\n");
            exit(1);
        }

        if(dup2(slaveFd, STDOUT_FILENO) != STDOUT_FILENO){
        fprintf(log_file, "CHILD: ERROR: dup2(slaveFd, STDOUT_FILENO)\r\n");
            exit(1);
        }

        if(dup2(slaveFd, STDERR_FILENO) != STDERR_FILENO){
            fprintf(log_file, "CHILD: ERROR: dup2(slaveFd, STDERR_FILENO)\r\n");
            exit(1);
        }

        fprintf(log_file, "Child exec-ing [%s", argv[3]);
        for(int i=4; i < argc; i++){
            fprintf(log_file, ", %s", argv[i]);
        }
        fprintf(log_file, "]\n");
        execvp(argv[3], &argv[3]);
    }

    /*
     * PARENT
     */
    if(tty_set_raw(STDIN_FILENO)){
        exit(1);
    }

    if(atexit(tty_reset) != 0){
        exit(88);
    }

    /*
     * Create a thread that will send keys to the slave device
     */
    if(start_worker(master_fd, argc, argv)){
        exit(1);
    }

    /*
     * Main parent loop: Read from STDIN and MasterFd
     * - Anything read on master_fd comes from the slave, write it our STDOUT
     * - Anything read on STDIN comes from the user, write it to the slave
     */
    char buf[BUF_SIZE];
    fd_set inFds;
    ssize_t numRead;

    for(;;){
        FD_ZERO(&inFds);
        FD_SET(STDIN_FILENO, &inFds);
        FD_SET(master_fd, &inFds);

        if(select(master_fd + 1, &inFds, NULL, NULL, NULL) == -1){
            fprintf(log_file, "MASTER: select() -> -1\n");
            exit(1);
        }

        if(child_finished){
            fprintf(log_file, "MASTER: child finished\n");
            exit(0);
        }

        // Transmit suff on our STDIN to the slave
        if(FD_ISSET(STDIN_FILENO, &inFds)){
            numRead = read(STDIN_FILENO, buf, BUF_SIZE);
            if(numRead < 0){
                fprintf(log_file, "MASTER: read(STDIN) -> numRead < 0\n");
                exit(0);
            }
            int numWrite = write(master_fd, buf, numRead);
            if(numWrite < 0){
                fprintf(log_file, "MASTER: Failed write (numRead == -1)\r\n");
                exit(1);
            }
            if(numWrite != numRead){
                fprintf(log_file, "MASTER: Partial/failed write (master_fd)\r\n");
                exit(1);
            }

        }

        // Transmit everything from master_fd to our STDOUT
        if(FD_ISSET(master_fd, &inFds)) {
            numRead = read(master_fd, buf, BUF_SIZE);
            if(numRead < 0){
                fprintf(stderr, "MASTER: read(STDIN) -> numRead < 0\n");
                exit(0);
            }
            if(write(STDOUT_FILENO, buf, numRead) != numRead){
                fprintf(stderr, "Partial/failed write (stdout)\n");
            }
        }
    }
    return 0;
}

void sleep_ms(int ms){
    struct timespec ts = { .tv_sec=(ms/1000), .tv_nsec = 1000000 * (ms % 1000) };
    nanosleep(&ts, NULL);
}

void chunk_send(int fd, const char *text, size_t sz){
    size_t len = strlen(text);
    const char *p = text;
    for(int i = 0; i < len; i += sz, p += sz){
        sleep_ms(40);
        size_t write_len = ( i+sz > len ? len-i : sz );
        if(write(fd, p, write_len) != write_len){
            fprintf(stderr, "Partial/failed write (master_fd)\n");
        }
    }
}

struct worker_args {
    int master_fd;
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

    chunk_send(args->master_fd, "set +o history\n", 4);
    sleep_ms(500);
    chunk_send(args->master_fd, "unset PROMPT_COMMAND; PS1=\"PTY-TEST $ \"\n", 4);

    chunk_send(args->master_fd, "bind \"set completion-query-items -1\"\n", 4);
    sleep_ms(500);

    chunk_send(args->master_fd, "bind \"set completion-display-width 0\"\n", 4);

    chunk_send(args->master_fd, "bind \"set page-completions off\"\n", 4);
    sleep_ms(500);

    chunk_send(args->master_fd, "echo \"YAYBOO YAYBOO it's lots of fun to do\"\n", 1);
    chunk_send(args->master_fd, "echo \"if you like it holler YAY\"\n", 1);
    chunk_send(args->master_fd, "echo \"and if you don't you holler BOO\"\n", 1);
    sleep_ms(500);

    chunk_send(args->master_fd, args->argv[2], 1);
    chunk_send(args->master_fd, "\t\t", 1);
    sleep_ms(1000);
    return NULL;
}

int start_worker(int master_fd, int argc, char *argv[]){
    struct worker_args wa = {
        .master_fd = master_fd,
        .argc = argc,
        .argv = argv,
        .log_file = log_file
    };
    pthread_t async_writer;
    return pthread_create(&async_writer, NULL, worker, &wa);
}

void tty_reset(void)
{
    if(tcsetattr(STDIN_FILENO, TCSANOW, &tty_orig) == -1){
        exit(77);
    }
}

void sigchld_handler(int signal){
    fprintf(log_file, "MASTER: Signal %d(%s) received, setting child_finished to 1\r\n", signal, sys_signame[signal]);
    child_finished = 1;
}

int tty_set_raw(int fd)
{
    struct termios t;

    if(tcgetattr(fd, &t) == -1){
        return -1;
    }

    t.c_lflag &= ~ICANON; // do not canonicalize input lines
    t.c_lflag &= ~ISIG;   // do not enable INTR, QUIT, [D]SUSP
    t.c_lflag &= ~IEXTEN; // do not enable DISCARD and LNEXT
    t.c_lflag &= ~ECHO;   // do not enable echoing

    t.c_iflag &= ~IGNBRK; // do not ignore BREAK condition
    t.c_iflag &= ~BRKINT; // do not map BREAK to SIGINTR
    t.c_iflag &= ~PARMRK; // do not mark parity and framing errors
    t.c_iflag &= ~ISTRIP; // do not strip 8th bit off chars
    t.c_iflag &= ~INPCK;  // do not enable checking of parity errors
    t.c_iflag &= ~ICRNL;  // do not map CR to NL
    t.c_iflag &= ~IGNCR;  // do not ignore CR
    t.c_iflag &= ~IXON;   // do not enable outptu flow control

    t.c_oflag &= ~OPOST;  // do not enable following output processing

    t.c_cc[VMIN] = 1;     // Minimum number of bytes that should be received when the read function successfully returns
    t.c_cc[VTIME] = 0;    // No timer in byte reception (see man 4 termios)

    if(tcsetattr(fd, TCSAFLUSH, &t) == -1){
        return -1;
    }

    return 0;
}
// [1] See 'man 2 select' for why it's 'master_fd + 1'.
//
//  > The first nfds descriptors are checked in each set; i.e., the
//  > descriptors from 0 through nfds-1 in the descriptor sets are
//  > examined.
//
//  So it's kind of like select is gonna do
//
//  for(fd = 0; fd < nfds; fd++){
//      if(<fd is in fdsed>){
//          check it or somethig
//      }
//  }
