//
//  HyperVPCIPrivate.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

void *MallocZero(vm_size_t size) {
  void * result;
  result = IOMalloc(size);
  if (result) {
      bzero(result, size);
  }
  return result;
}

void HyperVPCI::handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count) {
  DBGLOG("Interrupt");
}


bool HyperVPCI::negotiateProtocol(HyperVPCIProtocolVersion protocolVersion) {
  HyperVPCIPacket *packet;
  packet = (HyperVPCIPacket*)MallocZero(sizeof(HyperVPCIPacket) + sizeof(HyperVPCIVersionRequest));
  
  HyperVPCIVersionRequest* versionRequest = (HyperVPCIVersionRequest*)&packet->message;
  versionRequest->messageType.type = kHyperVPCIMessageQueryProtocolVersion;
  versionRequest->protocolVersion = protocolVersion;
  
  
  DBGLOG("negotiating protocol"); // to be removed, for debugging only.
  DBGLOG("request type %X", versionRequest->messageType.type);
  if (hvDevice->writeInbandPacket(&versionRequest, sizeof(HyperVPCIVersionRequest), true) != kIOReturnSuccess) {
    SYSLOG("failed to send protocol negotiation message");
    return false;
  }
  
  return true;
};
