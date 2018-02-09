#include "helpers.h"
#include "tracee.h"

#include "elfutils.h"

#ifndef __ARCH__
 #error "Please specify the architecture through the __ARCH__ preprocessor var"
#else
#if __ARCH__ == 32
 #define REG_IP		eip
 #define REG_SP		esp
 #define REG_RET	eax
 #define TRAP		0xCC
 #define TRAP_SZ	1
#elif __ARCH__ == 64
 #define REG_IP		rip
 #define REG_SP		rsp
 #define REG_RET	rax
 #define TRAP		0xCC
 #define TRAP_SZ	1
 #define MAIN_BP_OFFSET	11
#else
 #error "Unsupported architecture (__ARCH__ must be either 32 or 64)"
#endif
#endif

/* MAIN_BP_OFFSET is the offset to put the 'main' function breakpoint at */
/* (offset of 0 leads to a Segmentation Fault ...) */
#ifndef MAIN_BP_OFFSET
#if defined(__ARCH__) && __ARCH__ == 64
 #define MAIN_BP_OFFSET	11
#else
 #define MAIN_BP_OFFSET	10
#endif
#endif


#ifndef print_fail
 #define print_fail(fmt, ...) do {				\
   fprintf(stderr, "Fatal error: " fmt ".\n", ##__VA_ARGS__);	\
   exit(-1);							\
 } while (0)
#endif

struct bp_node {
  void *		addr;
  long			instr_bckup;
  const char *		name;
  int			wp;
  struct arity *	function_arity;
  bp_list		next;
};

static int tracee_wait_stop (tracee_t * tracee)
{
  int status;
  if (waitpid(tracee->pid, &status, 0) < 0)
    failwith("waitpid");
  /* if (status) ... */
  return status;
}

static void tracee_resume (tracee_t * tracee, int signal)
{
  int ret = ptrace(PTRACE_CONT, tracee->pid, 0, signal);
  if (ret < 0)
    failwith("PTRACE_CONT");
}

tracee_t * tracee_attach (pid_t pid) {
  print_fail("Not implemented.\n");
  return NULL;
}

static void tracee_add_breakpoint (tracee_t * tracee,
                                   void * target_addr,
                                   const char * bp_name,
                                   struct arity * function_arity,
                                   int is_watchpoint);

tracee_t * tracee_summon (char * const args[])
{
  pid_t pid;
  if ((pid = fork()) == -1) {
    failwith("fork");
  }
  if (pid == 0) {
  /* tracee */
    if(ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0)
      failwith("Failed to PTRACE_TRACEME");
    execvp(args[0], args);
    failwith("Error when trying to execute '%s'", *args);
  }
  /* tracer */
  tracee_t * tracee = malloc(sizeof(tracee_t));
  if (!tracee) failwith("tracee_summon: malloc");
  tracee->name = (const char *) strdup(args[0]);
  if (!tracee->name) failwith("tracee_summon: malloc");
  tracee->pid = pid;
  tracee->bps = NULL;
  printd("Child '%s' running under PID %d\n", tracee->name, pid);
  tracee_wait_stop(tracee);
  ptrace(PTRACE_SETOPTIONS, tracee->pid, 0, PTRACE_O_TRACEEXIT);
  const char * raw_binary;
  int fd = open_raw_binary(args[0], &raw_binary);
  void * main_addr = (void *) lookup_symbol(raw_binary, "main");
  if (main_addr)
    tracee_add_breakpoint(tracee, main_addr + MAIN_BP_OFFSET, "main", NULL, 0);
  else
    printd("Couldn't find main.\n");
  close_raw_binary(fd, raw_binary);
  tracee_resume(tracee, 0);
  tracee_wait_stop(tracee);
  return tracee;
}

static long tracee_write_byte(tracee_t * tracee, void * addr, int byte)
{
  long ret = ptrace(PTRACE_PEEKTEXT, tracee->pid, addr, NULL);
  long tmp =
    ptrace(PTRACE_POKETEXT, tracee->pid, addr, (byte & 0xff) | (ret & ~0xff));
  if (tmp < 0) failwith("PTRACE_POKETEXT");
  return ret;
}

