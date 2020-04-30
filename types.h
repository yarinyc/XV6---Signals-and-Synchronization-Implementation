typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef uint pde_t;

#define null 0

#define SIG_DFL 0  /*default signal handling*/
#define SIG_IGN 1  /*ignore signal*/
#define SIGKILL 9
#define SIGSTOP 17
#define SIGCONT 19

struct sigaction{
  void (*sa_handler)(int);
  uint sigmask;
};

