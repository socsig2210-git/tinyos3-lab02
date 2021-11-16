
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"


/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_new_multithread()
{
  int exitval;

  Task call =  cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  sys_ThreadExit(exitval);
}

/*void increase_refcount(PTCB* ptcb){
  ptcb->refcount++;
}*/

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args){

  if(task!=NULL){

    //initialize ptcb and tcb
    //initialization of new ptcb  
    PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB)); //acquire space for ptcb
    ptcb->task = task;
    ptcb->argl = argl;
    ptcb->args = args;
    ptcb->exitval = CURPROC->exitval;
    ptcb->exit_cv = COND_INIT;
    ptcb->exited =0;
    ptcb->detached = 0;
    ptcb->refcount = 0;

    //Pass ptcb to curr_thread, in order to pass process info to new thread
    cur_thread()->ptcb = ptcb;

    //initialization of new tcb
    TCB* tcb  = spawn_thread(CURPROC, start_new_multithread);

    //CURPROC->main_thread = tcb;

    // Connect new tcb with ptcb
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;
    //ptcb->tcb->owner_pcb = tcb->owner_pcb;
    
    // Add ptcb_node to pcb's ptcb_list
    rlnode_init(&ptcb->ptcb_list_node, ptcb);
    rlist_push_back(&CURPROC->ptcb_list, &ptcb->ptcb_list_node);

    // +1 thread to PCB
    CURPROC->thread_count++;

    wakeup(ptcb->tcb); 

    return (Tid_t) ptcb;

  }

  return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread();
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{

  if((PTCB*)tid != NULL){

    // tid is (PTCB*) of T2
    PTCB* T2 = (PTCB*) tid;
    TCB* T2_tcb = T2->tcb;
    PCB* T2_pcb = T2_tcb->owner_pcb;

    if(T2->detached == 1){
      printf("Thread Detached");
      return -1;
    }

    if(T2_pcb != CURPROC){
      printf("Process different than Current proccess");
      return -1;
    }

    if(cur_thread() == T2_tcb){
      printf("Current Thread self joins");
      return -1;
    }

    if(T2->exited == 1){
      printf("Thread Exited");
      return -1;
    }
    //wait until T2 exits or detaches
    while(T2->exited==0 && T2->detached==0){
      kernel_wait(&T2->exit_cv,SCHED_USER);
    }

    if(T2->exited==1){ 
      T2->refcount++;
      *exitval = T2->exitval;
    }


    if(exitval!=NULL){
      assert(T2->refcount>0);
      T2->refcount--;
      if(T2->refcount==0){
        rlist_remove(&T2->ptcb_list_node);
        free(T2);
    }
  }

    return 0;
  }

  return -1;


}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  if((PTCB*) tid !=NULL){
    PTCB* Detached_PTCB = (PTCB*) tid;
    assert(Detached_PTCB->tcb!=NULL);

    if(Detached_PTCB->exited==1){
      return -1;
    }

    Detached_PTCB->detached=1;
    kernel_broadcast(&Detached_PTCB->exit_cv);
    return 0;
}
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

}

