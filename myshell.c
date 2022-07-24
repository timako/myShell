#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <termios.h>
#include <dirent.h>
#include <assert.h>

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

typedef int (*handler)(char *);

/*inner cmd*/
struct icmd
{
    char *name;
    handler handler;
};

int sh_bg(char *str);
int sh_cd(char *str);
int sh_clr(char *str);
int sh_dir(char *str);
int sh_echo(char *str);
int sh_exec(char *str);
int sh_exit(char *str);
int sh_fg(char *str);
int sh_help(char *str);
int sh_jobs(char *str);
int sh_pwd(char *str);
int sh_set(char *str);
int sh_time(char *str);
int sh_umask(char *str);

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
    { "umask", sh_umask }};

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

/*Teiminal*/
static struct termios termios_Orig;
static struct termios termios_Editor;

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
    termios_Editor.c_lflag &= ~(ISIG);

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

int main()
{
    char buf[512];
    while (getcmd(buf, 512))
    {
        int pid = fork();
        if (pid == 0)
        {
            runcmd(parsecmd(buf));
            panic_on(1, "Should not reach here");
        }
        else if (pid > 0)
        {
            waitpid(pid, NULL, 0);
            continue;
        }
        else
        {
            panic_on(1, "Fork Err");
        }
    }
}

FILE *
get_outfd_from_str(char *str)
{
    char *filename = NULL;
    int ret = sscanf(str, "*>%[^ ]", filename);
    if(ret == 0){
      if((ret = sscanf(str, "*>>%[^ ]", filename))== 0){
        return stdout;
      }
      FILE *fd = fopen(filename, 'a');
      if (fd < 0)
          perror("Invalid filename");
      return fd;
    }
    LOG("Filename = %s\n", filename); // May have error here, need further log test
    FILE *fd = fopen(filename, 'w');
    if (fd < 0)
        perror("Invalid filename");
    return fd;
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

int sh_bg(char *str)
{
    panic_on(str[0] != 'b' || str[1] != 'g' || str[2] != '\0', "Parsing Err");
    char *delim = " ";
    char *p;
    int jobid;
    /*check all paras*/
    p = strtok(str, delim);
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
        p = strtok(NULL, delim);
    }
    /*process all paras*/
    p = strtok(str, delim);
    while (p != NULL)
    {
        jobid = atoi(p);
        if (childProcessPool[jobid].status == PSUSPEND)
        {
            kill(childProcessPool[jobid].pid, SIGCONT);
        }
        else
        {
            fprintf(stderr, "pid: %d doesn't exist to continue\n", jobid);
        }
        /*At last*/
        p = strtok(NULL, delim);
    }
    return 0;
}

int sh_cd(char *str)
{
    panic_on(str[0] != 'c' || str[1] != 'd' || str[2] != '\0', "Parsing Err");
    if (syscall(SYS_chdir, str + 3) < 0)
        fprintf(stderr, "cannot cd %s\n", str + 3);
    return 1;
}

int sh_clr(char *str)
{
    if (strlen(str) == 3 && str[0] == 'c' && str[1] == 'l' && str[2] == 'r' && str[3] == '\0')
    {
        write(STDOUT_FILENO, "\x1b[2J", 4);
    }
    else
    {
        fprintf(stderr, "clr format: clr");
    }
}
int sh_dir(char *str)
{
    panic_on(str[0] != 'd' || str[1] != 'i' || str[2] != 'r' || str[3] != '\0', "Parsing Err");
    char *delim = " ";
    char *p;
    p = strtok(str, delim);
    if (strcmp(p, "dir") == 0)
        p = strtok(NULL, delim);
    struct dirent *d;
    DIR *dh = opendir(p);
    if (!dh)
    {
        if (errno == ENOENT)
        {
            perror("Directory doesn't exist");
        }
        else
        {
            perror("unable to read directory");
        }
    }
    while ((d = readdir(dh)) != NULL)
    {
        FILE *fd = get_outfd_from_str();
        fprintf(fd, d->d_name);
        fprintf(fd, " ");
    }
}

