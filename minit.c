#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <alloca.h>
#include <sys/reboot.h>
#include "fmt.h"
#include "str.h"

#include "minit.h"

#define MALLOC_TEST
#if !defined(__dietlibc__) && !defined(__GLIBC__)
#undef MALLOC_TEST
#endif

#ifdef MALLOC_TEST
extern void* __libc_malloc(size_t size);
extern void* __libc_realloc(void* x,size_t size);
extern void __libc_free(void* x);
static char malloc_buf[2048];
static unsigned long n;
static struct process procbuf[100];
void *malloc(size_t size) {
  if (n+size<sizeof(malloc_buf)) {
    char* tmp=malloc_buf+n;
    n+=size;
    n=(n+3)&~3;
    return tmp;
  }
  return __libc_malloc(size);
}
void free(void* x) {
  if ((char*)x>=malloc_buf && (char*)x<malloc_buf+sizeof(malloc_buf)) return;
  __libc_free(x);
}
void* realloc(void* x,size_t size) {
  if (x==0 || x==procbuf) {
    void* y;
    if (size<=sizeof(procbuf))
      return procbuf;
    y=__libc_malloc(size);
    if (!y) return 0;
    memcpy(y,x,size);
    return y;
  }
  return __libc_realloc(x,size);
}
#endif

char** Argv;

#undef printf
extern int printf(const char *format,...);

#ifdef CONSOLE
extern void opendevconsole();
#endif

static void minitexit(int status);

//#define UPDATE
#ifdef UPDATE
static int doupdate;
#endif

static int i_am_init;

extern int openreadclose(char *fn, char **buf, unsigned long *len);
extern char **split(char *buf,int c,int *len,int plus,int ofs);

extern char **environ;

#ifndef HISTORY
#define HISTORY 10
#endif
#ifdef HISTORY
int history[HISTORY];
#endif

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
    exit(1);
  }
  return -1;
}

/* return index of service in process data structure or -1 if not found */
int findservice(char *service) {
  int i;
  for (i=0; i<=maxprocess; ++i) {
    if (!strcmp(root[i].name,service))
      return i;
  }
  return -1;
}

/* look up process index in data structure by PID */
int findbypid(pid_t pid) {
  int i;
  for (i=0; i<=maxprocess; ++i) {
    if (root[i].pid == pid)
      return i;
  }
  return -1;
}

/* clear circular dependency detection flags */
void circsweep() {
  int i;
  for (i=0; i<=maxprocess; ++i)
    root[i].circular=0;
}

/* add process to data structure, return index or -1 */
int addprocess(struct process *p) {
  if (maxprocess+1>=processalloc) {
    struct process *fump;
    processalloc+=8;
    if ((fump=(struct process *)realloc(root,processalloc*sizeof(struct process)))==0) return -1;
    root=fump;
  }
  memmove(&root[++maxprocess],p,sizeof(struct process));
  return maxprocess;
}

/* load a service into the process data structure and return index or -1
 * if failed */
int loadservice(char *service) {
  struct process tmp;
  int fd;
  if (*service==0) return -1;
  fd=findservice(service);
  if (fd>=0) return fd;
  if (chdir(MINITROOT) || chdir(service)) return -1;
  if (!(tmp.name=strdup(service))) return -1;
  tmp.pid=0;
  fd=open("respawn",O_RDONLY);
  if (fd>=0) {
    tmp.respawn=1;
    close(fd);
  } else
    tmp.respawn=0;
  tmp.startedat=0;
  tmp.circular=0;
  tmp.__stdin=0; tmp.__stdout=1;
  {
    char *logservice=alloca(str_len(service)+5);
    strcpy(logservice,service);
    strcat(logservice,"/log");
    tmp.logservice=loadservice(logservice);
    if (tmp.logservice>=0) {
      int pipefd[2];
      if (pipe(pipefd)) return -1;
      fcntl(pipefd[0],F_SETFD,FD_CLOEXEC);
      fcntl(pipefd[1],F_SETFD,FD_CLOEXEC);
      root[tmp.logservice].__stdin=pipefd[0];
      tmp.__stdout=pipefd[1];
    }
  }
  return addprocess(&tmp);
}

