#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/syscall.h>

#define MAXARGS 10 
enum
{
    EXEC = 1,
    PIPE,
    BACK,
    SEMI,
    REDIR
};

class cmd // command node
{
public:
    int type;
};

class execcmd: public cmd{
public:
    char *argv[MAXARGS];
};