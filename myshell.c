#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <termios.h>
#include <dirent.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>

/*parse part*/
#define BUILT_IN_COMMAND_NUM (15)
char *BUILT_IN_COMMAND[BUILT_IN_COMMAND_NUM] = {"bg", "cd", "clr", "dir", "echo", "exec", "exit", "fg", "help", "jobs", "pwd", "time", "set", "umask", "test"};
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

char ifile[1024], ofile[1024];
int imode, omode;
int bgmode;

#define MAX_CMD_PHASE_NUM (100)

int splitcmd(char *cmd, char **arr);
char **parsecmd(char **arr);
int runcmd();
int runRawcmd(char *cmd);

// return jobid;
int addjob(int pid, char *pname, int pstatus);
// return jobid;
int deljob(int pid);
char *strchr(const char *s, int c)
{
    for (; *s; s++)
    {
        if (*s == c)
            return (char *)s;
    }
    return NULL;
}
int strin(char *arr[], int len, char *target)
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
/*parse part end*/

/*test part*/
#define MAX_JOB 100
#define TEST_MODE

#define panic_on(cond, str, ...)                                                                         \
    do                                                                                                   \
    {                                                                                                    \
        if (cond)                                                                                        \
        {                                                                                                \
            printf("\33[1;34m[%s,%d,%s]] " str "\33[0m\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
            exit(1);                                                                                     \
        }                                                                                                \
    } while (0)

#ifdef TEST_MODE
#define LOG(fmt, ...) printf("\33[1;34m[%s,%d,%s]] " fmt "\33[0m\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#else
#define LOG(fmt, ...)
#endif

#define TODO() panic_on(1, "Please implement me");
/*test part end*/
enum
{
    SH_BG = 1,
    SH_CD,
    SH_CLR,
    SH_DIR,
    SH_ECHO,
    SH_EXEC,
    SH_EXIT,
    SH_FG,
    SH_HELP,
    SH_JOBS,
    SH_PWD,
    SH_SET,
    SH_TIME,
    SH_UMASK,
    SH_TEST
};

int sh_bg(char **argv);
int sh_cd(char **argv);
int sh_clr(char **argv);
int sh_dir(char **argv);
int sh_echo(char **argv);

int sh_exec(char **argv);
int sh_exit(char **argv);
int sh_fg(char **argv);
int sh_help(char **argv);
int sh_jobs(char **argv);

int sh_pwd(char **argv);
int sh_set(char **argv);
int sh_time(char **argv);
int sh_umask(char **argv);
int sh_test(char **argv);

typedef int (*handler)(char **);

/*inner cmd*/
struct icmd
{
    char *name;
    handler handler;
};
struct icmd innercmd[] = {
    [SH_BG]
    { "bg", sh_bg },
    [SH_CD]
    { "cd", sh_cd },
    [SH_CLR]
    { "clr", sh_clr },
    [SH_DIR]
    { "dir", sh_dir },
    [SH_ECHO]
    { "echo", sh_echo },
    [SH_EXEC]
    { "exec", sh_exec },
    [SH_EXIT]
    { "exit", sh_exit },
    [SH_FG]
    { "fg", sh_fg },
    [SH_HELP]
    { "help", sh_help },
    [SH_JOBS]
    { "jobs", sh_jobs },
    [SH_PWD]
    { "pwd", sh_pwd },
    [SH_SET]
    { "set", sh_set },
    [SH_TIME]
    { "time", sh_time },
    [SH_UMASK]
    { "umask", sh_umask },
    [SH_TEST]
    { "test", sh_test }};
int cmd2index(char *cmd)
{
    if (!strcmp(cmd, "bg"))
        return SH_BG;
    if (!strcmp(cmd, "cd"))
        return SH_CD;
    if (!strcmp(cmd, "clr"))
        return SH_CLR;
    if (!strcmp(cmd, "dir"))
        return SH_DIR;
    if (!strcmp(cmd, "echo"))
        return SH_ECHO;
    if (!strcmp(cmd, "exec"))
        return SH_EXEC;
    if (!strcmp(cmd, "exit"))
        return SH_EXIT;
    if (!strcmp(cmd, "fg"))
        return SH_FG;
    if (!strcmp(cmd, "help"))
        return SH_HELP;
    if (!strcmp(cmd, "jobs"))
        return SH_JOBS;
    if (!strcmp(cmd, "pwd"))
        return SH_PWD;
    if (!strcmp(cmd, "set"))
        return SH_SET;
    if (!strcmp(cmd, "time"))
        return SH_TIME;
    if (!strcmp(cmd, "umask"))
        return SH_UMASK;
    if (!strcmp(cmd, "test"))
        return SH_TEST;
    return 0;
}
struct foregroundprocess
{
    int pid;
    char pname[1005];
} FG;
enum PSTATUS
{
    PDEAD,
    PALIVE,
    PSUSPEND
};
struct childProcess
{
    char pname[105];
    int pid;
    int status;
} childProcessPool[MAX_JOB];

struct globalConfig
{
    int termMode_;
} P;

/*Teiminal*/
static struct termios termios_Orig;
static struct termios termios_Editor;
/*Signal handler*/
struct sigaction osig;
struct sigaction nsig;

void initEnv()
{
    char *path = (char *)malloc(sizeof(char) * 128);
    getcwd(path, 128);
    putenv("shell");
    setenv("shell", path, 1);
}
void hSIGCHLD(int sig_no, siginfo_t *info, void *vcontext)
{
    pid_t pid = info->si_pid;
    int i;
    if (!!bgmode)
    {
        for (i = 0; i < MAX_JOB; i++)
        {
            if (pid == childProcessPool[i].pid)
                break;
        }

        if (i < MAX_JOB)
        {
            if (childProcessPool[i].status == PALIVE)
            {
                printf("已完成 [%d] %d %s\n", i, pid, childProcessPool[i].pname);
                childProcessPool[i].status = PDEAD;
                deljob(pid);
            }
        }
    }
}
void hSIGTSTP(int sig_no)
{
    if (FG.pid != -1)
    {
        addjob(FG.pid, FG.pname, PSUSPEND);
        kill(FG.pid, SIGSTOP);
        FG.pid = -1;
        strcpy(FG.pname, "");
    }
}

void initSig()
{
    memset(&nsig, 0, sizeof(nsig));
    nsig.sa_sigaction = hSIGCHLD;
    nsig.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&nsig.sa_mask);
    sigaction(SIGCHLD, &nsig, &osig);
    signal(SIGTSTP, hSIGTSTP);
    signal(SIGSTOP, hSIGTSTP);
}

