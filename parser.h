#ifndef PARSER_H
#define PARSER_H

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#define BUILT_IN_COMMAND_NUM (15)
char *BUILT_IN_COMMAND = {"bg", "cd", "exec", "fg", "pwd", "time", "clr", "dir", "set", "echo", "help", "exit", "umask", "test", "jobs"};
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

char *ifile, ofile;
int imode, omode;
int bgmode;

#define MAX_CMD_PHASE_NUM (100)
extern enum PSTATUS {
    PDEAD,
    PALIVE,
    PSUSPEND
};
int splitcmd(char *cmd, char **arr);
char **parsecmd(char **arr);
int runcmd();
// return jobid;
int addjob(int pid, char *pname, int pstatus);
// return jobid;
int deljob(pid);
char *strchr(const char *s, int c)
{
    for (; *s; s++)
    {
        if (*s == c)
            return (char *)s;
    }
    return NULL;
}
int strin(char **arr, int len, char *target)
{
    int i;
    for (i = 0; i < len; i++)
    {
        if (strncmp(arr[i], target, strlen(target)) == 0)
        {
            return 1;
        }
    }
    return 0;
}
#endif