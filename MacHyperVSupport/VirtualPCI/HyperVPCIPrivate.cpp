//
//  HyperVPCIPrivate.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"

void HyperVPCI::handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count) {
  DBGLOG("Interrupt");
  VMBusPacketType type;
  UInt32 headersize;
  UInt32 totalsize;
  
  HyperVPCIPacket *completionPacket;

  void *responseBuffer;
  UInt32 responseLength;

  while (true) {
    if (!hvDevice->nextPacketAvailable(&type, &headersize, &totalsize)) {
      break;
    }
    
    UInt8 *buf = (UInt8*)IOMalloc(totalsize);
    hvDevice->readRawPacket((void*)buf, totalsize);
    
    switch (type) {
      case kVMBusPacketTypeDataInband:
        break;
      
      case kVMBusPacketTypeCompletion:
        if (hvDevice->getPendingTransaction(((VMBusPacketHeader*)buf)->transactionId, &responseBuffer, &responseLength)) {
          memcpy(responseBuffer, (UInt8*)buf + headersize, responseLength);
          hvDevice->wakeTransaction(((VMBusPacketHeader*)buf)->transactionId);
        } else {
          completionPacket = (HyperVPCIPacket*)((VMBusPacketHeader*)buf)->transactionId;
          responseBuffer = (HyperVPCIResponse*)buf;
          completionPacket->completionFunc(completionPacket->completionCtx, (HyperVPCIResponse*)responseBuffer, responseLength);
        };
        break;

      default:
        break;
    }
    
    IOFree(buf, totalsize);
  }
}


IOReturn HyperVPCI::negotiateProtocol(HyperVPCIProtocolVersion version) {
  IOReturn ret;
  
  HyperVPCIVersionRequest *versionRequest;
  HyperVPCIPacket *packet;
  
  packet = (HyperVPCIPacket*)IOMalloc(sizeof(*packet) + sizeof(*versionRequest));
  if (!packet){ return kIOReturnNoMemory; }
  
  versionRequest = (HyperVPCIVersionRequest*)&packet->message;
  versionRequest->messageType.type = kHyperVPCIMessageQueryProtocolVersion;
  
  versionRequest->protocolVersion = version;
  ret = hvDevice->writeInbandPacketWithTransactionId(versionRequest, sizeof(HyperVPCIVersionRequest), (UInt64)packet, true);
  if (ret != kIOReturnSuccess) {
    SYSLOG("HyperVPCI failed to request version");
  };
  
  return true;
};
