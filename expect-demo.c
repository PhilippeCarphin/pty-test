#include "tlpi-pt.h"
#include "phil-expect.h"

#include <stdio.h>


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

int main(int argc, char **argv)
{
    FILE *log_file = fopen("log.txt", "a");
    setbuf(log_file, NULL);
    if(log_file == NULL){
        fprintf(stderr, "ERROR opening log_file\n");
    }

    char * const pty_argv[] = { "bash", NULL };
    pty_t *eb = pty_spawnvp("bash -l", pty_argv, 40960);
    eb->log_file = log_file;

    pty_send(eb, "unset PROMPT_COMMAND; PS1=XXPS1XX\n", 100);
    pty_expect(eb, "XXPS1XX");
    pty_debug(eb, "eb.before = '%s'\n", eb->before);
    pty_debug(eb, "eb.after = '%s'\n",  eb->after);

    pty_expect(eb, "XXPS1XX");
    pty_debug(eb, "eb.before = '%s'\n",  eb->before);
    pty_debug(eb, "eb.after = '%s'\n",  eb->after);

    pty_send_discard(eb, "bind \"set bell-style none\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set completion-query-items -1\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set completion-display-width 0\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set page-completions off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set visible-stats off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set colored-completion-prefix off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"set colored-stats off\"\n", "XXPS1XX");
    pty_send_discard(eb, "bind \"enable-bracketed-paste off\"\n", "XXPS1XX");

    pty_send_discard(eb, "stty echo\n", "XXPS1XX");


    pty_send(eb, "echo \"YAYBOO\"\n", 100);
    pty_expect(eb, "YAYBOO");
    pty_expect(eb, "YAYBOO");
    pty_debug(eb, "eb.before = '%s'\n",  eb->before);
    pty_debug(eb, "eb.after = '%s'\n",  eb->after);

    pty_expect(eb, "XXPS1XX");

    pty_send(eb, "rcd \t\t", 100);
    pty_expect(eb, "rcd ");
    pty_expect(eb, "XXPS1XX");
    pty_debug(eb, "eb.before = '%s'\n",  eb->before);
    pty_debug(eb, "eb.after = '%s'\n",  eb->after);
    pty_send(eb, "exit 0\n", 100);

    fprintf(stdout, "%s\n", eb->before);
    return 0;
}




