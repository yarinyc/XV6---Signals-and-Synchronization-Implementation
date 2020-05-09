#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

int
fib(int n){ //some calculation
	if(n<=1)
		return n;
	else return fib(n-1)+fib(n-2);
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}


int 
allocpid(void) 
{
  int pid;
  do{
    pid = nextpid;
  } while(!cas(&nextpid, pid, pid+1));
  return pid;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.

static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;
  pushcli();
  do {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == UNUSED)
        break;
      if (p == &ptable.proc[NPROC-1]) {
        popcli();
        return 0; // ptable is full
      }
    }
  } while (!cas(&p->state, UNUSED, EMBRYO));
  popcli();

  //continue with allocation:
  p->pid = allocpid();

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  //2.1.2
  for(int i=0; i<32; i++){
    p->signalHandlers[i].sa_handler = (void*)SIG_DFL;
  }
  p->pendingSignals = 0;
  p->block_user_signals = 0;
  p->suspend = 0;
  p->wakeup = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");

  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S
  
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  pushcli();
  if (!cas(&p->state, EMBRYO, RUNNABLE))
    panic("userinit: cas failed");
  popcli();
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  //2.1.2 (2)
  np->signalMask = curproc->signalMask; //copy signal mask from parent
  for(int i=0; i<32 ; i++){ //copy all signal handlers from parent
    np->signalHandlers[i].sa_handler = curproc->signalHandlers[i].sa_handler;
    np->signalHandlers[i].sigmask = curproc->signalHandlers[i].sigmask;
  }
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  np->pendingSignals = 0; // no pending siganls on creation


  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;
  if(!cas(&np->state, EMBRYO, RUNNABLE))
    panic("fork: cas failed");

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  //acquire(&ptable.lock);
  pushcli();
  if(!cas(&curproc->state, RUNNING, -ZOMBIE))
    panic("exit: cas no.1 failed");

  // Parent might be sleeping in wait().
  //wakeup1(curproc->parent); ????

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  //Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  //acquire(&ptable.lock);
  pushcli();
  for(;;){

    if (!cas(&(curproc->state), RUNNING,-SLEEPING))
        panic("wait: cas failed");

    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      while(p->state == -ZOMBIE){
        cprintf("still -ZOMBIE\n");
        //busy-wait
      }
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->chan=0;

        p->pendingSignals = 0; // added may 2nd
        p->signalMask = 0;

        //p->state = UNUSED;
        if (!(cas(&p->state, ZOMBIE, UNUSED)))
          panic("wait: cas failed -> ZOMBIE to UNUSED");

        if (!(cas(&curproc->state,-SLEEPING, RUNNING)))
          panic("wait: cas failed -> -SLEEPING to RUNNING");
        //release(&ptable.lock);
        popcli();
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      //release(&ptable.lock);
      if(!(cas(&curproc->state, -SLEEPING, RUNNING))){
        panic("wait: cas failed -> -SLEEPING to RUNNING 2");
      }
      popcli();
      return -1;
    }

    curproc->chan = (void*)curproc;
    //cprintf("pid: %d sleeping on %x\n",curproc->pid, curproc->chan);
    sched();
    curproc->chan = 0;
    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    //acquire(&ptable.lock);
    pushcli();
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(!cas(&p->state, RUNNABLE, RUNNING)){
        continue;
      }
      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      swtch(&(c->scheduler), p->context);
      switchkvm();

      if(cas(&p->state,-ZOMBIE, ZOMBIE)){
        //cprintf("pid %d waking up channel %x\n", p->pid, p->parent);
        wakeup1(p->parent);//****
      }

      if(cas(&p->state, -SLEEPING, SLEEPING)){
        if(p->killed == 1){
          if(!cas(&p->state, SLEEPING,RUNNABLE)){
            panic("scheduler: failed(killed=1) SLEEPING to RUNNABLE");
          }
        }
        else if(p->wakeup == 1){
          p->wakeup = 0;
          if(!cas(&p->state, SLEEPING,RUNNABLE)){
            panic("scheduler: failed(wakeup=1) SLEEPING to RUNNABLE");
          }
        }
      }
      cas(&p->state, -RUNNABLE, RUNNABLE);


      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    //release(&ptable.lock);
    popcli();

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  // if(!holding(&ptable.lock))
  //   panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  pushcli();
  if (!cas(&myproc()->state, RUNNING, -RUNNABLE))
    panic("yield: cas failed");
  sched();
  popcli();
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  // release(&ptable.lock);
  popcli();
  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Go to sleep.
  p->chan = chan;
  pushcli();
  if (!cas(&myproc()->state, RUNNING, -SLEEPING))
    panic("sleep: cas failed");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.

  release(lk);


  // Go to sleep.
  //p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;
  popcli();

  // Reacquire original lock.
  acquire(lk);

}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if((p->state == SLEEPING || p->state == -SLEEPING) && p->chan == chan){
      if(p->state == -SLEEPING){
        p->wakeup = 1;
        //cprintf("still -SLEEPING\n");
        //busy-wait for -SLEEPING to become SLEEPING
      }
      if(p->state != SLEEPING){
        continue;
      }
      p->wakeup = 0;
      if(!cas(&p->state, SLEEPING, RUNNABLE))
        panic("wakeup1: failed cas SLEEPING to RUNNABLE");
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  pushcli();
  wakeup1(chan);
  popcli();
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
// 2.2.1 changes:
int
kill(int pid, int signum)
{
  struct proc *p;
  if(signum<0 || signum>31){
    return -1;
  }
  // acquire(&ptable.lock);
  pushcli();
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      uint bitwise = 1<<signum;
      uint pending;
      do{
        pending = p->pendingSignals;
      }while(!(cas(&p->pendingSignals, pending, (pending|bitwise))));
      if(signum == SIGKILL && (p->state == -SLEEPING || p->state == SLEEPING)){
        while(p->state == -SLEEPING){
          cprintf("kill: still -SLEEPING\n");
          //busy-wait for -SLEEPING to become SLEEPING
        }
        if(!cas(&p->state, SLEEPING, RUNNABLE))
          panic("kill: failed cas SLEEPING to RUNNABLE");
      }
      // release(&ptable.lock);
      popcli();
      return 0;
    }
  }
  // release(&ptable.lock);
  popcli();
  return -1;
}
 