/*将shell终端恢复原设置*/
void disableTerminalSettings()
{
    if (P.termMode_ == 1)
    {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_Orig);
        P.termMode_ = 0;
    }
}

int enableTerminalSettings()
{
    tcgetattr(STDIN_FILENO, &termios_Orig);
    if (P.termMode_ == 1)
        return 0;
    /*atexit: 注册当exit()被调用时，在其之前调用的函数*/
    atexit(disableTerminalSettings);
    termios_Editor = termios_Orig;
    /*命令行参数设置*/

    /*取消SIG信号：ctrl-z，ctrl-c*/
    // termios_Editor.c_lflag &= ~(ISIG);

    //设置为当前的命令参数
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios_Editor);
    P.termMode_ = 1;
    return 0;
}

int sConfigInit()
{
    for (int i = 0; i < MAX_JOB; i++)
    {
        strcpy(childProcessPool[i].pname, "");
        childProcessPool[i].pid = 0;
        childProcessPool[i].status = PDEAD;
    }
    P.termMode_ = 0;
    initSig();
    initEnv();
    return 0;
}

int getcmd(char *buf, int nbuf)
{
    char path[128];
    getcwd(path, 128);
    printf("(%s)> ", path);

    memset(buf, 0, nbuf);

    while (nbuf--)
    {
        int nread = syscall(SYS_read, 0, buf, 1);
        if (nread <= 0)
            return -1;
        if (*(buf++) == '\n')
            break;
    }
    return 0;
}
int parsebgmode(char *buf)
{
    bgmode = 0;
    for (char *p = buf; *p != '\0'; p++)
    {
        if (*p == '&')
        {
            bgmode = 1;
            *p = '\0';
            return 1;
        }
    }
    return 0;
}

