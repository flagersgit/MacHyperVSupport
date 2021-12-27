//
//  Completion.cpp
//  MacHyperVSupport
//
//  Created by flagers on 12/27/21.
//

#include "Completion.hpp"

OSDefineMetaClassAndStructors(Completion, OSObject);

bool Completion::init() {
  if (!OSObject::init()) {
    return false;
  }
  
  DBGLOG("allocating lock for completion");
  lock = IOLockAlloc();
  if (lock == NULL) {
    SYSLOG("failed to allocate lock for completion");
    return false;
  } else {
    DBGLOG("successfully allocated lock for completion");
  }
  
  return true;
}

void Completion::free() {
  IOLockFree(lock);
  OSObject::free();
  return;
}

void Completion::waitForCompletion() {
  //
  // Sleep thread until completion.
  //
  IOLockLock(lock);
  while (isSleeping) {
    IOLockSleep(lock, &isSleeping, THREAD_INTERRUPTIBLE);
  }
  IOLockUnlock(lock);
  return;
};

void Completion::complete() {
  //
  // Wake sleeping thread.
  //
  IOLockLock(lock);
  isSleeping = false;
  IOLockUnlock(lock);
  IOLockWakeup(lock, &isSleeping, true);
  return;
}

Completion* Completion::create() {
  Completion *completion = new Completion;
  
  if (completion && !completion->init()) {
    completion->release();
    return NULL;
  }
  
  return completion;
}
