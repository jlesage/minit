#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

/* purpose: argv[1] is the full path to a PID file,
 *          argv+2 is the daemon to run.
 * the daemon is expected to fork in the background and write its PID in
 * the pid file.
 */

extern char** environ;

int main(int argc, char* argv[]) {
  int count=0;
  if (argc<3) {
    write(1,"usage: pidfilehack service /var/run/daemon.pid /usr/sbin/daemon args...\n",72);
    return 0;
  }
  if (unlink(argv[2])) {
    if (errno!=ENOENT) {
      perror("could not remove pid file");
      return 1;
    }
  }
  switch (fork()) {
  case -1:
    perror("could not fork");
    return 2;
  case 0: /* child */
    execve(argv[3],argv+3,environ);
    perror("execvp failed");
    return 3;
  }
  do {
    int fd=open(argv[2],O_RDONLY);
    if (fd>=0) {
      static char buf[100] = "-P";
      int len=read(fd,buf+2,98);
      close(fd);
      if (len>0) {
	char* _argv[] = { "msvc", 0, 0, 0 };
	if (buf[len+1]=='\n')
	  buf[len+1]=0;
	else
	  buf[len+2]=0;
	_argv[1]=buf;
	_argv[2]=argv[1];
/*	printf("execvp %s %s %s\n",_argv[0],_argv[1],_argv[2]); */
	execvp(_argv[0],_argv);
	perror("execvp failed");
	return 0;
      } /* else
	printf("file there but open returned %d\n",fd); */
    } /* else
      printf("%s not there yet\n",argv[2]); */
    sleep(1);
    if (++count>=30)
      exit(0);
  } while (1);
}