int main()
{
    sConfigInit();
    char buf[512];
    setbuf(stdout, NULL);
    while (getcmd(buf, 512) == 0)
    {
        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ')
        {
            buf[strlen(buf) - 1] = 0;
            if (syscall(SYS_chdir, buf + 3) < 0)
            {
                fprintf(stderr, "cannot cd %s\n", buf + 3);
                return 1;
            }
        }
        else
        {
            bgmode = parsebgmode(buf);
            int pid = fork();
            if (pid == 0)
            {
                runcmd(buf);
                exit(0);
            }
            else if (pid > 0)
            {
                if (!!bgmode)
                {
                    int jobid = addjob(pid, buf, PALIVE);
                    printf("[%d] %d\n", jobid, pid);
                }
                else
                {
                    FG.pid = pid;
                    strcpy(FG.pname, buf);
                }
                waitpid(pid, NULL, (!!bgmode) ? WNOHANG : WUNTRACED);
                if (!bgmode)
                {
                    FG.pid = -1;
                    strcpy(FG.pname, "");
                }
                continue;
            }
            else
            {
                panic_on(1, "Fork Err");
            }
        }
    }
}

/*NOTE: printf is not async-signal-safe*/
void sigstop()
{
    LOG("process Suspended");
}

void sigcont()
{
    LOG("process Back");
}

void sigint()
{
    LOG("process Interrupt");
    exit(0);
}

int checkIfConsistOfNum(char *str)
{
    char *p = str;
    if (atoi(p) == 0)
    {
        int len = strlen(p);
        for (int i = 0; i < len; i++)
        {
            if (p[i] > '9' || p[i] < '0')
            {
                return 0;
            }
        }
    }
    return 1;
}
int sh_bg(char **argv)
{
    panic_on(strcmp(argv[0], "bg"), "Parsing Err");
    char *p;
    int jobid;
    /*check all paras*/
    p = argv[1];
    int ind = 1;
    while (p != NULL)
    {
        jobid = atoi(p);
        bool atoi_valid_flag = checkIfConsistOfNum(p);
        if (atoi_valid_flag == 0)
        {
            perror("invalid usage. bg <jobid1> [jobid2] [jobid3]...");
            return -1;
        }
        /*At last*/
        ind++;
        p = argv[ind];
    }
    /*process all paras*/
    ind = 1;
    p = argv[1];
    while (p != NULL)
    {
        jobid = atoi(p);
        if (childProcessPool[jobid].status == PSUSPEND)
        {
            kill(childProcessPool[jobid].pid, SIGCONT);
            childProcessPool[jobid].status = PALIVE;
        }
        else
        {
            fprintf(stderr, "pid: %d doesn't exist to continue\n", jobid);
        }
        /*At last*/
        ind++;
        p = argv[ind];
    }
    return 0;
}

int sh_cd(char **argv)
{

    panic_on(strcmp(argv[0], "cd"), "Parsing Err");
    if (argv[1] != NULL)
    {
        if (syscall(SYS_chdir, argv[1]) < 0)
        {
            fprintf(stderr, "cannot cd %s\n", argv[1]);
            return 1;
        }
    }
    else
    {
        char path[128];
        getcwd(path, 128);
        printf("%s\n", path);
    }
    return 0;
}

int sh_clr(char **argv)
{
    panic_on(strcmp(argv[0], "clr"), "Parsing Err");
    if (argv[0] != NULL)
    {
        // move cursor to (1,1) and clear the console
        const char *CLEAR_SCREEN_ANSI = "\e[1;1H\e[2J";
        write(STDOUT_FILENO, CLEAR_SCREEN_ANSI, 12);
        // write(STDOUT_FILENO, "\x1b[2J", 4);
        return 0;
    }
    else
    {
        fprintf(stderr, "clr format: clr");
        return 1;
    }
}
int sh_dir(char **argv)
{
    panic_on(strcmp(argv[0], "dir"), "Parsing Err");
    char *p = argv[1];
    if (p == NULL)
    {
        fprintf(stderr, "dir format: dir <directory>");
        return 1;
    }
    struct dirent *d;
    DIR *dh = opendir(p);
    if (!dh)
    {
        if (errno == ENOENT)
        {
            perror("Directory doesn't exist");
            return 1;
        }
        else
        {
            perror("unable to read directory");
            return 1;
        }
    }
    while ((d = readdir(dh)) != NULL)
    {
        printf("%s", d->d_name);
        printf("  ");
    }
    printf("\n");
    return 0;
}