//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = p->state==-ZOMBIE ? "-ZOMBIE": p->state==-SLEEPING ? "-SLEEPING":p->state==-RUNNABLE ? "-RUNNABLE":"???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

uint
sigprocmask(uint sigmask){
  struct proc *p = myproc();
  uint oldmask = p->signalMask;
  p->signalMask = sigmask;
  return oldmask;
}

int
sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  struct proc *p = myproc();
  if(signum>31 || signum <0 || signum == SIGKILL || signum == SIGSTOP){
    return -1;
  }
  if(oldact != null){ //backup old handler
    struct sigaction* h = &p->signalHandlers[signum];
    if(h->sa_handler == (void*)SIG_DFL || h->sa_handler == (void*)SIG_IGN || h->sa_handler == (void*)SIGKILL || h->sa_handler == (void*)SIGSTOP || h->sa_handler == (void*)SIGCONT){
      oldact->sa_handler = p->signalHandlers[signum].sa_handler;
      oldact->sigmask = myproc()->signalMask;
    } else {
      oldact->sa_handler = p->signalHandlers[signum].sa_handler;
      oldact->sigmask = p->signalHandlers[signum].sigmask;
    }
  }
  p->signalHandlers[signum].sa_handler = act->sa_handler;
  p->signalHandlers[signum].sigmask = act->sigmask;
  return 0;
}