static void tracee_add_breakpoint (tracee_t * tracee,
                                   void * target_addr,
                                   const char * bp_name,
                                   struct arity * function_arity,
                                   int is_watchpoint)
{
  printd("Adding breakpoint at %p.\n", target_addr);
  struct bp_node * new_bp = malloc(sizeof(struct bp_node));
  if (!new_bp) failwith("tracee_follow_function: malloc");
  new_bp->addr = target_addr;
  new_bp->instr_bckup = tracee_write_byte(tracee, target_addr, TRAP);
  if (bp_name) {
    printd_low("tracee_follow_function: name = %s\n", bp_name);
    new_bp->name = (const char *) strdup(bp_name);
  } else
    new_bp->name = NULL;
  new_bp->wp = is_watchpoint;
  new_bp->function_arity = NULL;
  if (function_arity) {
    printd_low("tracee_follow_function:\n\t"
      "args_number = %zu, returns_void = %d\n",
      function_arity->args_number, function_arity->returns_void);
    new_bp->function_arity = malloc(sizeof(struct arity));
    if (!new_bp->function_arity)
      failwith("tracee_follow_function: malloc");
    new_bp->function_arity->args_number = function_arity->args_number;
    new_bp->function_arity->returns_void = function_arity->returns_void;
  }
  new_bp->next = tracee->bps;
  tracee->bps = new_bp;
}

static void tracee_pass_breakpoint (tracee_t * tracee, bp_list bp)
{
  tracee_write_byte(tracee, bp->addr, bp->instr_bckup);
  if (ptrace(PTRACE_SINGLESTEP, tracee->pid, 0, 0) < 0)
    failwith("PTRACE_SINGLESTEP");
  tracee_wait_stop(tracee);
  bp->instr_bckup = tracee_write_byte(tracee, bp->addr, TRAP);
}

static void free_bp (bp_list bp_ptr, int free_next);

static void tracee_remove_breakpoint (tracee_t * tracee, void * target_addr)
{
  while (tracee->bps->addr == target_addr) {
    bp_list temp = tracee->bps;
    tracee->bps = tracee->bps->next;
    tracee_write_byte(tracee, temp->addr, temp->instr_bckup);
    free_bp(temp, 0);
  }
  bp_list prev_bp = tracee->bps;
  for (bp_list cur_bp = tracee->bps->next;
       cur_bp != NULL;
       cur_bp = cur_bp->next)
  {
    if (cur_bp->addr == target_addr) {
      prev_bp->next = cur_bp->next;
      tracee_write_byte(tracee, cur_bp->addr, cur_bp->instr_bckup);
      free_bp(cur_bp, 0);
      cur_bp = prev_bp;
    } else
      prev_bp = cur_bp;
  }
}

static void * get_and_shift_ip (tracee_t * tracee, intptr_t shift)
{
  struct user_regs_struct regs;
  if (ptrace(PTRACE_GETREGS, tracee->pid, NULL, &regs) < 0)
    failwith("PTRACE_GETREGS");
  if (shift) {
    printd_low("Applying shift of %p to %p...",
      (void *) shift, (void *) regs.REG_IP);
    regs.REG_IP += shift;
    printd_low(" got %p now.\n", (void *) regs.REG_IP);
    if (ptrace(PTRACE_SETREGS, tracee->pid, NULL, &regs) < 0)
      failwith("PTRACE_SETREGS");
  }
  return (void *) regs.REG_IP;
}

void * get_ip (tracee_t * tracee)
{
  return get_and_shift_ip(tracee, 0);
}

void * get_sp (tracee_t * tracee)
{
  struct user_regs_struct regs;
  if (ptrace(PTRACE_GETREGS, tracee->pid, NULL, &regs) < 0)
    failwith("PTRACE_GETREGS");
  return (void *) regs.REG_SP;
}

void * get_ret (tracee_t * tracee)
{
  struct user_regs_struct regs;
  if (ptrace(PTRACE_GETREGS, tracee->pid, NULL, &regs) < 0)
    failwith("PTRACE_GETREGS");
  return (void *) regs.REG_RET;
}

