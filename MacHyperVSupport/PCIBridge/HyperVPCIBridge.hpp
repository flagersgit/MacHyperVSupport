//
//  HyperVPCIBridge.hpp
//  Hyper-V PCI passthrough device support
//
//  Copyright © 2022 Goldfish64. All rights reserved.
//

#ifndef HyperVPCIBridge_hpp
#define HyperVPCIBridge_hpp

#include <IOKit/pci/IOPCIBridge.h>

#include "HyperVPCIProvider.hpp"
#include "HyperVVMBusDevice.hpp"
#include "HyperVPCIBridgeRegs.hpp"

#include "HyperVModuleDevice.hpp"

class HyperVPCIBridge : public HV_PCIBRIDGE_CLASS {
  OSDeclareDefaultStructors(HyperVPCIBridge);
  HVDeclareLogFunctionsVMBusChild("pcib");
  typedef HV_PCIBRIDGE_CLASS super;
  
private:
  //
  // Parent VMBus device.
  //
  HyperVVMBusDevice       *_hvDevice         = nullptr;
  IOInterruptEventSource  *interruptSource  = nullptr;
  IOSimpleLock      *pciLock;
  
  HyperVPCIBridgeProtocolVersion  currentPciVersion;
  
  HyperVModuleDevice  *hvModuleDevice;
  IODeviceMemory      *ioMemory;
  IORangeScalar       pciConfigSpace;
  IOMemoryDescriptor  *pciConfigMemoryDescriptor;
  IOMemoryMap         *pciConfigMemoryMap;
  UInt32              msiCap;
  bool                isMsiX = false;
  bool                interruptConfigured = false;
  
  IOPCIDevice         *pciDeviceNub = nullptr;
  
  UInt32                        pciFunctionCount = 0;
  HyperVPCIFunctionDescription  *pciFunctions = nullptr;
  
  UInt8                       busNum;
  UInt64                      bars[kHyperVPCIBarCount];
  UInt64                      barSizes[kHyperVPCIBarCount];
  IODeviceMemory::InitElement iodmRangeList[kHyperVPCIBarCount];
  int                         iodmRangeListIdx = 0;
  
  void handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count);
  void handleIncomingPCIMessage(HyperVPCIBridgeIncomingMessageHeader *pciMsgHeader, UInt32 msgSize);
  
  bool negotiateProtocolVersion();
  bool queryBusRelations();
  bool ejectPCIDevice();
  bool allocatePCIConfigWindow();
  bool enterPCID0();
  bool queryResourceRequirements();
  bool sendResourcesAllocated(UInt32 slot);
  bool sendResourcesReleased(UInt32 slot);
  
  inline UInt64 getBarSize(UInt64 barValue) {
    return roundup((1 + ~(barValue & kHyperVPCIBarMemoryMask)), PAGE_SIZE);
  }
  
  UInt32 readPCIConfig(UInt32 offset, UInt8 size);
  void writePCIConfig(UInt32 offset, UInt8 size, UInt32 value);
  
public:
  //
  // IOService overrides.
  //
  virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
  virtual void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
  
  //
  // IOPCIBridge overrides.
  //
  virtual bool configure(IOService *provider) APPLE_KEXT_OVERRIDE;
  IODeviceMemory *ioDeviceMemory() APPLE_KEXT_OVERRIDE { HVDBGLOG("start"); return ioMemory; }
  UInt32 configRead32(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
  void configWrite32(IOPCIAddressSpace space, UInt8 offset, UInt32 data) APPLE_KEXT_OVERRIDE;
  UInt16 configRead16(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
  void configWrite16(IOPCIAddressSpace space, UInt8 offset, UInt16 data) APPLE_KEXT_OVERRIDE;
  UInt8 configRead8(IOPCIAddressSpace space, UInt8 offset) APPLE_KEXT_OVERRIDE;
  void configWrite8(IOPCIAddressSpace space, UInt8 offset, UInt8 data) APPLE_KEXT_OVERRIDE;

  IOPCIAddressSpace getBridgeSpace() APPLE_KEXT_OVERRIDE {
    HVDBGLOG("start");
    IOPCIAddressSpace space = { 0 };
    space.s.busNum = busNum;
    return space;
  }

  UInt8 firstBusNum() APPLE_KEXT_OVERRIDE {
    HVDBGLOG("start");
    return busNum;
  }
  
  UInt8 lastBusNum() APPLE_KEXT_OVERRIDE {
    HVDBGLOG("start");
    return busNum;
  }
  
  virtual bool publishNub(IOPCIDevice *nub, UInt32 index) APPLE_KEXT_OVERRIDE;
};

#endif
