#include "types.h"
#include "stat.h"
#include "user.h"

#define N	60

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
    fib(30);
    printf(1,"bigHandler...\n");
  }
  struct sigaction act;
  act.sa_handler = (void*)SIG_IGN;
  act.sigmask = 0;
  sigaction(SIGUSER2,&act,null);
  flag=1;
}

void userHandler1(int signum){
  flag=1;
  exit();
}
void userHandler2(int signum){
  flag=1;
  exit();
}
void userHandler3(int signum){
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
            printf(1,"(fork number: %d) \n",i);
            kill(getpid(), SIGUSER1);
          }
          if(i % 3 == 1){
            act.sa_handler = &userHandler2;
            sigaction(SIGUSER2,&act,null);
            printf(1,"(fork number: %d) \n",i);
            kill(getpid(), SIGUSER2);
          }
          if(i % 3 == 2){
            act.sa_handler = &userHandler3;
            sigaction(SIGUSER3,&act,null);
            printf(1,"(fork number: %d) \n",i);
            kill(getpid(), SIGUSER3);
          }
          for(;;){
            //loops for ever until recieving a signal
          }
      }
      else{
        wait();
      }
	}
	printf(1,"userHandlersTest Passed\n\n");
}

void concurrentUserHandlersTest(){
	printf(1,"concurrentUserHandlersTest\n");
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
        for(;;){
          //loops for ever until recieving a signal
        }
      }
	}
  for(int i=0; i<N; i++){
    wait();
  }
	printf(1,"concurrentUserHandlersTest Passed\n\n");
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
			fib(25);
		}		
		printf(1,"child done\n");
		exit();
	}
	else{
		kill(pid,SIGSTOP);
    sleep(100);
		kill(pid,SIGCONT);
		wait();
	}
	printf(1,"stopContTest Passed\n\n");
}

void procMaskTest(){
	  printf(1,"procMaskTest \n");
	  struct sigaction act;
    //part 1:
    int mask = 1 << SIGUSER3;
    sigprocmask(mask);
	  kill(getpid(),SIGUSER3); // if test exits here then failed
    fib(25);
    //return to default mask
    act.sa_handler = (void*)SIG_IGN;
    sigaction(SIGUSER3,&act,null);
    sigprocmask(0);
    //part 2:
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
    printf(1,"signalDefaultTest fail signal 5 wasn't default ");
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

void inheritTest(){
  printf(1,"inheritTest\n");
  struct sigaction act,_act,act1,act2,act3;
  act1.sa_handler = &userHandler1;
  act2.sa_handler = &userHandler2;
  act3.sa_handler = &userHandler3;

  sigprocmask(((1 << 13) | (1 << 14) | (1 << 15)));
  //Changing signal handler of signals 13,14,15
  sigaction(13, &act1, null);
  sigaction(14, &act2, null);
  sigaction(15, &act3, null);

  int pid = fork();

  if(pid < 0){
    printf(1,"inheritTest failed in fork\n");
  }
  if (pid == 0) {
      if (sigprocmask(0) != ((1 << 13) | (1 << 14) | (1 << 15))){
          printf(1,"inheritTest failed : Child didnt inherit mask array\n");
          exit();
      }
      _act.sa_handler = (void*)SIG_DFL;
      sigaction(13, &_act, &act);
      if(act.sa_handler != &userHandler1){
          printf(1,"inheritTest failed : Child didnt inherit signal handler\n");
          exit();
      }
      sigaction(14, &_act, &act);
      if(act.sa_handler != &userHandler2){
          printf(1,"inheritTest failed : Child didnt inherit signal handler\n");
          exit();
      }
      sigaction(15, &_act, &act);
      if(act.sa_handler != &userHandler3){
          printf(1,"inheritTest failed : Child didnt inherit signal handler\n");
          exit();
      }
      for(;;);
  }
  else {
      sleep(20);
      kill(pid,15);
      wait();
  }

  printf(1,"inheritTest Passed\n\n");
}

int
main(int argc, char *argv[])
{
  printf(1,"Signal Test Started:\n\n");
  userHandlersTest();
  concurrentUserHandlersTest();
  stopContTest();
  procMaskTest();
  signalDefaultTest();
  killIgnoreTest();
  inheritTest();
  printf(1,"Signal Test Ended Succesfully!\n"); 
  exit();
}