int sh_echo(char **argv)
{
    panic_on(strcmp(argv[0], "echo"), "Parsing Err");
    if (argv[1])
    {
        printf("%s\n", argv[1]);
        return 0;
    }
    else
    {
        perror("echo format: echo <comment>\n");
        return 1;
    }
}

int sh_exec(char **argv) // may have spaces in the front(used by sh_time)
{
    panic_on(strcmp(argv[0], "exec"), "Parsing Err");
    int aind = 1;
    while (argv[aind])
    {
        char *const args[] = {NULL};
        char *const env[] = {NULL};
        execve(argv[aind], args, env);
        aind++;
    }
    return 0;
}

int sh_exit(char **argv)
{
    panic_on(strcmp(argv[0], "exit"), "Parsing Err");
    kill(getppid(), 9);
    exit(0);
}

int sh_fg(char **argv)
{
    panic_on(strcmp(argv[0], "fg"), "Parsing Err");
    int job_num = atoi(argv[1]);
    if (job_num < 0)
    {
        fprintf(stderr, "Could not bring process to foreground: Invalid job number.\n");
        return 1;
    }
    if (childProcessPool[job_num].status == PDEAD)
    {
        perror("nonexistent or dead process");
        return 1;
    }
    else
    {
        int pid_fg = childProcessPool[job_num].pid;
        // protect shell against signals for illegal use of stdin and stdout
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        // make the process forground
        tcsetpgrp(STDERR_FILENO, pid_fg);
        kill(pid_fg, SIGCONT);
        // end protection from signals
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
    }
    return 0;
}

const char help_message[] = {
    "Usage: blablabla\n"};

int sh_help(char **argv)
{
    panic_on(strcmp(argv[0], "help"), "Parsing Err");
    printf("%s", help_message);
    return 1;
}

const char *pstatus[] = {
    "DEAD", "ALIVE", "STOPPED"};

int sh_jobs(char **argv)
{
    panic_on(strcmp(argv[0], "jobs"), "Parsing Err");
    for (int i = 0; i < MAX_JOB; ++i)
    {
        if (childProcessPool[i].status == PALIVE || childProcessPool[i].status == PSUSPEND)
        {
            printf("[%d] %d | %s | %s\n", i, childProcessPool[i].pid, childProcessPool[i].pname, pstatus[childProcessPool[i].status]);
        }
    }
    return 1;
}

int sh_pwd(char **argv)
{
    panic_on(strcmp(argv[0], "pwd"), "Parsing Err");
    char path[128];
    getcwd(path, 128);
    printf("%s\n", path);
    return 1;
}

int sh_set(char **argv)
{
    panic_on(strcmp(argv[0], "set"), "Parsing Err");
    if (!argv[1])
    {
        extern char **environ;
        for (int i = 0; environ[i] != NULL; i++)
            printf("%s\n", environ[i]);
        return 0;
    }
    else if (argv[1] && argv[2] && !argv[3])
    {
        char *env = getenv(argv[1]);
        if (!env)
        {
            perror("env doesn't exist");
        }
        else
        {
            setenv(argv[1], argv[2], 1);
        }
        return 0;
    }
    else
    {
        perror("\"set\" need 0 or 2 parameters");
        return 1;
    }
}

int sh_time(char **argv)
{
    panic_on(strcmp(argv[0], "time"), "Parsing Err");
    time_t t = time(NULL);
    printf("Current date and time is : %s", ctime(&t));
    return 0;
}

