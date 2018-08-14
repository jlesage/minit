#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define USAGE "write_proc <value> <path_to_proc_file>\n"

int main(int argc,char*argv[]) {
  int fd;
  if (argc!=3) goto usage;
  if ((fd=open(argv[2],O_WRONLY))==-1) goto usage;
  write(fd,argv[1],strlen(argv[1]));
  close(fd);
  return 0;
usage:
  write(2,USAGE,strlen(USAGE));
  return 1;
}
