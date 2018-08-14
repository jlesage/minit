/*
 * Notes:
 * - uncomment `#define ALLOW_SUID' below if you want users other than
 *   root to be able to shut down the system. 
 * - after compiling, install under /sbin/shutdown with chgrp adm and 
 *   chmod 4750 for SUID root, or 0700 for root only
 * - uncomment `#define USE_MINIT' below if you want to use shutdown
 *   with minit. If defined, shutdown will try to bring down the services 
 *   halt (for -h or -o) or reboot (-r) before killing all other processes.
 *   Please make sure that you have a depends in reboot and halt that 
 *   will bring down all respawning services. A respawning service during
 *   shutdown might cause you to wait for a fsck during the next boot
 * - If you do not use minit shutdown will bring your system down similar
 *   to SysV-Inits shutdown with -n
 *
 * TODO: 
 * - add a function for wall-messages
 * - cleanup
 */

#include <sys/types.h>
#include <sys/reboot.h>
#include <signal.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "str.h"

#ifdef __dietlibc__
#include <write12.h>
#else
#include <unistd.h>
static inline int __write1(const char*s) { return write(1,s,str_len(s)); }
static inline int __write2(const char*s) { return write(2,s,str_len(s)); }
#endif

#define ALLOW_SUID
#define USE_MINIT

#ifdef USE_MINIT
#define NOVARS
#include "minit.h"
#endif

extern void opendevconsole();

extern char **environ;
extern int openreadclose(char *fn, char **buf, unsigned long *len);
extern char **split(char *buf,int c,int *len,int plus,int ofs);
extern char *optarg;

void wall(char *buf) {
  __write2(buf);
}

int exec_cmd(char *cmd, ...) {
  char *argv[10];
  va_list arguments;
  pid_t pid;
  int i;

  va_start(arguments, cmd);
  for (i=0;i<9 && (argv[i] = va_arg(arguments, char *)) != NULL; i++);
  argv[i] = NULL;
  va_end(arguments);
  pid = fork();
  if (pid < 0) return -1;
  if (pid > 0) {
    wait(NULL);
  } else {
    execve(cmd, argv, environ);
    //perror("execvp failed");
    exit(0);
  }
  return 0;
}

#ifdef USE_MINIT

static int infd, outfd;
static char buf[1500];

int minit_serviceDown(char *service) {
  char *s=0;
  unsigned long len;
  pid_t pid=0;
  
  if (!service || !*service) return 0;
  if (chdir(MINITROOT) || chdir(service)) return -1;
  
  if (!openreadclose("depends", &s, &len)) {
    char **deps;
    int depc, i;
    deps=split(s, '\n', &depc, 0, 0);
    for (i=0; i<depc; i++) {
      if (deps[i][0] == '#') continue;
      minit_serviceDown(deps[i]);
    }
  }
  
  // get the pid
  buf[0]='p';
  strncpy(buf+1, service, 1400);
  write(infd, buf, str_len(buf));
  len=read(outfd, buf, 1500);
  if (len != 0) {
    buf[len]=0;
    pid = atoi(buf);
  }

  if (strcmp("reboot",service) && strcmp("halt",service) && pid > 1) {
    int i;
    __write2("\t--> "); __write2(service);
    buf[0]='r'; // we want to disable respawning first
    strncpy(buf+1, service, 1400);
    buf[1400]=0;
    write(infd, buf, str_len(buf));
    read(outfd, buf, 1500);
    i=kill(pid, SIGTERM);
    if (i == 0) __write2("\t\tdone\n");
    else __write2("\t\tfailed\n");
  }
  return 0;
}

int minit_shutdown(int level) {
  int retval;

  __write2("Shutting down minit services: \n");
  infd=open(MINITROOT "/in", O_WRONLY);
  outfd=open(MINITROOT "/out", O_RDONLY);
  if (infd>=0) {
    while (lockf(infd, F_TLOCK, 1)) {
      __write2("could not acquire lock!\n");
      sleep(1);
    }  
  }

  retval=minit_serviceDown(level?"halt":"reboot");
  close(infd); close(outfd);
  return retval;
}
#endif

