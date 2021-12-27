//
//  Completion.hpp
//  MacHyperVSupport
//
//  Created by flagers on 12/27/21.
//

#ifndef Completion_hpp
#define Completion_hpp

#include <libkern/c++/OSObject.h>
#include <IOKit/IOLocks.h>

#include "HyperV.hpp"

#define SYSLOG(str, ...) SYSLOG_PRINT("HyperVPCI-Completion", str, ## __VA_ARGS__)
#define DBGLOG(str, ...) DBGLOG_PRINT("HyperVPCI-Completion", str, ## __VA_ARGS__)

class Completion : public OSObject {
  OSDeclareDefaultStructors(Completion);
  
private:
  IOLock *lock;
  bool isSleeping;
  
public:
  bool init() override;
  
  void free() override;
  
  void waitForCompletion();
  
  void complete();
  
  static Completion* create();
};

#endif /* Completion_hpp */
