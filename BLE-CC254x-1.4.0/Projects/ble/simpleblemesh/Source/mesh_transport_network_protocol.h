#ifndef MESH_TRANSPORT_PROTOCOL_H
#define MESH_TRANSPORT_PROTOCOL_H

#ifdef	__cplusplus
extern "C" {
#endif

//#define TEST_FLAG

#ifdef TEST_FLAG
    #include <time.h>
    typedef unsigned char uint8;
    typedef unsigned short uint16;
    typedef unsigned long uint32;
    typedef uint32 uint24;
    #define TRUE 1
    #define FALSE 0
#else
    #include "comdef.h"
#endif


typedef enum
{
  BROADCAST = 0,
  GROUP_BROADCAST,
  STATELESS_MESSAGE,
  STATEFUL_MESSAGE,
  STATEFUL_MESSAGE_ACK

} MessageType;

typedef void (*advertiseDataFunction)(uint8* data, uint8 length, uint16 delay);
typedef void (*cancelAdvertisementDataFunction)(uint16 source, uint8 sequenceID);
typedef void (*onMessageRecieved)(uint16 source, uint8* message, uint8 length);
typedef uint32 (*getSystemTimestampFunction) ();
typedef uint16 (*randomFunction)();

typedef struct  
{
    uint16 networkIdentifier;
    uint16 source;
    uint8 length : 5;
    MessageType type : 3;
    uint8 sequenceID;
    uint16 destination;
} MessageHeader;

typedef struct 
{
    uint16 source;
    uint8 sequenceID;
    uint8 timesReceived;
    uint32 time;
} ProccessedMessageInformation;

typedef struct 
{
    uint16 destination;
    uint8 sequenceId;
    uint8 length;
    uint32 time;
    uint8* message;
    uint8 resentCount;
} PendingACK;

void initializeMeshConnectionProtocol(uint16 networkIdentifier, 
	uint16 deviceIdentifier, 
	advertiseDataFunction dataFunction, 
	onMessageRecieved messageCallback,
        getSystemTimestampFunction timestampFunction,
        randomFunction randFun,
        cancelAdvertisementDataFunction cancelDataFunction);

void processIncomingMessage(uint8* data, uint8 length);

void broadcastMessage(uint8* message, uint8 length);

void broadcastGroupMessage(uint16 groupDestination, uint8* message, uint8 length);

void sendStatefulMessage(uint16 destination, uint8* message, uint8 length);

void sendStatelessMessage(uint16 destination, uint8* message, uint8 length);

uint8 joinGroup(uint16 groupId);

uint8 leaveGroup(uint16 groupId);

void destructMeshConnectionProtocol();

void periodicTask();

#ifdef	__cplusplus
}
#endif

#endif