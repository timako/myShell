#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <string>
#include <assert.h>

#define panic_on(cond, str, ...)                                                                         \
    do                                                                                                   \
    {                                                                                                    \
        if (cond)                                                                                        \
        {                                                                                                \
            printf("\33[1;34m[%s,%d,%s]] " str "\33[0m\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
            exit(1);                                                                                     \
        }                                                                                                \
    } while (0)

#define MAXARGS 10
enum
{
    EXEC = 1,
    PIPE,
    BACK,
    SEMI,
    REDIR
};

struct cmd
{
    int type;
};

struct execcmd : cmd
{
    char *argv[MAXARGS], *eargv[MAXARGS];
    execcmd()
    {
        type = EXEC;
    }
};

struct redircmd : cmd
{
    int fd, mode;
    char *file, *efile;
    struct cmd *cmd;
    redircmd()
    {
        type = REDIR;
    }
    redircmd(struct cmd *subcmd, char *file, char *efile, int mode,
             int fd)
    {
        this->type = REDIR;
        this->cmd = subcmd;
        this->file = file;
        this->efile = efile;
        this->mode = mode;
        this->fd = fd;
    }
};

struct pipecmd : cmd
{
    struct cmd *left, *right;
    pipecmd()
    {
        type = PIPE;
        left = NULL;
        right = NULL;
    }
    pipecmd(struct cmd *left, struct cmd *right)
    {
        this->type = PIPE;
        this->left = left;
        this->right = right;
    }
};

struct semicmd : cmd
{
    struct cmd *left, *right;
    semicmd()
    {
        left = NULL;
        right = NULL;
        type = SEMI;
    }
    semicmd(cmd *left, cmd *right)
    {
        this->left = left;
        this->right = right;
        this->type = SEMI;
    }
};

struct backcmd : cmd
{
    struct cmd *cmd;
    backcmd()
    {
        cmd = NULL;
    }
    backcmd(struct cmd *subcmd)
    {
        this->cmd = subcmd;
        this->type = BACK;
    }
};

struct cmd *parsecmd(char *);
void runcmd(struct  cmd* cmd);

int main(){
    char cbuf[127];
    
}



void runcmd(struct cmd *cmd)
{
    assert(cmd != NULL);
    
}