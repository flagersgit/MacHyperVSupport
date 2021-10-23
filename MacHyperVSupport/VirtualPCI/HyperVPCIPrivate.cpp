//
//  HyperVPCIPrivate.cpp
//  MacHyperVSupport
//
//  Created by flagers on 9/18/21.
//

#include "HyperVPCI.hpp"
//#include "HyperVPCIHelper.h"

void HyperVPCI::handleInterrupt(OSObject *owner, IOInterruptEventSource *sender, int count) {
  DBGLOG("Interrupt");
  VMBusPacketType type;
  UInt32 headersize;
  UInt32 totalsize;
  
  HyperVPCIPacket *completionPacket;
  HyperVPCIIncomingMessage *newMessage;
  
  HyperVPCIBusRelations *busRelations;

  void *responseBuffer;
  UInt32 responseLength;

  while (true) {
    if (!hvDevice->nextPacketAvailable(&type, &headersize, &totalsize)) {
      DBGLOG("Last packet");
      break;
    }
    
    UInt8 *buf = (UInt8*)IOMalloc(totalsize);
    DBGLOG("Reading packet to buffer");
    hvDevice->readRawPacket((void*)buf, totalsize);
    
    switch (type) {
      case kVMBusPacketTypeDataInband:
        DBGLOG("Packet type: inband");
        if (hvDevice->getPendingTransaction(((VMBusPacketHeader*)buf)->transactionId, &responseBuffer, &responseLength)) {
          memcpy(responseBuffer, (UInt8*)buf + headersize, responseLength);
          hvDevice->wakeTransaction(((VMBusPacketHeader*)buf)->transactionId);
        } else {
          newMessage = (HyperVPCIIncomingMessage*)responseBuffer;
          switch (newMessage->messageType.type) {
            case kHyperVPCIMessageBusRelations:
              busRelations = (HyperVPCIBusRelations*)buf;
              if (responseLength < offsetof(HyperVPCIBusRelations, func) + (sizeof(HyperVPCIFunctionDescription) * (busRelations->deviceCount))) {
                  DBGLOG("bus relations too small");
                  break;
              }
          }
        }
        break;
      
      case kVMBusPacketTypeCompletion:
        DBGLOG("Packet type: completion");
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
        SYSLOG("Unhandled packet type: %d, tid %llx len %d", type, ((VMBusPacketHeader*)buf)->transactionId, responseLength);
        break;
    }
    
    IOFree(buf, totalsize);
  }
}


IOReturn HyperVPCI::negotiateProtocol(HyperVPCIProtocolVersion version) {
  IOReturn ret;
  
  HyperVPCIVersionRequest *versionRequest;
  HyperVPCIPacket *packet;
  HyperVPCICompletion *completionPacket;
  
  packet = (HyperVPCIPacket*)IOMalloc(sizeof(*packet) + sizeof(*versionRequest));
  if (!packet){ return kIOReturnNoMemory; }
  
  packet->completionFunc = HyperVPCI::genericCompletion;
  packet->completionCtx = &completionPacket;
  
  versionRequest = (HyperVPCIVersionRequest*)&packet->message;
  versionRequest->messageType.type = kHyperVPCIMessageQueryProtocolVersion;
  versionRequest->protocolVersion = version;
  
  ret = hvDevice->writeInbandPacketWithTransactionId(versionRequest, sizeof(HyperVPCIVersionRequest), (UInt64)packet, true);
  if (ret != kIOReturnSuccess) {
    SYSLOG("HyperVPCI failed to request version");
  };
  
  return ret;
};

void HyperVPCI::genericCompletion(void *ctx, HyperVPCIResponse *response, int responsePacketSize) {
  HyperVPCICompletion* completionPacket = (HyperVPCICompletion*)ctx;
  
  if (responsePacketSize >= (offsetof(HyperVPCIResponse, status)+sizeof(response->status))) {
    completionPacket->status = response->status;
  } else {
    completionPacket->status = -1;
  }
};

IOReturn HyperVPCI::queryRelations() {
  IOReturn ret;
  HyperVPCIMessage message;
  
  message.type = kHyperVPCIMessageQueryBusRelations;
  
  ret = hvDevice->writeInbandPacket(&message, sizeof(message), true);
  if (ret != kIOReturnSuccess) {
    SYSLOG("HyperVPCI failed to write query relations packet");
  };
  
  return ret;
};
