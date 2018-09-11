#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

extern char **environ;

/* execute a command and wait for its completion
 * return the command's exit code or -1 on error */
int exec_cmd(char *cmd, ...) {
  char *argv[10];
  va_list arguments;
  pid_t pid;
  int i;

  va_start(arguments, cmd);
  for (i=0;i<9 && (argv[i] = va_arg(arguments,char *)) != NULL; i++);
  argv[i] = NULL;
  va_end(arguments);
  pid = fork();
  if (pid < 0) return -1;
  if (pid > 0) {
    int status;
    if (waitpid(pid,&status,0) == 0) {
      if (!WIFEXITED(status)) return -1;
      return WEXITSTATUS(status);
    }
  } else {
    execve(cmd,argv,environ);
    exit(126);
  }
  return -1;
}
