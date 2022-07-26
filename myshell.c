#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdbool.h>
#include <termios.h>
#include <dirent.h>
#include <assert.h>
#include <parser.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <sys/wait.h>
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
    SH_UMASK
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

typedef int (*handler)(char *);

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
    { "childProcessPool", sh_jobs },
    [SH_PWD]
    { "pwd", sh_pwd },
    [SH_SET]
    { "set", sh_set },
    [SH_TIME]
    { "time", sh_time },
    [SH_UMASK]
    { "umask", sh_umask }};

struct foregroundprocess
{
    int pid;
    char *pname;
} FG;
enum PSTATUS
{
    PDEAD,
    PALIVE,
    PSUSPEND
};
struct childProcess
{
    char *pname;
    int pid;
    int status;
} childProcessPool[MAX_JOB];

struct globalConfig
{
    int termMode_;
} P;
struct environmentvariable
{
    char *shell;
    char *parent;
    char *directory;
} E;

/*Teiminal*/
static struct termios termios_Orig;
static struct termios termios_Editor;
/*Signal handler*/
struct sigaction osig;
struct sigaction nsig;

void initEnvironmentVariable()
{
    char *path = (char *)malloc(sizeof(char) * 128);
    getcwd(path, 128);
    E.directory = path;
}
void hSIGCHLD(int sig_no, siginfo_t *info, void *vcontext)
{
    pid_t pid = info->si_pid;
    int i;
    for (i = 0; i < MAX_JOB; i++)
    {
        if (pid == childProcessPool[i].pid)
            break;
    }

    if (i < MAX_JOB)
    {

        if (childProcessPool[i].status == PALIVE)
        {
            childProcessPool[i].status = PDEAD;
            deljob(pid);
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
        FG.pname = NULL;
    }
}

void initsig()
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
    initsig();
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
            break;
        }
    }
}
extern int bgmode;
int main()
{
    char buf[512];
    while (getcmd(buf, 512))
    {
        bgmode = parsebgmode(buf);
        int pid = fork();
        if (pid == 0)
        {
            runcmd();
            // panic_on(1, "Should not reach here");
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
            continue;
        }
        else
        {
            panic_on(1, "Fork Err");
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

int sh_bg(char **argv)
{
    panic_on(!strcmp(argv[0], "bg"), "Parsing Err");
    char *delim = " ";
    char *p;
    int jobid;
    /*check all paras*/
    p = argv[1];
    int ind = 1;
    while (p != NULL)
    {
        jobid = atoi(p);
        bool atoi_valid_flag = 1;
        if (jobid == 0)
        {
            int len = strlen(p);
            for (int i = 0; i < len; i++)
            {
                if (p[i] > '9' || p[i] < '0')
                {
                    atoi_valid_flag = 0;
                    break;
                }
            }
        }
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

    panic_on(!strcmp(argv[0], "cd"), "Parsing Err");
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
    panic_on(!strcmp(argv[0], "clr"), "Parsing Err");
    if (argv[1] != NULL)
    {
        write(STDOUT_FILENO, "\x1b[2J", 4);
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
    panic_on(!strcmp(argv[0], "dir"), "Parsing Err");
    char *delim = " ";
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
        printf(" ");
    }
    return 0;
}

int sh_echo(char **argv)
{
    panic_on(!strcmp(argv[0], "echo"), "Parsing Err");
    if (argv[1])
    {
        printf("%s", argv[1]);
        return 0;
    }
    else
    {
        perror("echo format: echo <comment>");
        return 1;
    }
}

int sh_exec(char **argv) // may have spaces in the front(used by sh_time)
{
    panic_on(!strcmp(argv[0], "exec"), "Parsing Err");
    int aind = 1;
    while (argv[aind])
    {
        execve(argv[aind], NULL, NULL);
        aind++;
    }
}

int sh_exit(char **argv)
{
    panic_on(!strcmp(argv[0], "exit"), "Parsing Err");
    exit(0);
}

int sh_fg(char **argv)
{
    panic_on(!strcmp(argv[0], "fg"), "Parsing Err");
    char *delim = " ";
    char *p = argv[0];

    int job_num = atoi(argv[1]);
    if (job_num <= 0)
    {
        fprintf(stderr, "Could not bring process to foreground: Invalid job number.\n");
        return;
    }
    if (childProcessPool[job_num].status == PDEAD)
    {
        perror("nonexistent or dead process");
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
}

const char help_message[] = {
    "Usage: blablabla\n"};

int sh_help(char **argv)
{
    panic_on(!strcmp(argv[0], "help"), "Parsing Err");
    printf("%s", help_message);
    return 1;
}

const char *pstatus[] = {
    "DEAD", "ALIVE", "STOPPED"};

int sh_jobs(char **argv)
{
    panic_on(!strcmp(argv[0], "jobs"), "Parsing Err");
    for (int i = 0; i < MAX_JOB; ++i)
    {
        if (childProcessPool[i].status == PALIVE || childProcessPool[i].status == PSUSPEND)
        {
            printf("[%d] %d | %s | %s\n", childProcessPool[i].pid, childProcessPool[i].pname, pstatus[childProcessPool[i].status]);
        }
    }
    return 1;
}

int sh_pwd(char **argv)
{
    panic_on(!strcmp(argv[0], "pwd"), "Parsing Err");
    char path[128];
    getcwd(path, 128);
    printf("%s\n", path);
    return 1;
}

int sh_set(char **argv)
{
    TODO();
    extern char **environ;
    for (char **env = environ; *env; ++env)
        printf("%s\n", *env);
}

int sh_time(char **argv)
{
    panic_on(!strcmp(argv[0], "time"), "Parsing Err");
    time_t t = time(NULL);
    printf("\n Current date and time is : %s", ctime(&t));
}

int sh_umask(char **argv)
{
    panic_on(!strcmp(argv[0], "umask"), "Parsing Err");
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
}
