#include <fcntl.h>
#include <unistd.h>

void opendevconsole() {
  int fd;
  if ((fd=open("/dev/console",O_RDWR|O_NOCTTY))>=0) {
    dup2(fd,0);
    dup2(fd,1);
    dup2(fd,2);
    if (fd>2) close(fd);
  }
}
