#include "types.h"
#include "stat.h"
#include "user.h"

#define N	30

#define SIGUSER1 20
#define SIGUSER2 21
#define SIGUSER3 22

int flag=0;

int
fib(int n){ //some calculation
	if(n<=1)
		return n;
	else return fib(n-1)+fib(n-2);
}

void failHandler(int signum){	
	flag=1;
}

void printHandler(int signum){
  printf(1,"printHandler, got signal: %d\n", signum);
}

void bigHandler(int signum){
  kill(getpid(),SIGUSER2);
  for (int i = 0; i < 5; i++){
    printf(1,"handler fib: %d\n",fib(30));
  }
  struct sigaction act;
  act.sa_handler = (void*)SIG_IGN;
  act.sigmask = 0;
  sigaction(SIGUSER2,&act,null);
  flag=1;
}

void userHandler1(int signum){
  printf(1,"handler1, got signal: %d\n", signum);
  flag=1;
  exit();
}
void userHandler2(int signum){
  printf(1,"handler2, got signal: %d\n", signum);
  flag=1;
  exit();
}
void userHandler3(int signum){
  printf(1,"handler3, got signal: %d\n", signum);
  flag=1;
  exit();
}

void userHandlersTest(){
	printf(1,"userHandlersTest\n");
	for(int i=0; i<N; i++){
      int pid = fork();
      if(pid<0){
        printf(1,"fork failed\n");
        exit();
      }
      if(pid == 0){
        struct sigaction act;
        act.sigmask=0;
          if(i % 3 == 0){
            act.sa_handler = &userHandler1;
            sigaction(SIGUSER1,&act,null);
            kill(getpid(), SIGUSER1);
          }
          if(i % 3 == 1){
            act.sa_handler = &userHandler2;
            sigaction(SIGUSER2,&act,null);
            kill(getpid(), SIGUSER2);
          }
          if(i % 3 == 2){
            act.sa_handler = &userHandler3;
            sigaction(SIGUSER3,&act,null);
            kill(getpid(), SIGUSER3);
          }
      }
      else{
        wait();
      }
	}
	printf(1,"userHandlersTest Passed\n\n");
}

void stopContTest()
{
	printf(1,"stopContTest\n");
	int pid = fork();
	if(pid<0){
		printf(1,"fork failed\n");
		exit();
	}
	if(pid == 0){
		for(int i=0;i<15;i++){
			printf(1,"child fib calc: %d\n",fib(25));
		}		
		printf(1,"child done\n");
		exit();
	}
	else{
		kill(pid,SIGSTOP);
		printf(1,"parent fib calc: %d\n",fib(20));
		kill(pid,SIGCONT);
		wait();
	}
	printf(1,"stopContTest Passed\n\n");
}

void procMaskTest(){
	  printf(1,"procMaskTest \n");
	  struct sigaction act;

    int mask = 1 << SIGUSER3;
    sigprocmask(mask);
	  kill(getpid(),SIGUSER3); // if test exits here then failed
    printf(1,"fib calc: %d\n",fib(10));
    //return to default mask
    act.sa_handler = (void*)SIG_IGN;
    sigaction(SIGUSER3,&act,null);
    sigprocmask(0);

    mask = 1 << SIGUSER2;
    act.sigmask = mask;
    act.sa_handler = &bigHandler;
    sigaction(SIGUSER1,&act,null);
	  kill(getpid(),SIGUSER1);
    fib(25);
    flag = 0;
    printf(1,"procMaskTest Passed\n\n");
}

void signalDefaultTest(){
  struct sigaction act, old;
  act.sa_handler = &userHandler1;
  printf(1,"signalDefaultTest\n");
	sigaction(5,&act, &old);
  if(old.sa_handler != (void*)SIG_DFL){
  printf(1,"signalDefaultTest fail signal 5 wasnt default ");
  exit();
  }
  sigaction(5,&old, null); 
  printf(1,"signalDefaultTest Passed\n\n");
}

void killIgnoreTest(){
	printf(1,"killIgnoreTest \n");
  int p = fork();
  if (p == 0) {
    struct sigaction act;
    act.sa_handler = (void*)SIG_IGN;
    sigaction(SIGKILL, &act, null);//1 - child ignores SIGKILL
    fib(45);
    printf(1,"killIgnoreTest failed: Ignoring SIGKILL!\n");
    for(;;);
  } else{
    sleep(5);
    kill(p,SIGKILL);
    wait();
    printf(1,"killIgnoreTest Passed\n\n");
  }
}

int
main(int argc, char *argv[])
{
  printf(1,"Signal Test Started:\n\n");
  //userHandlersTest();
  stopContTest();
  // procMaskTest();
  // signalDefaultTest();
  // killIgnoreTest();
  printf(1,"Signal Test ended:\n"); 
  exit();
}

