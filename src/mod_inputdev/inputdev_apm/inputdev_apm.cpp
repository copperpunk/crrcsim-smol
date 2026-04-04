/*
 * APM input interface
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 (or later) as published by the Free Software Foundation.
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
#include "../../crrc_main.h"
#include "../../global.h"
#include "../../aircraft.h"
#include "../../SimStateHandler.h"
#include "../../mod_fdm/fdm.h"
#include "../../mod_misc/ls_constants.h"
#include "../../mod_env/earth/atmos_62.h"
#include "inputdev_apm.h"
#include "coremag.h"

#include <stdio.h>
#include <cmath>
#include <random>

#define PI 3.141592653589793
#define FEET2METERS 0.3048
#define PSF_TO_PA 47.880258

#pragma GCC optimize("O0")

static std::mt19937 rng(42);

T_TX_InterfaceAPM::T_TX_InterfaceAPM()
{
    input = NULL;
    _use_smol_protocol = false;
    _cycle = 0;
    _wmm_initialized = false;
    _noise = {};
    _bias = {};
    _earth_field_ned[0] = 0;
    _earth_field_ned[1] = 0;
    _earth_field_ned[2] = 0;
    _origin_lat_rad = 0;
    _origin_lon_rad = 0;
}

T_TX_InterfaceAPM::~T_TX_InterfaceAPM()
{
    if (input != NULL)
        delete input;
}

int T_TX_InterfaceAPM::init(SimpleXMLTransfer* config)
{
    char devicestr[100];

    T_TX_Interface::init(config);

    device = config->getString("inputMethod.apm.device", "udpserver,127.0.0.1/0.0.0.0,9002");

    std::string protocol = config->getString("inputMethod.apm.protocol", "ardupilot");
    _use_smol_protocol = (protocol == "smol");

    if (_use_smol_protocol) {
        std::string sensor_model_path = config->getString(
            "inputMethod.apm.sensor_model", "configs/smol_sensor_model.xml");
        printf("smol protocol: loading sensor model from %s\n", sensor_model_path.c_str());
        SimpleXMLTransfer sensor_cfg(sensor_model_path);

        _noise.enabled     = sensor_cfg.getInt("apm.noise.enabled", 1) != 0;
        _noise.accel_sigma = sensor_cfg.getDouble("apm.noise.accel_sigma", 0.035);
        _noise.gyro_sigma  = sensor_cfg.getDouble("apm.noise.gyro_sigma", 0.001);
        _noise.gps_pos_sigma = sensor_cfg.getDouble("apm.noise.gps_pos_sigma", 0.5);
        _noise.gps_vel_sigma = sensor_cfg.getDouble("apm.noise.gps_vel_sigma", 0.1);
        _noise.mag_sigma   = sensor_cfg.getDouble("apm.noise.mag_sigma", 0.005);
        _noise.baro_alt_sigma = sensor_cfg.getDouble("apm.noise.baro_alt_sigma", 0.3);
        _noise.airspeed_sigma = sensor_cfg.getDouble("apm.noise.airspeed_sigma", 0.1);
        printf("  noise %s\n", _noise.enabled ? "enabled" : "disabled");

        _bias.accel_x  = sensor_cfg.getDouble("apm.bias.accel_x", 0.0);
        _bias.accel_y  = sensor_cfg.getDouble("apm.bias.accel_y", 0.0);
        _bias.accel_z  = sensor_cfg.getDouble("apm.bias.accel_z", 0.0);
        _bias.gyro_x   = sensor_cfg.getDouble("apm.bias.gyro_x", 0.0);
        _bias.gyro_y   = sensor_cfg.getDouble("apm.bias.gyro_y", 0.0);
        _bias.gyro_z   = sensor_cfg.getDouble("apm.bias.gyro_z", 0.0);
        _bias.mag_x    = sensor_cfg.getDouble("apm.bias.mag_x", 0.0);
        _bias.mag_y    = sensor_cfg.getDouble("apm.bias.mag_y", 0.0);
        _bias.mag_z    = sensor_cfg.getDouble("apm.bias.mag_z", 0.0);
        _bias.baro_alt = sensor_cfg.getDouble("apm.bias.baro_alt", 0.0);
        printf("  biases accel(%.3f,%.3f,%.3f) gyro(%.4f,%.4f,%.4f) "
               "mag(%.3f,%.3f,%.3f) baro(%.1f)\n",
               _bias.accel_x, _bias.accel_y, _bias.accel_z,
               _bias.gyro_x, _bias.gyro_y, _bias.gyro_z,
               _bias.mag_x, _bias.mag_y, _bias.mag_z,
               _bias.baro_alt);

        double origin_lat_deg = sensor_cfg.getDouble("apm.origin.latitude_deg", 0.0);
        double origin_lon_deg = sensor_cfg.getDouble("apm.origin.longitude_deg", 0.0);
        _origin_lat_rad = origin_lat_deg * M_PI / 180.0;
        _origin_lon_rad = origin_lon_deg * M_PI / 180.0;
        printf("  origin (%.6f, %.6f)\n", origin_lat_deg, origin_lon_deg);
    }

    strncpy(devicestr, device.c_str(), 100);
    devicestr[99] = '\0';
    input = new APM(devicestr);
    reverse = 0;

    return 0;
}

void T_TX_InterfaceAPM::putBackIntoCfg(SimpleXMLTransfer* config)
{
    T_TX_Interface::putBackIntoCfg(config);
    config->setAttributeOverwrite("inputMethod.apm.device", device);
}

float T_TX_InterfaceAPM::gaussNoise(float sigma)
{
    if (!_noise.enabled || sigma <= 0.0f)
        return 0.0f;
    std::normal_distribution<float> dist(0.0f, sigma);
    return dist(rng);
}

#define degrees(r) ((r)*RAD_TO_DEG)

bool T_TX_InterfaceAPM::getInputData(TSimInputs* inputs)
{
    if (_use_smol_protocol)
        return getInputDataSmol(inputs);
    else
        return getInputDataAPM(inputs);
}

bool T_TX_InterfaceAPM::getInputDataAPM(TSimInputs* inputs)
{
    struct apm_input apm_input;
    struct apm_fdm apm_fdm;

    if (!input->get_input(&apm_input)) {
        return false;
    }

    inputs->elevator = apm_input.pitch_rate;
    inputs->aileron  = apm_input.roll_rate;
    inputs->throttle = apm_input.throttle;
    inputs->rudder   = apm_input.yaw_rate;
    inputs->pitch    = apm_input.col_pitch;

    if (Global::aircraft->getFDM() == NULL) {
        return true;
    }

    CRRCMath::Vector3 vel, waccel, accel, pqr;

    vel    = Global::aircraft->getFDM()->getVel();
    waccel = Global::aircraft->getFDM()->getAccel();
    waccel.r[2] -= 9.80665 / FEET2METERS;
    pqr    = Global::aircraft->getFDM()->getPQR();

    accel = Global::aircraft->getFDM()->WorldToBody(waccel);

    apm_fdm.timestamp = Global::dt * Global::Simulation->SimSteps();
    apm_fdm.latitude = degrees(Global::aircraft->getFDM()->getLat());
    apm_fdm.longitude = degrees(Global::aircraft->getFDM()->getLon());
    apm_fdm.altitude = Global::aircraft->getFDM()->getAlt() * FEET2METERS;

    apm_fdm.heading = atan2(-vel.r[1], vel.r[0]);
    apm_fdm.speedN = vel.r[0] * FEET2METERS;
    apm_fdm.speedE = vel.r[1] * FEET2METERS;
    apm_fdm.speedD = vel.r[2] * FEET2METERS;
    apm_fdm.xAccel = accel.r[0] * FEET2METERS;
    apm_fdm.yAccel = accel.r[1] * FEET2METERS;
    apm_fdm.zAccel = accel.r[2] * FEET2METERS;

    apm_fdm.rollRate = pqr.r[0];
    apm_fdm.pitchRate = pqr.r[1];
    apm_fdm.yawRate = pqr.r[2];

    apm_fdm.roll = Global::aircraft->getFDM()->getPhi();
    apm_fdm.pitch = Global::aircraft->getFDM()->getTheta();
    apm_fdm.yaw = Global::aircraft->getFDM()->getPsi();

    apm_fdm.airspeed = Global::aircraft->getFDM()->getVRelAirmass() * FEET2METERS;

    input->put_state_data(&apm_fdm);

    return true;
}

bool T_TX_InterfaceAPM::getInputDataSmol(TSimInputs* inputs)
{
    SimActuatorMsg actuator;
    if (!input->recv_actuator(&actuator)) {
        return false;
    }

    inputs->aileron  = actuator.aileron;
    inputs->elevator = -actuator.elevator;
    inputs->throttle = actuator.throttle;
    inputs->rudder   = -actuator.rudder;

    static const char* kModeNames[] = {"Manual", "Acro", "AttHold", "Cruise", "Loiter", "Auto"};
    Global::flightModeString = actuator.mode <= 5 ? kModeNames[actuator.mode] : "???";

    if (Global::aircraft->getFDM() == NULL) {
        return true;
    }

    float time_sec = static_cast<float>(Global::dt * Global::Simulation->SimSteps());

    double lat_rad = Global::aircraft->getFDM()->getLat() + _origin_lat_rad;
    double lon_rad = Global::aircraft->getFDM()->getLon() + _origin_lon_rad;
    double alt_ft  = Global::aircraft->getFDM()->getAlt();
    double lat_deg = lat_rad * RAD_TO_DEG;
    double lon_deg = lon_rad * RAD_TO_DEG;
    float  alt_m   = static_cast<float>(alt_ft * FEET2METERS);

    double phi   = Global::aircraft->getFDM()->getPhi();
    double theta = Global::aircraft->getFDM()->getTheta();
    double psi   = Global::aircraft->getFDM()->getPsi();

    CRRCMath::Vector3 vel = Global::aircraft->getFDM()->getVel();
    float vn = static_cast<float>(vel.r[0] * FEET2METERS);
    float ve = static_cast<float>(vel.r[1] * FEET2METERS);
    float vd = static_cast<float>(vel.r[2] * FEET2METERS);

    CRRCMath::Vector3 waccel = Global::aircraft->getFDM()->getAccel();
    waccel.r[2] -= 9.80665 / FEET2METERS;
    CRRCMath::Vector3 accel = Global::aircraft->getFDM()->WorldToBody(waccel);
    float ax = static_cast<float>(accel.r[0] * FEET2METERS);
    float ay = static_cast<float>(accel.r[1] * FEET2METERS);
    float az = static_cast<float>(accel.r[2] * FEET2METERS);

    CRRCMath::Vector3 pqr = Global::aircraft->getFDM()->getPQR();
    float p = static_cast<float>(pqr.r[0]);
    float q = static_cast<float>(pqr.r[1]);
    float r = static_cast<float>(pqr.r[2]);

    float true_airspeed_mps = static_cast<float>(
        Global::aircraft->getFDM()->getVRelAirmass() * FEET2METERS);
    float indicated_airspeed_mps = true_airspeed_mps * sqrtf(_density_ratio);

    sendImu(time_sec,
            ax + _bias.accel_x + gaussNoise(_noise.accel_sigma),
            ay + _bias.accel_y + gaussNoise(_noise.accel_sigma),
            az + _bias.accel_z + gaussNoise(_noise.accel_sigma),
            p + _bias.gyro_x + gaussNoise(_noise.gyro_sigma),
            q + _bias.gyro_y + gaussNoise(_noise.gyro_sigma),
            r + _bias.gyro_z + gaussNoise(_noise.gyro_sigma));

    sendTruth(time_sec, lat_deg, lon_deg, alt_m, vn, ve, vd,
              static_cast<float>(phi), static_cast<float>(theta),
              static_cast<float>(psi), indicated_airspeed_mps);

    // RC input comes from external joystick publisher, not from sim
    // sendRc(time_sec);

    if (_cycle % 2 == 0) {
        if (!_wmm_initialized) {
            double field[6];
            double h_km = alt_m / 1000.0;
            calc_magvar(lat_rad, lon_rad, h_km, now_to_julian_days(), field);
            _earth_field_ned[0] = field[3] / 100000.0;
            _earth_field_ned[1] = field[4] / 100000.0;
            _earth_field_ned[2] = field[5] / 100000.0;
            _wmm_initialized = true;
            printf("WMM earth field NED: %.4f %.4f %.4f Gauss\n",
                   _earth_field_ned[0], _earth_field_ned[1], _earth_field_ned[2]);
        }

        double cphi = cos(phi), sphi = sin(phi);
        double cth  = cos(theta), sth = sin(theta);
        double cpsi = cos(psi), spsi = sin(psi);

        double fn = _earth_field_ned[0];
        double fe = _earth_field_ned[1];
        double fd = _earth_field_ned[2];

        float hx = static_cast<float>(
            (cth*cpsi)*fn + (cth*spsi)*fe + (-sth)*fd);
        float hy = static_cast<float>(
            (sphi*sth*cpsi - cphi*spsi)*fn +
            (sphi*sth*spsi + cphi*cpsi)*fe +
            (sphi*cth)*fd);
        float hz = static_cast<float>(
            (cphi*sth*cpsi + sphi*spsi)*fn +
            (cphi*sth*spsi - sphi*cpsi)*fe +
            (cphi*cth)*fd);

        sendMag(time_sec,
                hx + _bias.mag_x + gaussNoise(_noise.mag_sigma),
                hy + _bias.mag_y + gaussNoise(_noise.mag_sigma),
                hz + _bias.mag_z + gaussNoise(_noise.mag_sigma));
    }

    if (_cycle % 4 == 0) {
        double sigma, v_sound, t_amb_r, p_amb_psf;
        ls_atmos(alt_ft, &sigma, &v_sound, &t_amb_r, &p_amb_psf);
        _density_ratio = static_cast<float>(sigma);
        float press_pa = static_cast<float>(p_amb_psf * PSF_TO_PA);
        float temp_C = static_cast<float>(t_amb_r / 1.8 - 273.15);
        float baro_alt = alt_m + _bias.baro_alt + gaussNoise(_noise.baro_alt_sigma);

        sendBaro(time_sec, baro_alt, press_pa, temp_C);
    }

    if (_cycle % 20 == 0) {
        double lat_noise_deg = gaussNoise(_noise.gps_pos_sigma) / 111320.0;
        double lon_noise_deg = gaussNoise(_noise.gps_pos_sigma) /
            (111320.0 * cos(lat_rad));
        float alt_noise = gaussNoise(_noise.gps_pos_sigma);

        sendGps(time_sec,
                lat_deg + lat_noise_deg,
                lon_deg + lon_noise_deg,
                alt_m + alt_noise,
                vn + gaussNoise(_noise.gps_vel_sigma),
                ve + gaussNoise(_noise.gps_vel_sigma),
                vd + gaussNoise(_noise.gps_vel_sigma));
    }

    if (_cycle % 10 == 0) {
        sendAirspeed(time_sec, indicated_airspeed_mps + gaussNoise(_noise.airspeed_sigma));
    }

    _cycle++;
    return true;
}

void T_TX_InterfaceAPM::sendImu(float time_sec, float ax, float ay, float az,
                                 float p, float q, float r)
{
    SimImuMsg msg = {};
    msg.header.msg_type = SIM_MSG_IMU;
    msg.header.payload_len = sizeof(SimImuMsg) - sizeof(SimMsgHeader);
    msg.sequence = _cycle;
    msg.time_sec = time_sec;
    msg.ax = ax;
    msg.ay = ay;
    msg.az = az;
    msg.p = p;
    msg.q = q;
    msg.r = r;
    input->send_msg(&msg, sizeof(msg));
}

void T_TX_InterfaceAPM::sendGps(float time_sec, double lat_deg, double lon_deg,
                                 float alt_m, float vn, float ve, float vd)
{
    SimGpsMsg msg = {};
    msg.header.msg_type = SIM_MSG_GPS;
    msg.header.payload_len = sizeof(SimGpsMsg) - sizeof(SimMsgHeader);
    msg.time_sec = time_sec;
    msg.latitude_deg = lat_deg;
    msg.longitude_deg = lon_deg;
    msg.altitude_m = alt_m;
    msg.vn = vn;
    msg.ve = ve;
    msg.vd = vd;
    msg.fix_type = 3;
    msg.sats = 12;
    input->send_msg(&msg, sizeof(msg));
}

void T_TX_InterfaceAPM::sendMag(float time_sec, float hx, float hy, float hz)
{
    SimMagMsg msg = {};
    msg.header.msg_type = SIM_MSG_MAG;
    msg.header.payload_len = sizeof(SimMagMsg) - sizeof(SimMsgHeader);
    msg.time_sec = time_sec;
    msg.hx = hx;
    msg.hy = hy;
    msg.hz = hz;
    input->send_msg(&msg, sizeof(msg));
}

void T_TX_InterfaceAPM::sendBaro(float time_sec, float alt_m, float press_pa,
                                  float temp_C)
{
    SimBaroMsg msg = {};
    msg.header.msg_type = SIM_MSG_BARO;
    msg.header.payload_len = sizeof(SimBaroMsg) - sizeof(SimMsgHeader);
    msg.time_sec = time_sec;
    msg.altitude_m = alt_m;
    msg.pressure_pa = press_pa;
    msg.temp_C = temp_C;
    input->send_msg(&msg, sizeof(msg));
}

void T_TX_InterfaceAPM::sendAirspeed(float time_sec, float indicated_airspeed_mps)
{
    SimAirspeedMsg msg = {};
    msg.header.msg_type = SIM_MSG_AIRSPEED;
    msg.header.payload_len = sizeof(SimAirspeedMsg) - sizeof(SimMsgHeader);
    msg.time_sec = time_sec;
    msg.indicated_airspeed_mps = indicated_airspeed_mps;
    input->send_msg(&msg, sizeof(msg));
}

void T_TX_InterfaceAPM::sendTruth(float time_sec, double lat_deg, double lon_deg,
                                   float alt_m, float vn, float ve, float vd,
                                   float phi, float theta, float psi,
                                   float indicated_airspeed_mps)
{
    SimTruthMsg msg = {};
    msg.header.msg_type = SIM_MSG_TRUTH;
    msg.header.payload_len = sizeof(SimTruthMsg) - sizeof(SimMsgHeader);
    msg.time_sec = time_sec;
    msg.latitude_deg = lat_deg;
    msg.longitude_deg = lon_deg;
    msg.altitude_m = alt_m;
    msg.vn = vn;
    msg.ve = ve;
    msg.vd = vd;
    msg.phi = phi;
    msg.theta = theta;
    msg.psi = psi;
    msg.indicated_airspeed_mps = indicated_airspeed_mps;
    input->send_msg(&msg, sizeof(msg));
}

void T_TX_InterfaceAPM::sendRc(float time_sec)
{
    SimRcMsg msg = {};
    msg.header.msg_type = SIM_MSG_RC;
    msg.header.payload_len = sizeof(SimRcMsg) - sizeof(SimMsgHeader);
    msg.time_sec = time_sec;

    auto bidir_to_pwm = [](float v) -> int16_t {
        return static_cast<int16_t>(1500.0f + v * 1024.0f);
    };
    auto unidir_to_pwm = [](float v) -> int16_t {
        return static_cast<int16_t>(988.0f + v * 1024.0f);
    };

    msg.roll     = bidir_to_pwm(Global::inputs.aileron);
    msg.pitch    = bidir_to_pwm(Global::inputs.elevator);
    msg.throttle = unidir_to_pwm(Global::inputs.throttle);
    msg.yaw      = bidir_to_pwm(Global::inputs.rudder);
    msg.flaps    = unidir_to_pwm(Global::inputs.flap);

    msg.mode_a = 2012;
    msg.mode_b = 2012;

    input->send_msg(&msg, sizeof(msg));
}
