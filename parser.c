#include <parser.h>
int parse_cmd(char * str)
{
  char *start = str;
  for(;*str;str++){
    if((*str == '|') && (*(str-1)==' ') && (*(str+1) == ' ')){
      *str = '\0';
      int p[2];
      int pid;
      pipe(p);
      if((pid = fork()) == 0){
        close(1);
        dup(p[1]);
        close(p[0]);
        close(p[1]);
        runRawcmd(start);
      }
      if((pid = fork()) == 0){
        close(0);
        dup(p[0]);
        close(p[0]);
        close(p[1]);
        parse_cmd(str + 1);
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

int runRawcmd(char * cmd){
  if ( cmd contains internal cmd ){
    call related cmd;
  }
  else {
    deal with &
    deal with redircmd
    parse argv
    execvp
  }
}
