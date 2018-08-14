/*

killall5 -- send a signal to all processes.

killall5 is the SystemV killall command. It sends a signal
to all processes except init(PID 1) and the processes in its
own  session, so  it  won't kill the shell that is running the
script it was called from. Its primary (only) use is in the rc
scripts found in the /etc/init.d directory.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version
2 of the License, or (at your option) any later version.

*/

#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define USAGE "Usage: killall5 SIGNAL\n"
#define NOPROC "No processes found - /proc not mounted?\n"

int main(int argc, char **argv)
{
 struct dirent *dir;
 DIR *dirstream;
 register pid_t pid, sid, mypid, mysid;
 int signal=-1;
 unsigned int sig_sent=0;

 if (argc == 2) {
  if (argv[1][0] == '-') argv[1]++;
  signal=atoi(argv[1]);
 }

 if ( (signal < 1) || ( signal > 31) ) { write(2,USAGE,sizeof USAGE - 1); return 1; }


 kill(-1,SIGSTOP);

 if ( (dirstream=opendir("/proc"))) {

 mypid=getpid();
 mysid=getsid(0);

   while ( (dir=readdir(dirstream))){
      pid=atoi(dir->d_name);

       if (pid > 1 ){ 
        sig_sent=1;
        sid=getsid(pid);
         if ( (pid != mypid) &&
           ( sid !=mysid)) kill(pid,signal);
       }
   }
 }

 kill(-1,SIGCONT);
 if (!sig_sent) { write(2,NOPROC, sizeof NOPROC -1); return 1; }

return 0;
}

