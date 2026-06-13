#pragma once

#include <stdint.h>
#include <string.h>

// Shared cockpit frame for Spartan hub -> display clients (ESP-NOW broadcast).
// Copy this header into M5/Waveshare firmware for decoding.

constexpr uint8_t kSpartanCockpitMagic = 0x53;  // 'S'
constexpr uint8_t kSpartanCockpitVersion = 1;
constexpr size_t kSpartanCockpitFrameSize = 14;

#pragma pack(push, 1)
struct SpartanCockpitFrame {
  uint8_t magic;
  uint8_t version;
  uint16_t seq;
  uint16_t lambda_x1000;
  uint16_t rpm;
  int16_t advance_x10;
  uint8_t map;
  uint8_t spartan_status;
  uint8_t flags;
  uint8_t crc8;
};
#pragma pack(pop)

constexpr uint8_t kSpartanFlagLambdaValid = 0x01;
constexpr uint8_t kSpartanFlagTuneFresh = 0x02;
constexpr uint8_t kSpartanFlagTuneConnected = 0x04;

inline uint8_t spartanCockpitCrc8(const uint8_t *data, size_t length)
{
  uint8_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

inline bool spartanCockpitFrameValid(const SpartanCockpitFrame &frame)
{
  if (frame.magic != kSpartanCockpitMagic || frame.version != kSpartanCockpitVersion) {
    return false;
  }
  const uint8_t expected = spartanCockpitCrc8(reinterpret_cast<const uint8_t *>(&frame),
                                              kSpartanCockpitFrameSize - 1);
  return frame.crc8 == expected;
}

inline void spartanCockpitEncode(SpartanCockpitFrame *frame,
                                 uint16_t seq,
                                 float lambda,
                                 bool lambdaValid,
                                 uint16_t rpm,
                                 float advance,
                                 uint8_t map,
                                 uint8_t spartanStatus,
                                 bool tuneFresh,
                                 bool tuneConnected)
{
  frame->magic = kSpartanCockpitMagic;
  frame->version = kSpartanCockpitVersion;
  frame->seq = seq;
  frame->lambda_x1000 = lambdaValid ? static_cast<uint16_t>(lambda * 1000.0f + 0.5f) : 0;
  frame->rpm = rpm;
  frame->advance_x10 = static_cast<int16_t>(advance * 10.0f + (advance >= 0 ? 0.5f : -0.5f));
  frame->map = map;
  frame->spartan_status = spartanStatus;
  frame->flags = 0;
  if (lambdaValid) {
    frame->flags |= kSpartanFlagLambdaValid;
  }
  if (tuneFresh) {
    frame->flags |= kSpartanFlagTuneFresh;
  }
  if (tuneConnected) {
    frame->flags |= kSpartanFlagTuneConnected;
  }
  frame->crc8 = spartanCockpitCrc8(reinterpret_cast<const uint8_t *>(frame), kSpartanCockpitFrameSize - 1);
}
