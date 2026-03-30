/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef TX_INTERFACE_APM_H
#define TX_INTERFACE_APM_H

#include "apm.h"
#include "../inputdev.h"
#include "../../mod_misc/SimpleXMLTransfer.h"

struct NoiseConfig {
  bool enabled;
  float accel_sigma;
  float gyro_sigma;
  float gps_pos_sigma;
  float gps_vel_sigma;
  float mag_sigma;
  float baro_alt_sigma;
  float airspeed_sigma;
};

struct BiasConfig {
  float accel_x, accel_y, accel_z;
  float gyro_x, gyro_y, gyro_z;
  float mag_x, mag_y, mag_z;
  float baro_alt;
};

class T_TX_InterfaceAPM : public T_TX_Interface
{
  public:
   T_TX_InterfaceAPM();
   virtual ~T_TX_InterfaceAPM();

   virtual int inputMethod() { return(T_TX_Interface::eIM_apm); }

   int init(SimpleXMLTransfer* config);

   virtual void putBackIntoCfg(SimpleXMLTransfer* config);

   bool getInputData(TSimInputs* inputs);

private:
   bool getInputDataAPM(TSimInputs* inputs);
   bool getInputDataSmol(TSimInputs* inputs);

   void sendImu(float time_sec, float ax, float ay, float az,
                float p, float q, float r);
   void sendGps(float time_sec, double lat_deg, double lon_deg,
                float alt_m, float vn, float ve, float vd);
   void sendMag(float time_sec, float hx, float hy, float hz);
   void sendBaro(float time_sec, float alt_m, float press_pa, float temp_C);
   void sendAirspeed(float time_sec, float indicated_airspeed_mps);
   void sendTruth(float time_sec, double lat_deg, double lon_deg,
                  float alt_m, float vn, float ve, float vd,
                  float phi, float theta, float psi, float indicated_airspeed_mps);
   void sendRc(float time_sec);

   float gaussNoise(float sigma);

   APM*              input;
   uint8_t           reverse;
   std::string       device;

   bool              _use_smol_protocol;
   uint32_t          _cycle;
   NoiseConfig       _noise;
   BiasConfig        _bias;

   float             _density_ratio = 1.0f;
   bool              _wmm_initialized;
   double            _earth_field_ned[3]; // Gauss, NED
   double            _origin_lat_rad;
   double            _origin_lon_rad;
};

#endif
