/* Test-only cross-process serialization for closed observation files. The
 * semaphore lives in public guest memory for the disposable profile's life. */
#ifndef AR_QA_OBSERVATION_H
#define AR_QA_OBSERVATION_H
#include <exec/semaphores.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>
static struct SignalSemaphore *qa_observation_sem;
static int qa_observation_take(void)
{
    if(!qa_observation_sem) {
        struct QaLock { struct SignalSemaphore sem; char name[48]; } *candidate;
        candidate=AllocVec(sizeof(*candidate),MEMF_PUBLIC|MEMF_CLEAR);
        if(!candidate) return 0;
        strcpy(candidate->name,"AmiRocket.QA.Observation.v1"); InitSemaphore(&candidate->sem);
        candidate->sem.ss_Link.ln_Name=candidate->name;
        Forbid(); qa_observation_sem=FindSemaphore(candidate->name);
        if(!qa_observation_sem) { AddSemaphore(&candidate->sem); qa_observation_sem=&candidate->sem; candidate=NULL; }
        Permit(); if(candidate) FreeVec(candidate);
    }
    ObtainSemaphore(qa_observation_sem); return 1;
}
static void qa_observation_release(void) { ReleaseSemaphore(qa_observation_sem); }
#endif
