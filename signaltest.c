#include "types.h"
#include "stat.h"
#include "user.h"
int flag=0;
void userHandler(int signum){
  printf(1,"handler, got signal: %d\n", signum);
  flag=1;
}

void userTest(void){
  int pid;
  if((pid=fork())==0){
    struct sigaction act;
    act.sa_handler = &userHandler;
    act.sigmask = 0;
    sigaction(20, &act, null);
    while(flag == 0){
      printf(1,"wait..");
    }
    printf(1,"got signal: 20\n");
    exit();
  }
  printf(1,"sending sig 20 to %d\n",pid);
  kill(pid,20);
  int pid2 = wait();
  printf(1,"sig 20 recieved by pid %d\n",pid2);
}

void killTest(void){
  int pid;
  if((pid=fork())==0){
    for(;;);
  }
  printf(1,"sending sigkill to %d\n",pid);
  kill(pid,SIGKILL);
  int pid2 = wait();
  printf(1,"sigkill recieved by pid %d\n",pid2);
}

void stopContTest(void){
  int pid;
  if((pid=fork())==0){
    for(;;){
      printf(1,"doing work\n",pid);
    }
  }
  printf(1,"sending sigstop to %d\n",pid);
  kill(pid,SIGSTOP);

  int pid2 = wait();
  printf(1,"sigcont recieved by pid %d\n",pid2);
}

int
main(int argc, char *argv[])
{
  printf(1,"Signal Test Started:\n");
  //killTest();
  //stopContTest();
  userTest();

  printf(1,"Signal Test ended:\n"); 
  exit();
}

