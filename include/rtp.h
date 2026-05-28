#ifndef RTP_H
#define RTP_H

#include <cstdint>

#pragma pack(push, 1)

struct RTPHeader {

    uint8_t versionPayload;
    uint8_t payloadType;

    uint16_t sequenceNumber;

    uint32_t timestamp;

    uint32_t ssrc;
};

#pragma pack(pop)

#endif