/* usage: isup(findservice("sshd")).
 * returns nonzero if process is up */
int isup(int service) {
  if (service<0) return 0;
  return (root[service].pid!=0);
}

int startservice(int service,int pause,int father);

#undef debug
void handlekilled(pid_t killed, int status) {
  int i;
#ifdef debug
  {
    char buf[50];
    snprintf(buf,50," %d\n",killed);
    write(2,buf,str_len(buf));
  }
#endif
  if (killed == (pid_t)-1) {
    static int saidso;
    if (!saidso) { write(2,"all services exited.\n",21); saidso=1; }
    if (i_am_init) minitexit(0);
  }
  if (killed==0) return;
  i=findbypid(killed);
#if 0
  printf("%d exited, idx %d -> service %s\n",killed,i,i>=0?root[i].name:"[unknown]");
#endif
  if (i>=0) {
    char *argv0=(char*)alloca(PATH_MAX+1);
    if (argv0) {
      char tmp[FMT_LONG]="126";
      int n;
      if (status >= 0 && WIFEXITED(status)) {
        tmp[fmt_long(tmp,WEXITSTATUS(status))]=0;
      }
      n=fmt_str(argv0,MINITROOT "/");
      n+=fmt_str(argv0+n,root[i].name);
      n+=fmt_str(argv0+n,"/finish");
      argv0[n]=0;
      exec_cmd(argv0,"finish",tmp,(char *) 0);
    }

    root[i].pid=0;
    if (root[i].respawn) {
#if 0
      printf("restarting %s\n",root[i].name);
#endif
      circsweep();
      startservice(i,time(0)-root[i].startedat<1,root[i].father);
    } else {
      root[i].startedat=time(0);
      root[i].pid=1;
    }
  }
}

/* called from inside the service directory, return the PID or 0 on error */
pid_t forkandexec(int pause,int service) {
  char **argv=0;
  int count=0;
  pid_t p;
  int fd;
  unsigned long len;
  char *s=0;
  int argc;
  char *argv0=0;
again:
  switch (p=fork()) {
  case (pid_t)-1:
    if (count>3) return 0;
    sleep(++count*2);
    goto again;
  case 0:
    /* child */

    if (i_am_init) {
      ioctl(0, TIOCNOTTY, 0);
      setsid();
#ifdef CONSOLE
      opendevconsole();
#endif
/*      ioctl(0, TIOCSCTTY, 1); */
      tcsetpgrp(0, getpgrp());
    }

    if (pause) {
      struct timespec req;
      req.tv_sec=0;
      req.tv_nsec=500000000;
      nanosleep(&req,0);
    }
    if (!openreadclose("params",&s,&len)) {
      argv=split(s,'\n',&argc,2,1);
      if (argv[argc-1]) argv[argc-1]=0; else argv[argc]=0;
    } else {
      argv=(char**)alloca(2*sizeof(char*));
      argv[1]=0;
    }
    argv0=(char*)alloca(PATH_MAX+1);
    if (!argv || !argv0) _exit(1);
    if (readlink("run",argv0,PATH_MAX)<0) {
      if (errno!=EINVAL) _exit(1);	/* not a symbolic link */
      argv0=strdup("./run");
    }
/*    chdir("/"); */
    argv[0]=strrchr(argv0,'/');
    if (argv[0])
      argv[0]++;
    else
      argv[0]=argv0;
    if (root[service].__stdin != 0) {
      dup2(root[service].__stdin,0);
      fcntl(0,F_SETFD,0);
    }
    if (root[service].__stdout != 1) {
      dup2(root[service].__stdout,1);
      dup2(root[service].__stdout,2);
      fcntl(1,F_SETFD,0);
      fcntl(2,F_SETFD,0);
    }
    {
      int i;
      for (i=3; i<1024; ++i) close(i);
    }
    execve(argv0,argv,environ);
    _exit(1);	
  default:
    fd=open("sync",O_RDONLY);
    if (fd>=0) {
      pid_t p2;
      close(fd);
      p2=waitpid(p,0,0);
      return 1;
    }
    {
      char tmp[FMT_LONG];
      tmp[fmt_long(tmp,p)]=0;
      exec_cmd("./check","check",tmp,(char *) 0);
    }
    return p;
  }
}

/* start a service, return nonzero on error */
int startnodep(int service,int pause) {
  /* step 1: see if the process is already up */
  if (isup(service)) return 0;
  /* step 2: fork and exec service, put PID in data structure */
  if (chdir(MINITROOT) || chdir(root[service].name)) return -1;
  root[service].startedat=time(0);
  root[service].pid=forkandexec(pause,service);
  return root[service].pid;
}

int startservice(int service,int pause,int father) {
  int dir=-1;
  unsigned long len;
  char *s=0;
  pid_t pid=0;
  if (service<0) return 0;
  if (root[service].circular)
    return 0;
  root[service].circular=1;
#if 0
  printf("setting father of %d (%s) to %d (%s)\n",
	 service,root[service].name,father,father>=0?root[father].name:"[msvc]");
#endif
  root[service].father=father;
#ifdef HISTORY
  {
    memmove(history+1,history,sizeof(int)*((HISTORY)-1));
    history[0]=service;
  }
#endif
  if (root[service].logservice>=0)
    startservice(root[service].logservice,pause,service);
  if (chdir(MINITROOT) || chdir(root[service].name)) return -1;
  if ((dir=open(".",O_RDONLY))>=0) {
    if (!openreadclose("depends",&s,&len)) {
      char **deps;
      int depc,i;
      deps=split(s,'\n',&depc,0,0);
      for (i=0; i<depc; i++) {
	int Service,blacklisted,j;
	if (deps[i][0]=='#') continue;
	Service=loadservice(deps[i]);

#if 1
	for (j=blacklisted=0; Argv[j]; ++j)
	  if (Argv[j][0]=='-' && !strcmp(Argv[j]+1,deps[i])) {
	    blacklisted=1;
	    ++Argv[j];
	    break;
	  }
#endif

	if (Service>=0 && root[Service].pid!=1 && !blacklisted)
	  startservice(Service,0,service);
      }
      fchdir(dir);
    }
    pid=startnodep(service,pause);

#if 0
    write(1,"started service ",17);
    write(1,root[service].name,str_len(root[service].name));
    write(1," -> ",4);
    {
      char buf[10];
      snprintf(buf,10,"%d\n",pid);
      write(1,buf,str_len(buf));
    }
#endif
    close(dir);
    dir=-1;
  }
  chdir(MINITROOT);
  return pid;
}

static void minitexit(int status) {	/* exiting on an initialization failure is not a good idea for init */
  char tmp[FMT_LONG];
  char *argv[]={"exit",tmp,0};
  tmp[fmt_int(tmp,status)]=0;
  execve(MINITROOT "/exit",argv,environ);
  _exit(status);
}


static void _puts(const char* s) {
  write(1,s,str_len(s));
}

void childhandler() {
  int status;
  pid_t killed;
#ifdef debug
  write(2,"wait...",7);
#endif
#if 0
  if (getpid()!=1) {
    char buf[100];
    _puts("childhandler() called from pid ");
    buf[fmt_ulong(buf,getpid())]=0;
    _puts(buf);
    _puts("\n");
    return;
  }
#endif
#ifdef UPDATE
if (doupdate) return;
#endif

  do {
    killed=waitpid(-1,&status,WNOHANG);
    handlekilled(killed,status);
  } while (killed && killed!=(pid_t)-1);
}

