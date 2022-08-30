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

/*语法分析*/
/*内置命令数量*/
#define BUILT_IN_COMMAND_NUM (15)
/*内置命令数组*/
char *BUILT_IN_COMMAND[BUILT_IN_COMMAND_NUM] = {"bg", "cd", "clr", "dir", "echo", "exec", "exit", "fg", "help", "jobs", "pwd", "time", "set", "umask", "test"};
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";
/*输入输出文件名*/
char ifile[1024], ofile[1024];
/*输入输出模式*/
int imode, omode;
/*后台模式*/
int bgmode;

/*命令参数大小*/
#define MAX_CMD_PHASE_NUM (100)
/*分割命令*/
int splitcmd(char *cmd, char **arr);
/*解析命令*/
char **parsecmd(char **arr);
/*执行命令*/
int runcmd();
/*执行单个命令（视管道为复合命令）*/
int runRawcmd(char *cmd);

/*检测字符串中是否有某一字符*/
char *strchr(const char *s, int c)
{
    for (; *s; s++)
    {
        if (*s == c)
            return (char *)s;
    }
    return NULL;
}
/*检测字符串数组中是否有某一字符串*/
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

/*测试部分*/

#define TEST_MODE
/* 置一时报错 */
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
/* 输出日志 */
#define LOG(fmt, ...) printf("\33[1;34m[%s,%d,%s]] " fmt "\33[0m\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#else
#define LOG(fmt, ...)
#endif
/* 报错待办事项 */
#define TODO() panic_on(1, "Please implement me");
/*test part end*/
/*执行函数句柄部分*/
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

/*这里的意思是注册一个返回类型是int， 参数是char** 的函数指针handler，
 * 调用的时候使用格式： innercmd[cmd2index(cmd)].handler(argv); 即可*/
typedef int (*handler)(char **);

/*注册内部命令结构*/
struct icmd
{
    char *name;
    handler handler;
};
/*这是一个结构数组，SH_XX是数组序号，大括号里的内容为隐名结构*/
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
/*函数名到结构数组序号的转化函数*/
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
/*function handler end*/

/*进程池部分*/
/*进程池大小*/
#define MAX_JOB 100
/*在进程池里添加任务，返回任务序号*/
int addjob(int pid, char *pname, int pstatus);
/*在进程池里删除任务，返回任务序号*/
int deljob(int pid);
/*唯一的前台进程*/
struct foregroundprocess
{
    int pid;
    char pname[1005];
} FG;
/*进程的三种状态*/
enum PSTATUS
{
    PDEAD,   //结束或杀死
    PALIVE,  //运行中
    PSUSPEND //挂起
};
/*进程池*/
struct childProcess
{
    char pname[105]; //进程名
    int pid;         //进程号
    int status;      //进程状态
} childProcessPool[MAX_JOB];
/*终端模式*/
struct globalConfig
{
    int termMode_;
} P;