//restore the process to its original workflow, when returning from user space
void 
sigret(void){
  struct proc* p = myproc();
  memmove(p->tf, &(p->userTrapBackup), sizeof(struct trapframe));
  p->signalMask = p->signalMask_backup;
  p->block_user_signals = 0;
  return;
}
// signal handlers for all the default behaviour:
void 
sigkill(int signum){
  popcli();
  struct proc *p = myproc();
  p->killed = 1;
  uint bitwise = 1<<signum;
  uint pending;
  do{
    pending = p->pendingSignals;
  }while(!(cas(&p->pendingSignals, pending, (pending & (~bitwise)) )));
  exit(); // stop the process from running
}

void
sigcont(int signum){
  struct proc *p = myproc();
  uint bitwise = 1<<signum;
  uint pending;
  do{
    pending = p->pendingSignals;
  }while(!(cas(&p->pendingSignals, pending, (pending & (~bitwise)) )));
  p->suspend = 0;
}

void 
sigstop(int signum){
  struct proc *p = myproc();
  uint bitwise = 1<<signum;
  uint pending; 
  do{
    pending = p->pendingSignals;
  }while(!(cas(&p->pendingSignals, pending, (pending & (~bitwise)) ))); 
  p->suspend = 1;
}

extern void check_signals(void);
extern void* call_sigret;
extern void* call_sigret_end; 

void
check_signals(void){
  struct proc *p = myproc();
  struct sigaction *act;
  uint pending;
  if (p==0){ // to avoid accessing proc before init
    return;
  }

  //if process is suspended and did not recieve SIGCONT yield() immediately
  while(p->suspend == 1){

    if((p->pendingSignals & (1<<SIGCONT)) && !(p->pendingSignals & (1<<SIGKILL))){
      break;
    }
    yield();
  }
  pushcli();
  uint call_sigret_size = (uint)&call_sigret_end - (uint)&call_sigret;
  for(int signum=0; signum<32; signum++){ //go through all pending signals and run their sa_handler()
    int shift = 1 << signum;
    if(( (p->pendingSignals & shift) != 0 ) && (signum==SIGSTOP || signum==SIGKILL || ((p->signalMask & shift) == 0)) ){ // if recieved signal and not blocked by mask 
      act = &p->signalHandlers[signum];
      if(act->sa_handler == (void*)SIG_DFL){ // do default behaviour
          switch (signum){
          case SIGKILL:
            sigkill(signum);
            break;
          case SIGSTOP:
            sigstop(signum);
            break;
          case SIGCONT:
            sigcont(signum);
            break;
          default:
            sigkill(signum);
            break;
          }
      }
      else if(act->sa_handler == (void*)SIG_IGN){
        uint bitwise = 1<<signum;
        do{
          pending = p->pendingSignals;
        }while(!(cas(&p->pendingSignals, pending, (pending & (~bitwise)) )));
      }
      else if(act->sa_handler == (void*)SIGKILL)
        sigkill(signum);
      else if(act->sa_handler == (void*)SIGSTOP)
        sigstop(signum);
      else if(act->sa_handler == (void*)SIGCONT)
        sigcont(signum);
      else if(p->block_user_signals == 0){ // user defined handler & no other user handler is currently running
        memmove(&(p->userTrapBackup), p->tf, sizeof(struct trapframe)); //backup the proc tf
        p->signalMask_backup = sigprocmask(act->sigmask); //backup the proc sigmask
        p->block_user_signals = 1; //block all non default signals
        p->tf->esp -= call_sigret_size; //save space for the code of call_sigret
        memmove((void*)p->tf->esp, &call_sigret, call_sigret_size); // copy the code of call_sigret to [esp]
        *((int*)(p->tf->esp - 4)) = signum; // push sig_handler's argument
        *((int*)(p->tf->esp - 8)) = p->tf->esp; // return address of sa_handler is to call_sigret
        p->tf->esp -= 8;
        p->tf->eip = (uint)act->sa_handler; // trapret will resume into  the user signal handler
        uint bitwise = 1<<signum;
        do{
          pending = p->pendingSignals;
        }while(!(cas(&p->pendingSignals, pending, (pending & (~bitwise)) )));
        popcli();
        return; //return to trapret
      }
    }
  }
  popcli();
}



