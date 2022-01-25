//
//  Completion.cpp
//  MacHyperVSupport
//
//  Created by flagers on 12/27/21.
//

#include "Completion.hpp"

OSDefineMetaClassAndStructors(HyperVCompletion, OSObject);

bool HyperVCompletion::init() {
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

void HyperVCompletion::free() {
  IOLockFree(lock);
  OSObject::free();
  return;
}

void HyperVCompletion::waitForCompletion() {
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

void HyperVCompletion::complete() {
  //
  // Wake sleeping thread.
  //
  IOLockLock(lock);
  isSleeping = false;
  IOLockUnlock(lock);
  IOLockWakeup(lock, &isSleeping, true);
  return;
}

HyperVCompletion* HyperVCompletion::create() {
  HyperVCompletion *completion = new HyperVCompletion;
  
  if (completion && !completion->init()) {
    completion->release();
    return NULL;
  }
  
  return completion;
}
