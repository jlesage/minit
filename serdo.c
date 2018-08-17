#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errmsg.h>
#include <sys/wait.h>
#include <str.h>
#include <byte.h>

extern char **environ;

#define MAXENV 256
char* envp[MAXENV+2];
int envc;

int continueonerror;

int envset(char* s) {
  int i,l;
  if (s[l=str_chr(s,'=')]!='=') return -1;
  ++l;
  for (i=0; i<envc; ++i)
    if (byte_equal(envp[i],l,s)) {
      envp[i]=s;
      return 0;
    }
  if (envc<MAXENV) {
    envp[envc]=s;
    envp[++envc]=0;
    return 0;
  }
  return -1;
}

int spawn(char** argv, int last) {
  int i;
  if (str_equal(argv[0],"cd")) {
    if (chdir(argv[1])==-1) {
      carpsys("chdir failed");
      return -1;
    }
    return 0;
  } else if (str_equal(argv[0],"export")) {
    for (i=1; argv[i]; ++i) envset(argv[i]);
    return 0;
  }
  if (!last) {
    if ((i=fork())==-1) diesys(1,"cannot fork");
  } else i=0;
  if (!i) {
    /* child */
    environ=envp;
    _exit(execvp(argv[0],argv));
  }
  if (waitpid(i,&i,0)==-1) diesys(1,"waitpid failed");
  if (!WIFEXITED(i))
    return -1;
  return WEXITSTATUS(i);
}

int run(char* s,int last) {
  int i,spaces;
  char** argv,**next;;
  for (i=spaces=0; s[i]; ++i) if (s[i]==' ') ++spaces;
  next=argv=alloca((spaces+1)*sizeof(char*));
  while (*s) {
    while (*s && isspace(*s)) ++s;
    if (*s=='"') {
      ++s;
      *next=s;
      while (*s && s[-1] != '\\' && *s != '"') ++s;
      if (!*s) {
	--*next;
	break;
      }
      *s=0;
      ++s;
    } else if (*s=='\'') {
      ++s;
      *next=s;
      while (*s && s[-1] != '\\' && *s != '\'') ++s;
      if (!*s) {
	--*next;
	break;
      }
      *s=0;
      ++s;
    } else {
      *next=s;
      while (*s && *s!=' ' && *s!='\t') ++s;
      if (!*s) break;
      *s=0;
      ++s;
    }
    ++next;
  }
  *++next=0;

  return spawn(argv,last);
}

int execute(char* s) {
  char* start;
  int r;
  r=0;
  while (*s) {
    int last;
    while (isspace(*s)) ++s;
    if (*s == '#') {
      while (*s && *s != '\n') ++s;
      continue;
    }
    start=s;

    while (*s && *s != '\n') ++s;
    if (*s) {
      char* tmp;
      *s=0;
      ++s;
      for (tmp=s; *tmp; ++tmp)
	if (!isspace(*tmp) && *tmp=='#') {
	  for (tmp=s+1; *tmp && *tmp!='\n'; ++tmp) ;
	} else break;
      last=(*tmp==0);
    } else
      last=1;
    r=run(start,last);
    if (r!=0 && !continueonerror)
      break;
  }
  return r;
}

int batch(char* s) {
  struct stat ss;
  int fd=open(s,O_RDONLY);
  char* map;
  if (fd==-1) diesys(1,"could not open ",s);
  if (fstat(fd,&ss)==-1) diesys(1,"could not stat ",s);
  if (ss.st_size>32768) die(1,"file ",s," is too large");
  map=alloca(ss.st_size+1);
  if (read(fd,map,ss.st_size)!=(long)ss.st_size) diesys(1,"read error");
  map[ss.st_size]=0;
  close(fd);

  return execute(map);
}

int main(int argc,char* argv[],char* env[]) {
  int r;
  (void)argc;
  if (argc<2) die(1,"usage: serdo [-c] filename");
  errmsg_iam("serdo");
  for (envc=0; envc<MAXENV && env[envc]; ++envc) envp[envc]=env[envc];
  envp[envc]=0;
  if (str_equal(argv[1],"-c")) {
    continueonerror=1;
    ++argv;
  }
  while (*++argv) {
    if ((r=batch(*argv)))
      return r;
  }
  return 0;
}