void printUsage() {
  __write2("usage: shutdown -[rhosmn] -[t secs]\n"
                "\t -r:        reboot after shutdown\n"
                "\t -h:        halt after shutdown\n"
		"\t -o:	       power-off after shutdown\n"
		"\t -s:	       single-user console after shutdown\n"
		"\t -m:	       only shutdown the minit-part\n"
                "\t -n:        do not shutdown services using minit\n"
      		"\t -t secs:   delay between SIGTERM and SIGKILL\n");
}

int main(int argc, char *const argv[]) {
  int c;
  int cfg_downlevel=2;
  /* 0: reboot
   * 1: halt
   * 2: power off
   */
  unsigned int cfg_delay = 3;
  int cfg_minitonly = 0;
  int cfg_sulogin = 0;

  #ifdef ALLOW_SUID
  setuid(geteuid());
  #endif
  if (getuid() != 0) {
	  __write2("you are not root, go away!\n");
	  return 1;
  }

  if (argc<2) {
    printUsage();
    return 0;
  }

  /* parse commandline options */
  while((c = getopt(argc, argv, "rhosmnt:")) != EOF) {
    switch(c) {
      case 'r': /* do we have to reboot... */
	      cfg_downlevel = 0;
	      break;
      case 'h': /* ...halt.. */
	      cfg_downlevel = 1;
	      break;
      case 's': /* rescue system */
	      cfg_sulogin = 1;
	      break;
      case 'm': /* exit after minit down */
	      cfg_minitonly = 1;
	      break;
      case 'o': /* ..or power off? */
	      cfg_downlevel = 2;
	      break;
      case 't': /* delay between SIGTERM and SIGKILL */
	      cfg_delay = atoi(optarg);
	      break;
      default:
	      printUsage();
	      return 1;
    }
  }
  
  opendevconsole();
  
  switch (cfg_downlevel) {
	  case 0:
		  wall("system is going down for reboot NOW\n");
		  break;
	  case 1:
		  wall("system is going down for system halt NOW\n");
		  break;
	  case 2:
		  wall("system is going down for power-off NOW\n");
		  break;
  }

  /* 
   * catch some signals; 
   * getting killed after killing the controlling terminal wouldn't be 
   * such a great thing...
   */
  signal(SIGQUIT, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP,  SIG_IGN);
  signal(SIGTSTP, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);
 
  // real shutdown? then lets rock..
  #ifdef USE_MINIT
  minit_shutdown(cfg_downlevel);
  if (cfg_minitonly) return 0;
  #endif
  
  /* kill all processes still left */
  __write2("sending all processes the TERM signal...\n");
  kill(-1, SIGTERM);
  sleep(cfg_delay);

  __write2("sending all processes the KILL signal...\n");
  kill(-1, SIGKILL);

  if (cfg_sulogin) {
    char* suargs[]={"sulogin",0};
    execve("/sbin/sulogin", suargs, 0);
    __write2("execve() /sbin/sulogin failed\n");
    return 1;
  }

  /* sync buffers */
  sync();

  exec_cmd("/sbin/swapoff", "swapoff", "-a", (char *) 0);
  exec_cmd("/bin/umount", "umount", "-a", (char *) 0);
  exec_cmd("/bin/mount", "mount", "-o", "remount,ro", "/", (char *) 0);

  sync();

  /* and finally reboot, halt or power-off the system */ 
  if (cfg_downlevel == 0) {
    reboot(RB_AUTOBOOT);
  } else if (cfg_downlevel == 1) {
    reboot(RB_HALT_SYSTEM);
  } else {
    reboot(RB_POWER_OFF);
  }
  return 0;
}