/*终端状态*/
static struct termios termios_Orig;
static struct termios termios_Editor;
/*信号处理的信号状态*/
struct sigaction osig;
struct sigaction nsig;
/*注册环境变量shell*/
char SHELLPATH[105]; // shell目录
void initEnv()
{
    char *path = (char *)malloc(sizeof(char) * 128);
    getcwd(path, 128);
    putenv("shell");
    setenv("shell", path, 1);
    strcpy(SHELLPATH, path);
}
/*子进程返回信号处理函数*/
void hSIGCHLD(int sig_no, siginfo_t *info, void *vcontext)
{
    pid_t pid = info->si_pid;
    int i;
    // 检测是否进程池中有该进程
    for (i = 0; i < MAX_JOB; i++)
    {
        if (pid == childProcessPool[i].pid)
            break;
    }
    // 在进程池中杀死该进程
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
/*停机挂起信号处理函数（ctrl-z）*/
void hSIGTSTP(int sig_no)
{
    /*停止前台进程并转到后台*/
    if (FG.pid != -1)
    {
        addjob(FG.pid, FG.pname, PSUSPEND);
        kill(FG.pid, SIGSTOP);
        FG.pid = -1;
        strcpy(FG.pname, "");
    }
}
/*信号初始化函数*/
void initSig()
{
    // 新信号初始化
    memset(&nsig, 0, sizeof(nsig));
    nsig.sa_sigaction = hSIGCHLD;
    nsig.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&nsig.sa_mask);
    // SIGCHLD信号需要sigaction函数来注册，注意SIGCHLD只能被主进程处理
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
/*全局初始化*/
int sConfigInit()
{
    //进程池初始化
    for (int i = 0; i < MAX_JOB; i++)
    {
        strcpy(childProcessPool[i].pname, "");
        childProcessPool[i].pid = 0;
        childProcessPool[i].status = PDEAD;
    }
    //终端初始化
    P.termMode_ = 0;
    //信号和环境变量初始化
    initSig();
    initEnv();
    return 0;
}
/*读命令*/
int getcmd(FILE *F, char *buf, int nbuf)
{
    //读的时候意味着需要处理输入了，在这之前打印当前工作目录
    if (F == NULL)
    {
        char path[128];
        getcwd(path, 128);
        printf("(%s)> ", path);

        memset(buf, 0, nbuf);
        //这里我脑子不好用了系统调用来读，一定要注意在读之前要处理缓冲
        //意思是printf会滞留在缓冲中，一般的read函数会提前将缓冲处理掉
        //但是直接调用系统调用的话不会处理缓冲，需要手动调用memset
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
    else
    {
        if (fgets(buf, 512, F) != NULL)
            return 0;
        else
            exit(0);
    }
}
/*解析后台命令：如果是后台命令的话，将字符串中的&去掉并bgmode设1*/
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
/*注意：fg和bg命令都牵扯到主进程中的进程池，并不能在子进程中进行修改，不能像其他内置命令一样fork子进程来处理
 *虽然但是也是可以fork子进程来实现的，不过fork的两个进程（一个exec别的命令的进程，一个fg/bg进程）没有直接关系
 *因此不能用wait系列的函数来查询了，只能用轮询的方式并调用kill，会增加程序的负荷。（而且很麻烦所以鸽了）*/
int execjobs(char *buf)
{
    //如果是fg命令
    if (buf[0] == 'f' && buf[1] == 'g' && buf[2] == ' ')
    {
        //得到任务index
        int job_num = atoi(buf + 3);
        if (job_num < 0)
        {
            fprintf(stderr, "Could not bring process to foreground: Invalid job number.\n");
            return -1;
        }
        //在进程池中移除后台进程
        if (childProcessPool[job_num].status == PDEAD)
        {
            //如果是死进程，报错
            perror("nonexistent or dead process");
            return -1;
        }
        else
        {
            if (childProcessPool[job_num].status == PALIVE)
            {
                //如果是运行进程，调到前台
                int parsejob_pid = childProcessPool[job_num].pid;
                deljob(parsejob_pid);
                //前台命令的含义就是让主进程等待该进程完成再进行操作
                waitpid(parsejob_pid, NULL, WUNTRACED);
                return 0;
            }
            else if (childProcessPool[job_num].status == PSUSPEND)
            {
                //如果是挂起进程，先将程序继续运行在调到前台
                int parsejob_pid = childProcessPool[job_num].pid;
                kill(parsejob_pid, SIGCONT);
                deljob(parsejob_pid);
                waitpid(parsejob_pid, NULL, WUNTRACED);
                return 0;
            }
            else // PDEAD
            {
                perror("Dead process!\n");
                return -1;
            }
        }
    }
    //如果是bg命令
    else if (buf[0] == 'b' && buf[1] == 'g' && buf[2] == ' ')
    {
        //获得任务号
        int job_num = atoi(buf + 3);
        if (job_num < 0)
        {
            fprintf(stderr, "Could continue background process: Invalid job number.\n");
            return -1;
        }
        //非法情况
        else if (childProcessPool[job_num].status != PSUSPEND)
        {
            fprintf(stderr, "Process nonexist or not suspend.\n");
            return -1;
        }
        //合法情况：发送SIGCONT继续进程
        else
        {
            kill(childProcessPool[job_num].pid, SIGCONT);
            childProcessPool[job_num].status = PALIVE;
            return 0;
        }
    }
    return -1;
}
/*主循环*/
int main(int argc, char **argv)
{
    FILE *F = NULL;
    if (argv[1])
        F = fopen(argv[1], "r");
    //全局初始化
    sConfigInit();
    char buf[512];
    //设置标准输出
    setbuf(stdout, NULL);
    //单独处理cd命令：子进程无法对主进程的工作目录进行修改
    while (getcmd(F, buf, 512) == 0)
    {
        if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ')
        {
            buf[strlen(buf) - 1] = 0;
            //用系统调用改变工作目录
            if (syscall(SYS_chdir, buf + 3) < 0)
            {
                fprintf(stderr, "cannot cd %s\n", buf + 3);
                return 1;
            }
        }
        else
        {
            //单独处理fg，bg命令
            if ((execjobs(buf)) == -1)
            {
                //其他内置命令和外部命令
                //解析后台命令
                bgmode = parsebgmode(buf);
                //子进程处理命令
                int pid = fork();
                //在子进程里
                if (pid == 0)
                {
                    //按照题目要求，注册环境变量parent
                    putenv("parent");
                    setenv("parent", SHELLPATH, 1);
                    //执行命令
                    runcmd(buf);
                    exit(0);
                }
                //在主进程里
                else if (pid > 0)
                {
                    //后台命令
                    if (!!bgmode)
                    {
                        //在进程池中加入任务
                        int jobid = addjob(pid, buf, PALIVE);
                        printf("[%d] %d\n", jobid, pid);
                    }
                    //前台命令
                    else
                    {
                        //设置前台命令参数
                        FG.pid = pid;
                        strcpy(FG.pname, buf);
                    }
                    //如果是前台命令，等待完成，如果是后台命令，不予等待
                    waitpid(pid, NULL, (!!bgmode) ? WNOHANG : WUNTRACED);
                    //前台命令执行完毕要重置前台命令参数
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
}
//检测字符串是否由数字构成（atoi函数前置）
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
//废弃的处理后台命令的函数，不予注释
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
//废弃的处理cd命令的函数，不予注释
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
    //如果没有参数，则显示当前目录
    else
    {
        char path[128];
        getcwd(path, 128);
        printf("%s\n", path);
    }
    return 0;
}
//清屏函数
int sh_clr(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "clr"), "Parsing Err");
    if (argv[0] != NULL)
    {
        // 光标移动到左上角并且输出全屏清屏的转义序列
        const char *CLEAR_SCREEN_ANSI = "\e[1;1H\e[2J";
        write(STDOUT_FILENO, CLEAR_SCREEN_ANSI, 12);
        return 0;
    }
    else
    {
        //错误的调用格式
        fprintf(stderr, "clr format: clr");
        return 1;
    }
}
int sh_dir(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "dir"), "Parsing Err");
    char *p = argv[1];
    //错误的调用格式
    if (p == NULL)
    {
        fprintf(stderr, "dir format: dir <directory>");
        return 1;
    }
    //用dirent结构遍历目录
    struct dirent *d;
    DIR *dh = opendir(p);
    //检测目录合法性
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
    //遍历文件
    while ((d = readdir(dh)) != NULL)
    {
        printf("%s", d->d_name);
        printf("  ");
    }
    printf("\n");
    return 0;
}
// echo命令：简单的字符串输出
int sh_echo(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "echo"), "Parsing Err");
    //合法
    if (argv[1])
    {
        printf("%s\n", argv[1]);
        return 0;
    }
    //非法
    else
    {
        perror("echo format: echo <comment>\n");
        return 1;
    }
}
// exec执行脚本文件
int sh_exec(char **argv) // may have spaces in the front(used by sh_time)
{
    //检测语法
    panic_on(strcmp(argv[0], "exec"), "Parsing Err");
    int aind = 1;
    while (argv[aind])
    {
        char *const args[] = {NULL};
        char *const env[] = {NULL};
        //调用execve执行命令
        execve(argv[aind], args, env);
        aind++;
    }
    return 0;
}
//退出主进程
int sh_exit(char **argv)
{
    //注意：进行在子进程里的exit需要杀死父进程
    panic_on(strcmp(argv[0], "exit"), "Parsing Err");
    kill(getppid(), 9);
    exit(0);
}
//废弃的前台处理函数，不予注释
int sh_fg(char **argv)
{
    panic_on(strcmp(argv[0], "fg"), "Parsing Err");
    int pid = atoi(argv[1]);
    if (pid < 0)
    {
        fprintf(stderr, "Could not bring process to foreground: Invalid job number.\n");
        return 1;
    }
    else
    {
        printf("pid = %d", pid);
        // protect shell against signals for illegal use of stdin and stdout
        int killpid;
        while ((killpid = kill(pid, 0)) != -1)
        {
            printf("killpid = %d\n", killpid);
            sleep(1);
        }

        printf("waitend");
    }
    return 0;
}
//帮助信息
const char help_message[] = {
    "1. bg: 将后台挂起的命令继续在后台执行\n"
    "格式: bg <jobid>\n"
    "实例: bg 0 \n\n"
    "2. cd: 改变工作目录，如果没有参数，则显示当前目录\n"
    "格式: cd [directory]\n"
    "实例: cd ..\n\n"
    "3. clr: 清屏\n"
    "格式: clr\n\n"
    "4. dir: 显示某个目录中所有文件\n"
    "格式: dir <directory>\n"
    "实例: dir .\n\n"
    "5. echo: 输出<comment>并换行\n"
    "格式: echo <comment>\n"
    "实例: echo 123\n\n"
    "6. exec: 执行文件\n"
    "格式: exec <file>\n"
    "实例: exec /bin/ls\n\n"
    "7. exit: 退出shell \n"
    "格式: exit\n\n"
    "8. fg: 将后台任务序号为<jobid>的进程转到前台运行\n"
    "格式: fg <jobid>\n"
    "实例: fg 0\n\n"
    "9. help: 输出帮助信息\n"
    "格式: help\n\n"
    "10. jobs: 输出任务列表\n"
    "格式: jobs\n\n"
    "11. pwd: 输出当前工作目录\n"
    "格式: pwd\n\n"
    "12. set: 设置环境变量，如果没有参数，输出所有环境变量\n"
    "格式: set [environment_variable][value]\n"
    "实例: set PWD /tmp\n\n"
    "13. test: 比较表达式大小 \n"
    "格式: test <variable a> <compare symbol> <variable b>\n"
    "实例: test 1 -lt 2\n\n"
    "14. time: 输出当前时间\n"
    "格式: time\n\n"
    "15. umask: 输出当前掩码 \n"
    "格式: umask\n\n"};
