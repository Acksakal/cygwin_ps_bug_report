/*
 * pscheck.c - check how a shell started by different methods shows up
 *             in "ps -ef" and "ps -W" on Cygwin.
 *
 * Build:
 *   gcc pscheck.c -o pscheck -std=c90 -D_POSIX_C_SOURCE=200809L -Wall -Wpedantic
 *
 * Usage:
 *   ./pscheck            same as ./pscheck system
 *   ./pscheck system     start the shell with system()
 *   ./pscheck popen      start the shell with popen()
 *   ./pscheck fork       start the shell with fork() + execl()
 *   ./pscheck spawn      start the shell with posix_spawn()
 *   ./pscheck analyze    do not run anything, re-read the saved files
 *
 * The shell writes its own PID and WINPID, then runs "ps -W" and
 * "ps -ef" while it is still alive. Only the rows that concern the
 * shell are printed, so no unrelated process names end up in the output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>

extern char **environ;

#define IDS_FILE  "/tmp/ids.txt"
#define PSW_FILE  "/tmp/psW.txt"
#define PSEF_FILE "/tmp/psef.txt"

static const char SHELL_CMD[] =
    "echo $$ $(cat /proc/$$/winpid) > " IDS_FILE "; "
    "ps -W > " PSW_FILE "; "
    "ps -ef > " PSEF_FILE;

static int run_system(void)
{
    return system(SHELL_CMD);
}

static int run_popen(void)
{
    FILE *p;
    char buf[256];

    p = popen(SHELL_CMD, "r");
    if (p == NULL) {
        perror("popen");
        return -1;
    }
    while (fgets(buf, sizeof buf, p) != NULL)
        fputs(buf, stdout);
    return pclose(p);
}

static int run_fork(void)
{
    pid_t c;
    int status;

    c = fork();
    if (c < 0) {
        perror("fork");
        return -1;
    }
    if (c == 0) {
        execl("/bin/sh", "sh", "-c", SHELL_CMD, (char *)NULL);
        _exit(127);
    }
    if (waitpid(c, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    return status;
}

static int run_spawn(void)
{
    pid_t c;
    int status, err;
    char *argv[4];

    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = (char *)SHELL_CMD;
    argv[3] = NULL;

    err = posix_spawn(&c, "/bin/sh", NULL, NULL, argv, environ);
    if (err != 0) {
        fprintf(stderr, "posix_spawn: error %d\n", err);
        return -1;
    }
    if (waitpid(c, &status, 0) < 0) {
        perror("waitpid");
        return -1;
    }
    return status;
}

/* ps -ef rows look like:  UID PID PPID TTY STIME COMMAND  */
static int scan_psef(long shell_pid)
{
    FILE *f;
    char line[4096];
    long pid, ppid;
    int hits = 0;

    f = fopen(PSEF_FILE, "r");
    if (f == NULL) {
        perror(PSEF_FILE);
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        if (sscanf(line, "%*s %ld %ld", &pid, &ppid) == 2
            && pid == shell_pid) {
            printf("  %s", line);
            hits++;
        }
    }
    fclose(f);
    return hits;
}

/*
 * ps -W rows look like:  [S] PID PPID PGID WINPID TTY UID STIME COMMAND
 * The first character is an optional status flag (S, I or O), so a row
 * may start with a letter before the PID.
 */
static int scan_psw(long shell_pid, long shell_winpid,
                    int *by_pid, int *by_winpid)
{
    FILE *f;
    char line[4096];
    long a, b, c, d;
    int n, m_pid, m_win;

    *by_pid = 0;
    *by_winpid = 0;

    f = fopen(PSW_FILE, "r");
    if (f == NULL) {
        perror(PSW_FILE);
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        n = sscanf(line, "%ld %ld %ld %ld", &a, &b, &c, &d);
        if (n != 4)
            n = sscanf(line, " %*c %ld %ld %ld %ld", &a, &b, &c, &d);
        if (n != 4)
            continue;

        m_pid = (a == shell_pid);
        m_win = (d == shell_winpid);
        if (m_pid || m_win) {
            printf("  [%s%s] %s",
                   m_pid ? "PID matches" : "",
                   m_win ? (m_pid ? " + WINPID matches" : "WINPID matches")
                         : "",
                   line);
            if (m_pid)
                (*by_pid)++;
            if (m_win)
                (*by_winpid)++;
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode;
    long shell_pid = 0, shell_winpid = 0;
    int rc = 0, n_ef, w_by_pid, w_by_win;
    FILE *f;

    mode = (argc > 1) ? argv[1] : "system";

    printf("mode: %s\n", mode);
    printf("my pid=%ld  my parent pid=%ld\n",
           (long)getpid(), (long)getppid());

    if (strcmp(mode, "analyze") != 0) {
        remove(IDS_FILE);
        remove(PSW_FILE);
        remove(PSEF_FILE);

        if (strcmp(mode, "system") == 0)
            rc = run_system();
        else if (strcmp(mode, "popen") == 0)
            rc = run_popen();
        else if (strcmp(mode, "fork") == 0)
            rc = run_fork();
        else if (strcmp(mode, "spawn") == 0)
            rc = run_spawn();
        else {
            fprintf(stderr,
                    "unknown mode '%s' (system|popen|fork|spawn|analyze)\n",
                    mode);
            return 2;
        }
        printf("start method returned %d\n", rc);
    }

    f = fopen(IDS_FILE, "r");
    if (f == NULL) {
        perror(IDS_FILE);
        return 1;
    }
    if (fscanf(f, "%ld %ld", &shell_pid, &shell_winpid) != 2) {
        fprintf(stderr, "could not read shell pid and winpid from %s\n",
                IDS_FILE);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("\nshell: cygwin pid=%ld  winpid=%ld\n", shell_pid, shell_winpid);

    printf("\nrows in 'ps -ef' whose PID column is the shell:\n");
    n_ef = scan_psef(shell_pid);

    printf("\nrows in 'ps -W' matching the shell's PID or WINPID:\n");
    scan_psw(shell_pid, shell_winpid, &w_by_pid, &w_by_win);

    printf("\nsummary (%s):\n", mode);
    printf("  ps -ef rows with PID == shell        : %d\n", n_ef);
    printf("  ps -W  rows with PID == shell        : %d\n", w_by_pid);
    printf("  ps -W  rows with WINPID == shell's   : %d\n", w_by_win);
    printf("\nfiles kept for inspection: %s %s %s\n",
           IDS_FILE, PSW_FILE, PSEF_FILE);
    return 0;
}
