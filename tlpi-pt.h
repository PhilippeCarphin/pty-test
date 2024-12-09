#ifndef TLPI_PT_H
#define TLPI_PT_H

#include <stdlib.h>
#include <sys/ioctl.h>

#define MAX_SNAME 1000

extern struct termios ttyOrig;

void ttyReset(void);
int ttySetRaw(int fd, struct termios *prefTermios);
int ptyMasterOpen(char *slaveName, size_t snLen);
pid_t ptyFork(int *masterFd, char *slaveName, size_t snLen, const struct termios *slaveTermios, const struct winsize *slaveWS);

#endif // TLPI_PT_H
