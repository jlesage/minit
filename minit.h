#ifndef MINITROOT
#define MINITROOT "/etc/minit"
#endif

#ifndef NOVARS
static struct process {
  char *name;
/*  char **argv; */
  pid_t pid;
  char respawn;
  char circular;
  time_t startedat;
  int father;	/* the service who started me or -1 if I was started directly */
  int __stdin,__stdout;
  int logservice;
} *root;

static int infd,outfd;
static int maxprocess=-1;
static int processalloc;
#endif
