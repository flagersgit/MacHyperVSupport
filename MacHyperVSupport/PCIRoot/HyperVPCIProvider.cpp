//
//  HyperVPCIProvider.cpp
//  Hyper-V PCI root bridge provider
//
//  Copyright © 2021 Goldfish64. All rights reserved.
//

#include "HyperVPCIProvider.hpp"
#include <IOKit/IODeviceTreeSupport.h>

OSDefineMetaClassAndStructors(HyperVPCIProvider, super);

IOService* HyperVPCIProvider::probe(IOService *provider, SInt32 *score) {
  IORegistryEntry *pciEntry = IORegistryEntry::fromPath("/PCI0@0", gIODTPlane);
  if (pciEntry != NULL) {
    HVDBGLOG("Existing PCI bus found (Gen1 VM), will not start");
    
    pciEntry->release();
    return NULL;
  }
  
  return this;
}

bool HyperVPCIProvider::start(IOService *provider) {
  HVCheckDebugArgs();
  
  //
  // Required by AppleACPIPlatform.
  //
  if (!super::init(provider, NULL, getPropertyTable())) {
    HVSYSLOG("Failed to initialize parent provider");
    return false;
  }
  
  if (!super::start(provider)) {
    HVSYSLOG("Failed to start parent provider");
    return false;
  }
  
  registerService();
  HVDBGLOG("Hyper-V PCI provider is now registered");
  return true;
}

IOReturn HyperVPCIProvider::evaluateInteger(const char *     objectName,
                                            UInt32 *         resultInt32,
                                            OSObject *       params[],
                                            IOItemCount      paramCount,
                                            IOOptionBits     options) {
  if (!strcmp(objectName, "_RMV")) {
    *resultInt32 = true;
    return kIOReturnSuccess;
  }
  return kIOReturnBadArgument;
}

bool HyperVPCIProvider::registerHotplugPCIBridge(IOPCIBridge *pciBridge) {
  //
  // Get the PCI root provider and its IOService plane path.
  //
  OSDictionary *pciMatching = IOService::serviceMatching("HyperVPCIProvider");
  if (pciMatching == NULL) {
    //HVSYSLOG("Failed to create HyperVPCIProvider matching dictionary");
    return false;
  }
  
  OSIterator *pciIterator = IOService::getMatchingServices(pciMatching);
  if (pciIterator == NULL) {
    //HVSYSLOG("Failed to create HyperVPCIProvider matching iterator");
    return false;
  }
  
  pciIterator->reset();
  HyperVPCIProvider *pciInstance = OSDynamicCast(HyperVPCIProvider, pciIterator->getNextObject());
  pciIterator->release();
  
  if (pciInstance == NULL) {
    //HVSYSLOG("Failed to locate HyperVPCIProvider instance");
    return false;
  }
  
  char registryPath[1024];
  int registryPathLen = sizeof (registryPath);
  if (!pciInstance->getPath((char *) &registryPath, &registryPathLen, gIOServicePlane)) {
    return false;
  }
  pciBridge->setProperty("acpi-path", (char *) &registryPath);
  
  return true;
}
