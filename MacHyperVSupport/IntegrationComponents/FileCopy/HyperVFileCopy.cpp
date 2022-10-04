//
//  HyperVFileCopy.cpp
//  Hyper-V file copy driver
//
//  Copyright © 2022 flagers. All rights reserved.
//

#include "HyperVFileCopy.hpp"

OSDefineMetaClassAndStructors(HyperVFileCopy, super);

static const VMBusICVersion fcopyVersions[] = {
  kHyperVFileCopyVersionV1
};

bool HyperVFileCopy::start(IOService *provider) {
  if (HVCheckOffArg()) {
    HVSYSLOG("Disabling Hyper-V File Copy due to boot arg");
    return false;
  }

  if (!super::start(provider)) {
    HVSYSLOG("super::start() returned false");
    return false;
  }

  HVCheckDebugArgs();
  setICDebug(debugEnabled);

  HVDBGLOG("Initializing Hyper-V File Copy");
  return true;
}

void HyperVFileCopy::stop(IOService *provider) {
  HVDBGLOG("Stopping Hyper-V File Copy");
  super::stop(provider);
}

void HyperVFileCopy::handlePacket(VMBusPacketHeader *pktHeader, UInt32 pktHeaderLength, UInt8 *pktData, UInt32 pktDataLength) {
  VMBusICMessageFileCopy *fileCopyMsg = (VMBusICMessageFileCopy*) pktData;
  HyperVUserClientFileCopy userClientMsg;
  size_t nlen;
  size_t plen;

  switch (fileCopyMsg->header.type) {
    case kVMBusICMessageTypeNegotiate:
      //
      // Determine supported protocol version and communicate back to Hyper-V.
      //
      if (!processNegotiationResponse(&fileCopyMsg->negotiate, fcopyVersions, arrsize(fcopyVersions))) {
        HVSYSLOG("Failed to determine a supported Hyper-V File Copy version");
        fileCopyMsg->header.status = kHyperVStatusFail;
      }
      break;

    case kVMBusICMessageTypeFileCopy:
      if (!_hvDevice->getHvController()->checkUserClient()) {
        HVDBGLOG("Userspace is not ready yet");
        fileCopyMsg->header.status = kHyperVStatusFail;
      }
      switch (fileCopyMsg->fcopyHeader.operation) {
        case kVMBusICFileCopyOperationStartFileCopy:
        case kVMBusICFileCopyOperationWriteToFile:
        case kVMBusICFileCopyOperationCompleteFileCopy:
        case kVMBusICFileCopyOperationCancelFileCopy:
          memset(&userClientMsg, 0, sizeof (userClientMsg));
          if (fileCopyMsg->fcopyHeader.operation == kVMBusICFileCopyOperationStartFileCopy) {
            memset(&userClientMsg.operationData.startCopy.fileName, 0, PATH_MAX);
            utf8_encodestr(fileCopyMsg->startCopy.fileName, kHyperVFileCopyMaxPath, (UInt8*)&userClientMsg.operationData.startCopy.fileName, &nlen, PATH_MAX, 0, 0);
            memset(&userClientMsg.operationData.startCopy.filePath, 0, PATH_MAX);
            utf8_encodestr(fileCopyMsg->startCopy.filePath, kHyperVFileCopyMaxPath, (UInt8*)&userClientMsg.operationData.startCopy.filePath, &plen, PATH_MAX, 0, 0);
            userClientMsg.operationData.startCopy.fileSize = fileCopyMsg->startCopy.fileSize;
            userClientMsg.operationData.startCopy.copyFlags = (HyperVUserClientFileCopyFlags)fileCopyMsg->startCopy.copyFlags;
            HVSYSLOG("File copy attempted for file %s at path %s", &userClientMsg.operationData.startCopy.fileName, &userClientMsg.operationData.startCopy.filePath);
          } else {
            memcpy(&userClientMsg.operationData, fileCopyMsg + sizeof (VMBusICMessageFileCopyHeader), pktDataLength - sizeof (VMBusICMessageFileCopyHeader));
          }
          userClientMsg.operation = (HyperVUserClientFileCopyOperation)fileCopyMsg->fcopyHeader.operation;
          _hvDevice->getHvController()->notifyUserClient(kHyperVUserClientNotificationTypeFileCopy, &userClientMsg, sizeof (userClientMsg));
          
          break;
        default:
          HVDBGLOG("Unknown file copy operation type %u", fileCopyMsg->fcopyHeader.operation);
          fileCopyMsg->header.status = kHyperVStatusFail;
          break;
      }
      break;

    default:
      HVDBGLOG("Unknown file copy message type %u", fileCopyMsg->header.type);
      fileCopyMsg->header.status = kHyperVStatusFail;
      break;
  }

  //
  // Send response back to Hyper-V. The packet size will always be the same as the original inbound one.
  //
  fileCopyMsg->header.flags = kVMBusICFlagTransaction | kVMBusICFlagResponse;
  _hvDevice->writeInbandPacket(fileCopyMsg, pktDataLength, false);
}