#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "str.h"
#include "buffer.h"

#include "minit.h"

#define USAGE "Usage: minit-update [ -v [ -u ] ]\n"
#define BUFLEN 1500

/*
 increases file size by almost 4k
#define WITH_STRERROR */

static char buf[BUFLEN+1];

static unsigned int verbose;
static int do_update;

int openreadclose(char *fn, char **buf, unsigned long *len);
char **split(char *buf,int c,int *len,int plus,int ofs);

void feed_struct_to_minit(struct process *data);

ssize_t read_outfd(void *buf, size_t count);

void addprocess(struct process *p);


void die(const char *msg) {
 buffer_putsflush(buffer_2, msg);
 _exit(111);
}


void buffer_putsnlflush(buffer *b, const char *msg) {
 buffer_puts(b, msg);
 buffer_putflush(b, "\n",1);
}


#ifdef WITH_STRERROR
void buffer_puts_strerror(const char *msg) {
 buffer_puts(buffer_2, "minit-update: ");
 buffer_puts(buffer_2, msg);
 buffer_putsnlflush(buffer_2, strerror(errno));
}
#else
#define buffer_puts_strerror(a) buffer_putsflush(buffer_2, a)
#endif 


void *xmalloc(size_t size) {
 void *ptr=malloc(size);
 if (!ptr) die("malloc() failed\n");
 return ptr;
}


void copywrite (const char *service) {
  strncpy(buf+1,service,BUFLEN);
  buf[BUFLEN]=0;
  write(infd,buf,str_len(buf));
}


int read_reply_from_minit(void) {
  if (read_outfd(buf,BUFLEN)==1) {
     if (buf[0]=='1') return 1;
     if (buf[0]=='0') buffer_puts(buffer_2,"expected '1' from minit, got '0' - minit too old?\n");
  }
    /* XXX: Uuuh. Should this be checked? 
     else buffer_putsflush(buffer_2, "minit response not understood\n");
    */
return 0;   
}


void find_service(int subdir, char *name, char *parent) {
 struct stat statbuf;
 char *service=0;
 DIR *dirstream=0;
 struct dirent *dir;

 if (chdir(name)) return;

 if (parent) {
  service=xmalloc(str_len(parent) + str_len(name) + 2 );
  strcpy(service, parent);
  strcat(service, "/");
  strcat(service, name);
 } else {
    if (subdir) {
     service=xmalloc(str_len(name)+1);
     strcpy(service, name);
    }
 }
#if 0 
 buffer_putsnlflush(buffer_1,service);
#endif

 if (service) { /* request and read a "struct process" from minit */
  struct process tmp;
#if 0
  int j;
  for (j=0; j<=maxprocess; ++j) { /* skip duplicates */
   if(!strcmp(root[j].name,service)) return 0;
  }
#endif

  if (verbose) {
    buffer_puts(buffer_1, "minit-update: status for ");
    buffer_puts(buffer_1, service);
  }

  buf[0]='D';
  copywrite(service);
  
  switch (read_outfd(&tmp,sizeof(tmp))) {
   case sizeof(tmp): 
     tmp.name=strdup(service);
     addprocess(&tmp);
     if (verbose) buffer_puts(buffer_1, " saved.\n");
     break;
   case 1:
     if (verbose) buffer_puts(buffer_1, " failed - minit has no information on this service\n");
   #if 0  
    break;
   default:
     buffer_puts(buffer_1, " failed - read incomplete structure!\n");
   #endif 
  }
 }
 
dirstream=opendir(".");
if (!dirstream) goto ret;

while ( (dir=readdir(dirstream))){
  if (dir->d_name[0]!='.') {
      if(!lstat(dir->d_name, &statbuf)) {
        if (S_ISDIR(statbuf.st_mode)) {
            find_service(1, dir->d_name, service);
#if 0   
        } else {
           buffer_putsnlflush(buffer_1,dir->d_name);
#endif
        }
      } else {
        buffer_puts(buffer_2, dir->d_name);
        buffer_puts(buffer_2, ": cannot stat\n");
        buffer_puts_strerror("lstat() failed: ");
      }
  }
} /* while */
 
closedir(dirstream);

ret:
if (service) free(service);
chdir(MINITROOT);
if (parent) chdir(parent);
buffer_flush(buffer_1);
} 


int main(int argc, char **argv) {
 int i;

 if (argc < 2) die(USAGE);

 while (argc>1) {
   argc--;
   if (argv[argc][0]=='-') {
     switch(argv[argc][1]) {
       case 'v': verbose++; break;
       case 'u': do_update=1; break;
       default:
        buffer_puts(buffer_2,"minit-update: Unknown Option: ");          
	buffer_putsnlflush(buffer_2,argv[argc]);
     } 
   } else die(USAGE);
 }

 infd=open(MINITROOT "/in",O_WRONLY);
 outfd=open(MINITROOT "/out",O_RDONLY);
 
 if (infd<0 || outfd<0) die("could not open " MINITROOT "/in or " MINITROOT "/out\n");

 while (lockf(infd,F_TLOCK,1)) {
    buffer_puts_strerror("could not acquire lock: ");
    sleep(1);
 }

 find_service(0,MINITROOT,0);

 if (maxprocess == -1) 
    die("Could not extract running services from minit\n");
 
 if (verbose) buffer_putsflush(buffer_1, "minit-update: telling minit to execve itself\n");
 
 if (!do_update) {
   buffer_putsflush(buffer_2, "Test mode: No update done.\n");
   return 0;
 }

 write(infd,"update",6);
 sleep(1);

 for (i=0; i<=maxprocess; i++) {
    if (verbose) {
      buffer_puts(buffer_1, "minit-update: restoring status for ");
      buffer_putsnlflush(buffer_1, root[i].name);
    }  
 
    buf[0]='U';
    copywrite(root[i].name);
   
    read_reply_from_minit();
   
    write(infd,&root[i],sizeof (struct process));
   
    if (read_reply_from_minit() && verbose) {
      buffer_puts(buffer_1, "minit-update: restored service ");
      buffer_putsnlflush(buffer_1, root[i].name);
    }
        
 } /* for() */

return 0; 
}


ssize_t read_outfd(void *buf, size_t count) {
 ssize_t br=read(outfd,buf,count);

 if (br<0) buffer_puts_strerror("Error reading from outfd: ");
return br;
}


void addprocess(struct process *p) {
  if (maxprocess+1>=processalloc) {
    struct process *fump;
    processalloc+=8;
    if ((fump=(struct process *)realloc(root,processalloc*sizeof(struct process)))==0) die("realloc() failed\n ");
    root=fump;
  }
  memmove(&root[++maxprocess],p,sizeof(struct process));
}