int sh_umask(char **argv)
{
    panic_on(strcmp(argv[0], "umask"), "Parsing Err");
    if (!argv[1])
    {
        mode_t temp;
        temp = umask(0);
        umask(temp);
        printf("%04d\n", temp);
        return 0;
    }
    if (strlen(argv[1]) > 4)
    {
        perror("umask [umask_code]: umask_code need 1 to 4 bit number");
        return 1;
    }
    char *invalidDigit = "89";
    char *p = argv[1];
    for (; *p != '\0'; p++)
    {
        if (strchr(invalidDigit, *p))
        {
            perror("umask [umask_code]: umask_code need digit valued 0 to 7");
            return 1;
        }
    }
    unsigned int mask = atoi(argv[1]) % 1000;
    umask(mask);
    return 0;
}
/*hard to implement bash test*/
int sh_test(char **argv)
{
    panic_on(strcmp(argv[0], "test"), "Parsing Err");
    if (!argv[1] || !argv[2] || !argv[3] || argv[4])
    {
        perror("test accept 3 parameters\n");
        return 1;
    }
    if (!strcmp(argv[2], "="))
    {
        if (!strcmp(argv[1], argv[3]))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "!="))
    {
        if (strcmp(argv[1], argv[3]))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "-eq"))
    {
        if (checkIfConsistOfNum(argv[1]) && checkIfConsistOfNum(argv[3]) && atoi(argv[1]) == atoi(argv[3]))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "-ne"))
    {
        if (checkIfConsistOfNum(argv[1]) &&
            checkIfConsistOfNum(argv[3]) &&
            atoi(argv[1]) != atoi(argv[3]))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "-gt") || !strcmp(argv[2], ">"))
    {
        if (checkIfConsistOfNum(argv[1]) &&
            checkIfConsistOfNum(argv[3]) &&
            atoi(argv[1]) > atoi(argv[3]))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "-ge"))
    {
        if (checkIfConsistOfNum(argv[1]) &&
            checkIfConsistOfNum(argv[3]) &&
            (atoi(argv[1]) > atoi(argv[3]) ||
             atoi(argv[1]) == atoi(argv[3])))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "-lt") || !strcmp(argv[2], "<"))
    {
        if (checkIfConsistOfNum(argv[1]) &&
            checkIfConsistOfNum(argv[3]) &&
            atoi(argv[1]) < atoi(argv[3]))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    else if (!strcmp(argv[2], "-le"))
    {
        if (checkIfConsistOfNum(argv[1]) &&
            checkIfConsistOfNum(argv[3]) &&
            (atoi(argv[1]) < atoi(argv[3]) ||
             atoi(argv[1]) == atoi(argv[3])))
        {
            printf("True\n");
        }
        else
        {
            printf("False\n");
        }
    }
    return 0;
}
int addjob(int pid, char *pname, int pstatus)
{
    int i = 0;
    for (; i < MAX_JOB; i++)
    {
        if (childProcessPool[i].status == PDEAD)
        {
            childProcessPool[i].pid = pid;
            strcpy(childProcessPool[i].pname, pname);
            childProcessPool[i].status = PALIVE;
            break;
        }
    }

    return (i == MAX_JOB) ? -1 : i;
}
// return jobid;
int deljob(int pid)
{
    int i = 0;
    for (; i < MAX_JOB; i++)
    {
        if (childProcessPool[i].pid == pid)
        {
            childProcessPool[i].pid = -1;
            strcpy(childProcessPool[i].pname, "");
            childProcessPool[i].status = PDEAD;
        }
    }
    return (i == MAX_JOB) ? i : -1;
}

int runcmd(char *str)
{
    char *start = str;
    for (; *str; str++)
    {
        if ((*str == '|') && (*(str - 1) == ' ') && (*(str + 1) == ' '))
        {
            *str = '\0';
            int p[2];
            int pid;
            pipe(p);
            if ((pid = fork()) == 0)
            {
                close(1); // stdout
                dup(p[1]);
                close(p[0]);
                close(p[1]);
                runRawcmd(start);
            }
            if ((pid = fork()) == 0)
            {
                close(0);
                dup(p[0]);
                close(p[0]);
                close(p[1]);
                runcmd(str + 1);
            }
            close(p[0]);
            close(p[1]);
            wait(NULL);
            wait(NULL);
            return 0;
        }
    }
    runRawcmd(start);
    return 0;
}

