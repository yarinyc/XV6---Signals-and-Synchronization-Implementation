.globl call_sigret
.globl call_sigret_end

call_sigret:
  movl $24, %eax  # call system call sigret to jump back to kernel mode
  int $64
call_sigret_end: