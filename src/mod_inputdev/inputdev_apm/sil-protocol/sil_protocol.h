#pragma once

#include <stdint.h>

#pragma pack(push, 1)

enum SimMsgType : uint8_t {
  SIM_MSG_IMU      = 1,
  SIM_MSG_GPS      = 2,
  SIM_MSG_MAG      = 3,
  SIM_MSG_BARO     = 4,
  SIM_MSG_AIRSPEED = 5,
  SIM_MSG_TRUTH    = 6,
  SIM_MSG_RC       = 7,
  SIM_MSG_ACTUATOR = 10,
};

struct SimMsgHeader {
  uint8_t msg_type;
  uint8_t _pad;
  uint16_t payload_len;
};

struct SimImuMsg {
  SimMsgHeader header;
  uint32_t sequence;
  float time_sec;
  float ax, ay, az;
  float p, q, r;
};

struct SimGpsMsg {
  SimMsgHeader header;
  float time_sec;
  double latitude_deg;
  double longitude_deg;
  float altitude_m;
  float vn, ve, vd;
  uint8_t fix_type;
  uint8_t sats;
  uint16_t _pad;
};

struct SimMagMsg {
  SimMsgHeader header;
  float time_sec;
  float hx, hy, hz;
};

struct SimBaroMsg {
  SimMsgHeader header;
  float time_sec;
  float altitude_m;
  float pressure_pa;
  float temp_C;
};

struct SimAirspeedMsg {
  SimMsgHeader header;
  float time_sec;
  float airspeed_mps;
};

struct SimRcMsg {
  SimMsgHeader header;
  float time_sec;
  int16_t roll;
  int16_t pitch;
  int16_t throttle;
  int16_t yaw;
  int16_t flaps;
  int16_t mode_a;
  int16_t mode_b;
  int16_t _pad;
};

struct SimTruthMsg {
  SimMsgHeader header;
  float time_sec;
  double latitude_deg;
  double longitude_deg;
  float altitude_m;
  float vn, ve, vd;
  float phi, theta, psi;
  float airspeed_mps;
};

struct SimActuatorMsg {
  SimMsgHeader header;
  uint32_t sequence;
  float aileron;
  float elevator;
  float throttle;
  float rudder;
};

#pragma pack(pop)

static_assert(sizeof(SimMsgHeader) == 4, "SimMsgHeader must be 4 bytes");
static_assert(sizeof(SimImuMsg) == 36, "SimImuMsg must be 36 bytes");
static_assert(sizeof(SimGpsMsg) == 44, "SimGpsMsg must be 44 bytes");
static_assert(sizeof(SimMagMsg) == 20, "SimMagMsg must be 20 bytes");
static_assert(sizeof(SimBaroMsg) == 20, "SimBaroMsg must be 20 bytes");
static_assert(sizeof(SimAirspeedMsg) == 12, "SimAirspeedMsg must be 12 bytes");
static_assert(sizeof(SimRcMsg) == 24, "SimRcMsg must be 24 bytes");
static_assert(sizeof(SimTruthMsg) == 56, "SimTruthMsg must be 56 bytes");
static_assert(sizeof(SimActuatorMsg) == 24, "SimActuatorMsg must be 24 bytes");