int tracee_main_loop (tracee_t * tracee,
                      int (*handle_traps) (struct trap_context * ctxt,
                                           int * tracee_keep_looping,
                                           void * extra),
                      void * extra)
{
  if (!handle_traps)
    print_fail("tracee_loop: got NULL 'handle_main_loop' argument.\n");
  int keep_looping = 1;
  int status, ret, signal = 0;
  while (keep_looping) {
    tracee_resume(tracee, signal);
    status = tracee_wait_stop(tracee);
    if (WIFEXITED(status)) {
      fprintf(stderr, BANNER "Child '%s' exited, returning status %d.\n",
        tracee->name, WEXITSTATUS(status));
      return WEXITSTATUS(status);
    }
    if (WIFSTOPPED(status) || WIFSIGNALED(status)) {
      signal = WIFSTOPPED(status) ? WSTOPSIG(status) : WTERMSIG(status);
      if (signal != SIGTRAP) {
        fprintf(stderr, BANNER "Warning: child '%s' got signal '%s'\n",
          tracee->name, strsignal(signal));
        printd("IP = %p\n", get_ip(tracee));
        if (strcmp(strsignal(signal), "Segmentation fault") == 0) {
          tracee_resume(tracee, signal);
          return EXIT_FAILURE;
        }
        ret = handle_traps(NULL, &keep_looping, extra);
      } else {
        void * ip = get_and_shift_ip(tracee, -TRAP_SZ);
        printd("Got SIGTRAP at %p\n", ip);
        /* Identify SIGTRAP */
        int unknown_sigtrap = 1;
        for (bp_list bp = tracee->bps; bp != NULL; bp = bp->next) {
          if (bp->addr == ip) {
            printd_low("Saved instruction byte: 0x%hhx\n", bp->instr_bckup);
            unknown_sigtrap = 0;
            struct trap_context trap_ctxt;
            trap_ctxt.name = (const char *) bp->name;
            trap_ctxt.function_arity = bp->function_arity;
            trap_ctxt.is_wp = 0;
            if (ptrace(PTRACE_GETREGS, tracee->pid, NULL, &trap_ctxt.regs) < 0)
              failwith("PTRACE_GETREGS (in bp_list loop)");
            if (bp->wp) { /* Is it a watched function? */
              trap_ctxt.is_wp = 1;
              void * sp = get_sp(tracee);
              printd_low("sp = %p\n", sp);
              void * ret_addr = (void *)
                ptrace(PTRACE_PEEKDATA, tracee->pid, sp, NULL);
              printd_low("target ret addr: %p\n", ret_addr);
              tracee_add_breakpoint(
                tracee, ret_addr, bp->name, bp->function_arity, 0);
              printd("Passing breakpoint at %p...", bp->addr);
              tracee_pass_breakpoint(tracee, bp);
              printd(" passed!\n");
              ret = handle_traps(&trap_ctxt, &keep_looping, extra);
            } else {
              printd_low("= return from watched function\n");
              ret = handle_traps(&trap_ctxt, &keep_looping, extra);
              tracee_remove_breakpoint(tracee, ip);
            }
            break;
          }
        }
        if (unknown_sigtrap) {
          printd("Couldn't identify SIGTRAP\n");
          get_and_shift_ip(tracee, +TRAP_SZ);
          ret = handle_traps(NULL, &keep_looping, extra);
        }
        signal = 0;
      }
    }
  }
  return ret;
}

static void free_bp (bp_list bp_ptr, int free_next)
{
  if (bp_ptr) {
    if (bp_ptr->name) free((void *) bp_ptr->name);
    if (bp_ptr->function_arity) free(bp_ptr->function_arity);
    if (free_next) free_bp(bp_ptr->next, 1);
    free(bp_ptr);
  }
}

void tracee_free (tracee_t * tracee)
{
  free((void *) tracee->name);
  free_bp(tracee->bps, 1);
  free(tracee);
}

void tracee_follow_function (tracee_t * tracee,
                             void * target_addr,
                             const char * function_name,
                             struct arity * function_arity)
{
  tracee_add_breakpoint(tracee, target_addr, function_name, function_arity, 1);
}

void tracee_unfollow_function (tracee_t * tracee, void * target_addr)
{
  tracee_remove_breakpoint(tracee, target_addr);
}