#ifdef CONSOLE
static volatile int dowinch=0;
#endif
static volatile int doint=0;
static volatile int doterm=0;

void sigchild(int sig) { (void)sig; }
#ifdef CONSOLE
void sigwinch(int sig) { (void)sig; dowinch=1; }
#endif
void sigint(int sig) { (void)sig; doint=1; }
void sigterm(int sig) { (void)sig; doterm=1; }

int main(int argc, char *argv[]) {
  /* Schritt 1: argv[1] als Service nehmen und starten */
  int count=0;
  int i;
  struct pollfd pfd;
  time_t last=time(0);
  int nfds=1;

#ifdef HISTORY
  for (i=0; i<HISTORY; ++i)
    history[i]=-1;
#endif

  Argv=argv;

  infd=open(MINITROOT "/in",O_RDWR);
  outfd=open(MINITROOT "/out",O_RDWR|O_NONBLOCK);

  if (getpid()==1) {
#ifdef CONSOLE
    int fd;
#endif
    i_am_init=1;
    reboot(0);
#ifdef CONSOLE
    if ((fd=open("/dev/console",O_RDWR|O_NOCTTY))) {
      ioctl(fd, KDSIGACCEPT, SIGWINCH);
      close(fd);
    } else
      ioctl(0, KDSIGACCEPT, SIGWINCH);
#endif
  }
/*  signal(SIGPWR,sighandler); don't know what to do about it */
/*  signal(SIGHUP,sighandler); ??? */
  {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction=0;
    sa.sa_flags=SA_RESTART | SA_NOCLDSTOP;
    sa.sa_handler=sigchild; sigaction(SIGCHLD,&sa,0);
    sa.sa_handler=sigint; sigaction(SIGINT,&sa,0);	/* ctrl-alt-del */
#ifdef CONSOLE
    sa.sa_handler=sigwinch; sigaction(SIGWINCH,&sa,0);	/* keyboard request */
#endif
  }

  if (infd<0 || outfd<0) {
    _puts("minit: could not open " MINITROOT "/in or " MINITROOT "/out\n");
    minitexit(1);
    nfds=0;
  } else
    pfd.fd=infd;
  pfd.events=POLLIN;

  fcntl(infd,F_SETFD,FD_CLOEXEC);
  fcntl(outfd,F_SETFD,FD_CLOEXEC);

#ifdef UPDATE
  {
   struct flock fl;
   fl.l_whence=SEEK_CUR;
   fl.l_start=0;
   fl.l_len=0;
   fl.l_pid=0;
   if ( (0 == fcntl(infd,F_GETLK,&fl)) &&
   		(fl.l_type != F_UNLCK )) doupdate=1;
  }

  if(!doupdate) {
#endif
  for (i=1; i<argc; i++) {
    circsweep();
    if (startservice(loadservice(argv[i]),0,-1)) count++;
   }
   circsweep();
   if (!count) startservice(loadservice("default"),0,-1);
#ifdef UPDATE
  }
#endif
  for (;;) {
    int i;
    char buf[1501];
    time_t now;
    if (doint) {
      doint=0;
      startservice(loadservice("sigint"),0,-1);
    }
    if (doterm) {
      doterm=0;
      startservice(loadservice("sigterm"),0,-1);
    }
#ifdef CONSOLE
    if (dowinch) {
      dowinch=0;
      startservice(loadservice("kbreq"),0,-1);
    }
#endif
    childhandler();
    now=time(0);
    if (now<last || now-last>30) {
      /* The system clock was reset.  Compensate. */
      long diff=last-now;
      int j;

      for (j=0; j<=maxprocess; ++j)
	root[j].startedat-=diff;
    }
    last=now;
    switch (poll(&pfd,nfds,5000)) {
    case -1:
      if (errno==EINTR) {
	childhandler();
	break;
      }
#ifdef CONSOLE
      opendevconsole();
#endif
      _puts("poll failed!\n");
      minitexit(1);
      /* what should we do if poll fails?! */
      break;
    case 1:
      i=read(infd,buf,1500);
      if (i>1) {
	pid_t pid;
	int idx,tmp;
	buf[i]=0;

/*	write(1,buf,str_len(buf)); write(1,"\n",1); */
#ifdef UPDATE
	if(!strcmp(buf,"update")) {
	  execve("/sbin/minit",argv, environ);
	}

	if (((buf[0]!='U') && buf[0]!='s') && ((idx=findservice(buf+1))<0)
	    && strcmp(buf,"d-"))
#else
	if (buf[0]!='s' && ((idx=findservice(buf+1))<0) && strcmp(buf,"d-") )
#endif
error:
	  write(outfd,"0",1);
	else {
	  switch (buf[0]) {
	  case 'p':
	    write(outfd,buf,fmt_ulong(buf,root[idx].pid));
	    break;
#ifdef UPDATE
	  case 'D':
	    doupdate=1;
	    write(outfd, &root[idx], sizeof(struct process));
	    break;
	  case 'U':
	    doupdate=1;
	    write(outfd,"1",1);
	    if (1==poll(&pfd,nfds,5000)) {
	      struct process tmp;
	      read(infd,&tmp,sizeof tmp);
	      tmp.name=strdup(buf+1);
	      addprocess(&tmp);
	    }
	    goto ok;
#endif
	  case 'r':
	    root[idx].respawn=0;
	    goto ok;
	  case 'R':
	    root[idx].respawn=1;
	    goto ok;
	  case 'C':
            if (root[idx].pid < 1) {
	      goto error;
            }
	    if (kill(root[idx].pid,0)) {	/* check if still active */
	      handlekilled(root[idx].pid,-1);	/* no!?! remove form active list */
	      goto error;
	    }
	    goto ok;
	  case 'P':
	    {
	      unsigned char *x=buf+str_len(buf)+1;
	      unsigned char c;
	      tmp=0;
	      while ((c=*x++-'0')<10) tmp=tmp*10+c;
	    }
	    if (tmp>0) {
	      if (kill(tmp,0)) goto error;
	      pid=tmp;
	    }
	    root[idx].pid=tmp;
	    goto ok;
	  case 's':
	    idx=loadservice(buf+1);
	    if (idx<0) goto error;
	    if (root[idx].pid<2) {
	      root[idx].pid=0;
	      circsweep();
	      idx=startservice(idx,0,-1);
	      if (idx==0) {
		write(outfd,"0",1);
		break;
	      }
	    }
ok:
	    write(outfd,"1",1);
	    break;
	  case 'u':
	    write(outfd,buf,fmt_ulong(buf,time(0)-root[idx].startedat));
	    break;
	  case 'd':
	    write(outfd,"1:",2);
	    {
	      int i;
#if 0
	      printf("looking for father==%d\n",idx);
#endif
	      for (i=0; i<=maxprocess; ++i) {
#if 0
		printf("pid of %d(%s) is %lu, father is %d\n",
		       i,root[i].name?root[i].name:"[none]",root[i].pid,root[i].father);
#endif
		if (root[i].father==idx)
		  write(outfd,root[i].name,str_len(root[i].name)+1);
	      }
	      write(outfd,"\0",2);
	    }
	    break;
	  }
	}
      } else {
	if (buf[0]=='h') {
#ifdef HISTORY
	  write(outfd,"1:",2);
	  {
	    int i;
	    for (i=0; i<HISTORY; ++i)
	      if (history[i]!=-1)
		write(outfd,root[history[i]].name,str_len(root[history[i]].name)+1);
	    write(outfd,"\0",2);
	  }
#else
	  write(outfd,"0",1);
#endif
	}
      }
      break;
    default:
#ifdef UPDATE
      doupdate=0;
#endif
      break;
    }
  }
}
