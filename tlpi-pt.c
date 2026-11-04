/*
 * This is made from the listings in section 64 PSEUDOTERMINALS in
 * The Linux Programming Interface book by Michael Kerrisk.
 */

#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>

#include "tlpi-pt.h"

struct termios ttyOrig;

void ttyReset(void)
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

#ifdef TIOCSCTTY
    // The book says this code is meant for BSD but I ran it on a Linux
    // computer and this macro was defined.
    fprintf(stderr, "TIOCSCTTY is defined: we might be on BSD\r\n");
    if(ioctl(slaveFd, TIOCSCTTY, 0) == -1){
        exit(1);
    }
#endif

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