//输出帮助信息
int sh_help(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "help"), "Parsing Err");
    printf("%s", help_message);
    return 1;
}
//进程名状态的字符串名称
const char *pstatus[] = {
    "DEAD", "ALIVE", "STOPPED"};

//列出所有任务进程
int sh_jobs(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "jobs"), "Parsing Err");
    for (int i = 0; i < MAX_JOB; ++i)
    {
        //如果进程不是结束的和被杀死的，则输出
        if (childProcessPool[i].status == PALIVE || childProcessPool[i].status == PSUSPEND)
        {
            printf("[%d] %d | %s | %s\n", i, childProcessPool[i].pid, childProcessPool[i].pname, pstatus[childProcessPool[i].status]);
        }
    }
    return 1;
}
//输出当前工作目录
int sh_pwd(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "pwd"), "Parsing Err");
    char path[128];
    //获得当前目录并输出
    getcwd(path, 128);
    printf("%s\n", path);
    return 1;
}
//设置环境变量，如果没有参数，输出环境变量
int sh_set(char **argv)
{
    //检测语法
    panic_on(strcmp(argv[0], "set"), "Parsing Err");
    //输出全部
    if (!argv[1])
    {
        //用environ内置变量来遍历
        extern char **environ;
        for (int i = 0; environ[i] != NULL; i++)
            printf("%s\n", environ[i]);
        return 0;
    }
    else if (argv[1] && argv[2] && !argv[3])
    {
        //如果有参数，尝试寻找并设置该环境变量
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
    //错误参数
    else
    {
        perror("\"set\" need 0 or 2 parameters");
        return 1;
    }
}
//输出当前时间
int sh_time(char **argv)
{
    panic_on(strcmp(argv[0], "time"), "Parsing Err");
    time_t t = time(NULL);
    printf("Current date and time is : %s", ctime(&t));
    return 0;
}
//输出掩码
int sh_umask(char **argv)
{
    panic_on(strcmp(argv[0], "umask"), "Parsing Err");
    //如果没有参数，直接输出
    if (!argv[1])
    {
        mode_t temp;
        // umask函数：设置新的掩码，返回旧的掩码
        temp = umask(0);
        umask(temp);
        printf("%04d\n", temp);
        return 0;
    }
    //检测要设置的掩码是否符合数字规范
    if (strlen(argv[1]) > 4)
    {
        perror("umask [umask_code]: umask_code need 1 to 4 bit number");
        return 1;
    }
    // umask的数字只有0-7
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
    //注意取余操作
    unsigned int mask = atoi(argv[1]) % 1000;
    umask(mask);
    return 0;
}
/*测试数字和字符串*/
int sh_test(char **argv)
{
    panic_on(strcmp(argv[0], "test"), "Parsing Err");
    if (!argv[1] || !argv[2] || !argv[3] || argv[4])
    {
        perror("test accept 3 parameters\n");
        return 1;
    }
    //字符串相等
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
    //字符串不相等
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
    //数字相等
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
    //数字不相等
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
    //数字大于
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
    //数字大于等于
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
    //数字小于
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
    //数字小于等于
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
//在进程池里添加任务
int addjob(int pid, char *pname, int pstatus)
{
    int i = 0;
    for (; i < MAX_JOB; i++)
    {
        if (childProcessPool[i].status == PDEAD)
        {
            childProcessPool[i].pid = pid;            //设置pid
            strcpy(childProcessPool[i].pname, pname); //设置进程名
            childProcessPool[i].status = pstatus;     //设置进程状态
            break;
        }
    }

    return (i == MAX_JOB) ? -1 : i;
}
//在进程池里删除任务
int deljob(int pid)
{
    int i = 0;
    for (; i < MAX_JOB; i++)
    {
        if (childProcessPool[i].pid == pid)
        {
            childProcessPool[i].pid = -1;          //清空pid
            strcpy(childProcessPool[i].pname, ""); //清空进程名
            childProcessPool[i].status = PDEAD;    //杀死进程
        }
    }
    return (i == MAX_JOB) ? i : -1;
}
//执行命令:重要函数
int runcmd(char *str)
{
    //命令头
    char *start = str;
    for (; *str; str++)
    {
        //处理管道：如果有管道标识符，直接清除
        if ((*str == '|') && (*(str - 1) == ' ') && (*(str + 1) == ' '))
        {
            *str = '\0';
            //建立管道输入输出
            int p[2];
            int pid;
            pipe(p);
            // fork第一个子进程：关闭stdout，然后将文件标识符p[1]复制到stdout，
            //之后关闭管道的两端，实际上是将输出端通到管道的输入端
            if ((pid = fork()) == 0)
            {
                close(1); // stdout
                dup(p[1]);
                close(p[0]);
                close(p[1]);
                runRawcmd(start);
            }
            // fork第二个子进程：关闭stdin，然后将文件标识符p[0]复制到stdin，
            //之后关闭管道的两端，实际上是将输出端通到管道的输入端
            if ((pid = fork()) == 0)
            {
                close(0);
                dup(p[0]);
                close(p[0]);
                close(p[1]);
                //递归的执行管道之后的命令：可以处理多重管道
                runcmd(str + 1);
            }
            //在主进程里：关闭管道
            close(p[0]);
            close(p[1]);
            //等待子进程都完成
            wait(NULL);
            wait(NULL);
            return 0;
        }
    }
    //关于管道的操作已经在循环内部return了，如果没有管道，仍需要执行单指令
    runRawcmd(start);
    return 0;
}
//执行无管道指令
int runRawcmd(char *cmd)
{
    // argv
    char *arr[MAX_CMD_PHASE_NUM];
    //分割命令
    splitcmd(cmd, arr);
    //解释命令
    char **argv = parsecmd(arr);
    if (!argv[0])
    {
        perror("empty command!");
        return 1;
    }
    else
    {
        //从文件读入
        if (imode == 1)
        {
            int fileFd = 0;
            fileFd = open(ifile, O_RDONLY, 0666);
            // stdin重定向到fildFd
            if (dup2(fileFd, fileno(stdin)) == -1)
                fprintf(stderr, "dup2 failed!\n");
            close(fileFd);
        }
        //覆盖写
        if (omode == 1)
        {
            int fileFd = 0;
            fileFd = open(ofile, O_RDWR | O_CREAT | O_TRUNC, 0666);
            // stdout重定向到fildFd
            if (dup2(fileFd, fileno(stdout)) == -1)
                fprintf(stderr, "dup2() failed!\n");
            close(fileFd);
        }
        //追加写
        if (omode == 2)
        {
            int fileFd = 0;
            fileFd = open(ofile, O_RDWR | O_CREAT | O_APPEND, 0666);
            // stdout重定向到fildFd
            if (dup2(fileFd, fileno(stdout)) == -1)
                fprintf(stderr, "dup2() failed!\n");
            close(fileFd);
        }
        //如果在内部命令表里：执行内部命令
        if (strin(BUILT_IN_COMMAND, BUILT_IN_COMMAND_NUM, argv[0]))
        {
            innercmd[cmd2index(cmd)].handler(argv);
        }
        //如果不在内部命令表里：进行系统调用
        else
        {
            execvp(argv[0], argv);
        }
        return 0;
    }
}
//分割字符串
int splitcmd(char *cmd, char **arr)
{
    char *p = cmd;
    //把argv清空
    for (int i = 0; i < MAX_CMD_PHASE_NUM; i++)
    {
        arr[i] = NULL;
    }
    // strtok函数将字符串分割
    if ((arr[0] = strtok(p, whitespace)) == NULL)
        return 1;
    int argc = 1;
    while (argc < MAX_CMD_PHASE_NUM)
    {
        // strtok函数将字符串分割
        if ((arr[argc] = strtok(NULL, whitespace)) == NULL)
            return argc;
        argc++;
    }
    return 0;
}
//解释命令
char **parsecmd(char **arr)
{
    //输入输出文件清空
    ifile[0] = '\0';
    ofile[0] = '\0';
    imode = omode = 0;
    //新分配argv空间
    char **argv = (char **)malloc(sizeof(char *) * MAX_CMD_PHASE_NUM);
    for (int i = 0; i < MAX_CMD_PHASE_NUM; i++)
    {
        argv[i] = NULL;
    }
    int vind = 0;  // argv中的下标
    int aind = 0;  // arr中的下标
    int state = 0; // 0:普通模式 1:等待读入文件名模式 2:等待输出文件名模式
    while (arr[aind] && aind < MAX_CMD_PHASE_NUM)
    {
        if (strlen(arr[aind]) == 1 && strchr(symbols, *arr[aind]))
        {
            char symb = *arr[aind];
            if (state == 0)
            {
                //注意test命令的特殊化
                //文件重定向读入
                if (symb == '<' && strcmp(arr[0], "test"))
                {
                    state = 1;
                    imode = 1;
                }
                // test的小于好
                else if (symb == '<' && !strcmp(arr[0], "test"))
                {
                    argv[vind] = arr[aind];
                    vind++;
                }
                //文件重定向的覆盖写
                else if (symb == '>' && strcmp(arr[0], "test"))
                {
                    state = 2;
                    omode = 1;
                }
                // test的大于号
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
        //文件重定向的追加写
        else if (!strcmp(arr[aind], ">>"))
        {
            state = 2;
            omode = 2;
            aind++;
        }
        //常规参数或者文件名
        else
        {
            //常规参数
            if (state == 0)
            {
                argv[vind] = arr[aind];
                vind++;
                aind++;
            }
            //读入文件名
            else if (state == 1)
            {
                strcpy(ifile, arr[aind]);
                aind++;
                state = 0;
            }
            //写文件名
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