int runRawcmd(char *cmd)
{
    char *arr[MAX_CMD_PHASE_NUM];
    splitcmd(cmd, arr);
    char **argv = parsecmd(arr);
    if (!argv[0])
    {
        perror("empty command!");
        return 1;
    }
    else
    {
        if (imode == 1)
        {
            int fileFd = 0;
            fileFd = open(ifile, O_RDONLY, 0666);
            if (dup2(fileFd, fileno(stdin)) == -1)
                fprintf(stderr, "dup2 failed!\n");
            close(fileFd);
        }
        if (omode == 1)
        {
            int fileFd = 0;
            fileFd = open(ofile, O_RDWR | O_CREAT | O_TRUNC, 0666);
            if (dup2(fileFd, fileno(stdout)) == -1)
                fprintf(stderr, "dup2() failed!\n");
            close(fileFd);
        }
        if (omode == 2)
        {
            int fileFd = 0;
            fileFd = open(ofile, O_RDWR | O_CREAT | O_APPEND, 0666);
            if (dup2(fileFd, fileno(stdout)) == -1)
                fprintf(stderr, "dup2() failed!\n");
            close(fileFd);
        }
        if (strin(BUILT_IN_COMMAND, BUILT_IN_COMMAND_NUM, argv[0]))
        {
            innercmd[cmd2index(cmd)].handler(argv);
        }
        else
        {
            execvp(argv[0], argv);
        }
        return 0;
    }
}
int splitcmd(char *cmd, char **arr)
{
    char *p = cmd;
    for (int i = 0; i < MAX_CMD_PHASE_NUM; i++)
    {
        arr[i] = NULL;
    }
    if ((arr[0] = strtok(p, whitespace)) == NULL)
        return 1;
    int argc = 1;
    while (argc < MAX_CMD_PHASE_NUM)
    {
        if ((arr[argc] = strtok(NULL, whitespace)) == NULL)
            return argc;
        argc++;
    }
    return 0;
}
char **parsecmd(char **arr)
{
    ifile[0] = '\0';
    ofile[0] = '\0';
    imode = omode = 0;
    char **argv = (char **)malloc(sizeof(char *) * MAX_CMD_PHASE_NUM);
    for (int i = 0; i < MAX_CMD_PHASE_NUM; i++)
    {
        argv[i] = NULL;
    }
    int vind = 0;
    int aind = 0;
    int state = 0; // 0:普通模式 1:等待读入文件名模式 2:等待输出文件名模式
    while (arr[aind] && aind < MAX_CMD_PHASE_NUM)
    {
        if (strlen(arr[aind]) == 1 && strchr(symbols, *arr[aind]))
        {
            char symb = *arr[aind];
            if (state == 0)
            {
                if (symb == '<' && strcmp(arr[0], "test"))
                {
                    state = 1;
                    imode = 1;
                }
                else if (symb == '<' && !strcmp(arr[0], "test"))
                {
                    argv[vind] = arr[aind];
                    vind++;
                }
                else if (symb == '>' && strcmp(arr[0], "test"))
                {
                    state = 2;
                    omode = 1;
                }
                else if (symb == '>' && !strcmp(arr[0], "test"))
                {
                    argv[vind] = arr[aind];
                    vind++;
                }
                else
                {
                    printf("Not support yet");
                }
                aind++;
            }
            else
            {
                printf("Invalid syntax!");
            }
        }
        else if (!strcmp(arr[aind], ">>"))
        {
            state = 2;
            omode = 2;
            aind++;
        }
        else // argv or filename
        {
            if (state == 0)
            {
                argv[vind] = arr[aind];
                vind++;
                aind++;
            }
            else if (state == 1)
            {
                strcpy(ifile, arr[aind]);
                aind++;
                state = 0;
            }
            else if (state == 2)
            {
                strcpy(ofile, arr[aind]);
                aind++;
                state = 0;
            }
        }
    }
    return argv;
}
