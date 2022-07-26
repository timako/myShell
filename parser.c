#include <parser.h>

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
  // if ( cmd contains internal cmd ){
  //   call related cmd;
  // }
  // else {
  //   deal with &
  //   deal with redircmd
  //   parse argv
  //   execvp
  // }
  char *arr[MAX_CMD_PHASE_NUM];
  int argc = splitcmd(cmd, arr);
  char **argv = parsecmd(arr);
  if (!argv[0])
  {
    perror("empty command!");
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
    if (strin(argv[0], BUILT_IN_COMMAND_NUM, BUILT_IN_COMMAND))
    {
    }
    else
    {
    }
  }
}
int splitcmd(char *cmd, char **arr)
{
  char *p = cmd;
  for (int i = 0; i < MAX_CMD_PHASE_NUM; i++)
  {
    arr[i] = NULL;
  }
  if (arr[0] = strtok(p, whitespace) == NULL)
    return 1;
  int argc = 1;
  while (argc < MAX_CMD_PHASE_NUM)
  {
    if (arr[argc] = strtok(p, NULL) == NULL)
      return argc;
    argc++;
  }
}
char **parsecmd(char **arr)
{
  ifile = NULL;
  ofile = NULL;
  imode = omode = 0;
  char *argv[MAX_CMD_PHASE_NUM];
  for (int i = 0; i < MAX_CMD_PHASE_NUM; i++)
  {
    argv[i] = NULL;
  }
  int vind = 0;
  int aind = 0;
  int state = 0; // 0:普通模式 1:等待读入文件名模式 2:等待输出文件名模式
  while (aind < MAX_CMD_PHASE_NUM)
  {
    if (strlen(arr[aind]) == 1 && strchr(symbols, &arr[aind]))
    {
      char symb = &arr[aind];
      if (state == 0)
      {
        if (symb == '<')
        {
          state = 1;
          imode = 1;
        }
        else if (symb == '>')
        {
          state = 2;
          omode = 1;
        }
        else
        {
          printf("Not support yet");
        }
        aind++;
      }
      else
      {
        print("Invalid syntax!");
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
        ifile = arr[aind];
        aind++;
        state = 0;
      }
      else if (state == 2)
      {
        ofile = arr[aind];
        aind++;
        state = 0;
      }
    }
  }
}
