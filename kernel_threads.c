
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_streams.h"


/*
  This function is provided as an argument to spawn,
  to execute the main thread of a process.
*/
void start_new_multithread()
{
  int exitval;
  assert(cur_thread()->ptcb != NULL);
  Task call =  cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;

  exitval = call(argl,args);
  ThreadExit(exitval);
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
    
    if(args!=NULL) {
      // ptcb->args = malloc(argl);
      // memcpy(ptcb->args, args, argl);
      ptcb->args = args;
      assert(ptcb->args != NULL);
      //fprintf(stderr, "args value");  
    }
    else{
      ptcb->args=NULL;
      assert(ptcb->args == NULL);
    }
    // if(args!=NULL) {
    //   ptcb->args = malloc(argl);
    //   memcpy(ptcb->args, args, argl);
    // }
    // else
    //   ptcb->args=NULL;
    ptcb->exitval = 0;
    ptcb->exit_cv = COND_INIT;
    ptcb->exited =0;
    ptcb->detached = 0;
    ptcb->refcount = 0;

    //Pass ptcb to curr_thread, in order to pass process info to new thread
    assert(cur_thread() != NULL);
    //cur_thread()->ptcb = ptcb; //THIS MIGHT BE NEEDED CHECK

    // IF SOMETHING DOESN'T WORK ADD PCB* FIELD TO PTCB 

    //initialization of new tcb
    TCB* tcb  = spawn_thread(CURPROC, start_new_multithread);

    //CURPROC->main_thread = tcb;

    // Connect new tcb with ptcb
    tcb->ptcb = ptcb;
    ptcb->tcb = tcb;
    //ptcb->tcb->owner_pcb = tcb->owner_pcb;
    
    // Add ptcb_node to pcb's ptcb_list
    rlnode_init(&ptcb->ptcb_list_node, ptcb); // CHECK INSTEAD OF PTCB --> NULL
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
  assert(cur_thread()->ptcb != NULL);
	return (Tid_t) cur_thread()->ptcb;
}


int check_valid_ptcb(Tid_t tid){
  // Checks if ptcb is valid/exists

  // if rlist_find returns 1, ptcb exists in current's proccess ptcb list
  PTCB* ptcb = (PTCB*) tid;
  if(rlist_find(&CURPROC->ptcb_list, ptcb, NULL))
    return 1;
  else 
    return 0;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval){
  
  PTCB* ptcb_to_join = (PTCB*) tid;

  if(rlist_find(&CURPROC->ptcb_list, ptcb_to_join, NULL) != NULL && tid != sys_ThreadSelf() && ptcb_to_join->detached == 0) { /**< Checks*/

    ptcb_to_join->refcount++; //Increase ref counter by 1

    while (ptcb_to_join->exited != 1 && ptcb_to_join->detached != 1) // Wait till new ptcb is exited or detached.
    {
      kernel_wait(&ptcb_to_join->exit_cv, SCHED_USER);
    }

    ptcb_to_join->refcount--; // Since get detached or exited, decrease ref counter

    if(ptcb_to_join->detached != 0) // If get detached dont return the exit value
      return -1;

    if(exitval!= NULL ) // exitval save 
      *exitval = ptcb_to_join->exitval;

    if(ptcb_to_join->refcount == 0){ // If PTCB exited and no other thread waits it then remove from PTCB list and set free.
      rlist_remove(&ptcb_to_join->ptcb_list_node); 
      free(ptcb_to_join);
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
  PTCB* ptcb_to_detach = (PTCB*) tid;

  if(tid != NOTHREAD && rlist_find(& CURPROC->ptcb_list, ptcb_to_detach, NULL) != NULL && ptcb_to_detach->exited == 0){ // Checks
    ptcb_to_detach->detached = 1; // Set ptcb to detached
    kernel_broadcast(&ptcb_to_detach->exit_cv); // Wake up Threads
    return 0;
  }else{
    return -1;
  }
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

  PTCB* ptcb = (PTCB*) sys_ThreadSelf();    
  
  ptcb->exited = 1;
  ptcb->exitval = exitval;
  kernel_broadcast(&ptcb->exit_cv);

  PCB* curproc = CURPROC;
  curproc->thread_count--;
  if ( curproc->thread_count == 0){

    if (get_pid(curproc)!= 1){

    /* Reparent any children of the exiting process to the
       initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list
         and signal the initial task */

      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);
    }

    assert(is_rlist_empty(& curproc->children_list));
    assert(is_rlist_empty(& curproc->exited_list));
    /*
      Do all the other cleanup we want here, close files etc.
     */

    /* Release the args data */
    if(curproc->args) {
      free(curproc->args);
      curproc->args = NULL;
    }

    /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }

    /* Disconnect my main_thread */
    curproc->main_thread = NULL;

    /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;
  }
  /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);

}