int sh_echo(char *str)
{
    panic_on(str[0] != 'e' || str[1] != 'c' || str[2] != 'h' || str[3] != 'o' || str[4] != '\0', "Parsing Err");
    FILE *fd = get_outfd_from_str();
    fprintf(fd, str + 5);
    fclose(fd);
    return 1;
}

int sh_exec(char *str) // may have spaces in the front(used by sh_time)
{
    panic_on(str[0] != 'e' || str[1] != 'x' || str[2] != 'e' || str[3] != 'c' || str[4] != '\0', "Parsing Err");
    char *delim = " ";
    char *p;
    p = strtok(str, delim);
    p = strtok(NULL, delim);
    execve(p, NULL, NULL);
}

int sh_exit(char *str)
{
    panic_on(str[0] != 'e' || str[1] != 'x' || str[2] != 'i' || str[3] != 't' || str[4] != '\0', "Parsing Err");
    exit(0);
}

int sh_fg(char *str)
{
    panic_on(str[0] != 'f' || str[1] != 'g' || str[2] != '\0', "Parsing Err");
    char *delim = " ";
    char *p;
    p = strtok(str, delim);
    p = strtok(NULL, delim);
    int job_num = strtol(p, NULL, 0);
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

int sh_help(char *str)
{
    panic_on(str[0] != 'h' || str[1] != 'e' || str[2] != 'l' || str[3] != 'p' || str[4] != '\0', "Parsing Err");
    FILE *fd = get_outfd_from_str();
    fprintf(fd, help_message);
    fclose(fd);
    return 1;
}

const char *pstatus[] = {
    "DEAD", "ALIVE", "STOPPED"};

int sh_jobs(char *str)
{
    FILE *fd = get_outfd_from_str();
    for (int i = 0; i < MAX_JOB; ++i)
    {
        if (childProcessPool[i].status == PALIVE)
        {
            fprintf(fd, "%d | %s | %s\n", childProcessPool[i].pid, childProcessPool[i].pname, pstatus[childProcessPool[i].status]);
        }
    }
    fclose(fd);
    return 1;
}

int sh_pwd(char *str)
{
    char path[128];
    getcwd(path, 128);
    FILE *fd = get_outfd_from_str();
    fprintf(fd, "%s\n", path);
    fclose(fd);
    return 1;
}

int sh_set(char *str){
  extern char **environ;
  for (char **env = environ; *env; ++env)
      printf("%s\n", *env);

}

int sh_time(char *str)
{
    int pid = fork();
    if (pid == 0)
    {
        struct timeval val1, val2;
        int ret = gettimeofday(&val1, NULL);
        if (ret == -1)
        {
            perror("Err: gettimeofday\n");
            exit(1);
        }

        int cpid = fork();
        if (cpid == 0)
        {
            // exec the file chosen
            sh_exec(str + 5);
            exit(0);
            panic_on(1, "Should not reach here\n");
        }
        else if (cpid > 0)
        {
            waitpid(cpid, NULL, 0);
            int ret = gettimeofday(&val2, NULL);
            if (ret == -1)
            {
                perror("Err: gettimeofday\n");
                exit(1);
            }
            unsigned long totaltime = val1.tv_sec * 1000000 + val1.tv_usec - (val2.tv_sec * 1000000 + val2.tv_usec);
            FILE *fd = get_outfd_from_str(fd);
            fprintf(fd, "%ld\n", totaltime);
            fclose(fd);
            exit(0);
        }
        else
        {
            perror("Fork Err");
            exit(1);
        }
    }
    else if (pid > 0)
    {
        waitpid(pid, NULL, 0);
        return 1;
    }
    else
    {
        panic_on(1, "Fork Err");
    }
}

int sh_umask(char *str);
