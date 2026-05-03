/*
 * CRRCsim - the Charles River Radio Control Club Flight Simulator Project
 *
 * Copyright (C) 2006-2009 Jan Reucker (original author)
 * Copyright (C) 2006 Todd Templeton
 * Copyright (C) 2007, 2008, 2010 Jens Wilhelm Wulf
 * Copyright (C) 2008 Olivier Bordes
 *
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
  

/** \file global.h
 *
 *  Global variables used in CRRCsim.
 */

#ifndef GLOBAL_H
#define GLOBAL_H

#include <cstdint>
#include <string>
#include <pthread.h>
#include "mod_fdm/fdm_inputs.h"
#include "mod_inputdev/inputdev.h"
#include "mouse_kbd.h"

// needed to make LOG() work without add. headers
#include "mod_main/EventDispatcher.h"

// There's no need to pull in the full headers here.
// Just declare the classes and leave the responsibility
// to the files that really need the single variables.
class SimStateHandler;
class CGUIMain;
class CRRCAirplane;
class CRRCAudioServer;
class ModFDMInterface;
class Scenery;
class T_GameHandler;
class T_TX_Interface;
class TInputDev;
class Aircraft;
class FlightRecorder;
class Robots;

/**
 * Contains data related to test mode.
 * 
 * @author Jens Wilhelm Wulf
 */
struct TestModeData
{
  /**
   *  test TXInterface
   */
  int   test_mode;
  
  /**
   * The real flAutozoom is changed while in testmode
   */
  float flAutozoom;
  
};

class Global
{
  public:
    Global(void);
    static SimStateHandler* Simulation;     ///< The simulation's main state machine.
    static int              training_mode;  ///< Draw thermals in the sky?
    static int              nVerbosity;     ///< How much info in the HUD?
    static int              HUDCompass;     ///< Draw azimuth/elevation in the HUD?
    static Scenery*         scenery;        ///< The scenery.
    static int              wind_mode;      ///< Wind estimation mode
    static CGUIMain*        gui;            ///< The GUI.
    static CRRCAudioServer* soundserver;    ///< The sound server.
    static T_GameHandler*   gameHandler;    ///< The active game mode.
    static TSimInputs       inputs;         ///< Control input values.
    static float            dt;             ///< time interval of integration of EOMs
    static std::string      verboseString;  ///< Informational line of text
    static TestModeData     testmode;       ///< Test mode data structure
    static int              nFPS;           ///< average video update rate (FPS)
    static T_TX_Interface*  TXInterface; 
    static TInputDev*       inputDev;
    static Aircraft*        aircraft;       ///< A complete Aircraft (model & FDM).
    static FlightRecorder*  recorder;
    static Robots*          robots;
    static std::string      flightModeString;

    static bool             hand_launch_mode;    ///< true when CRRCSIM_LAUNCH_MODE=hand
    static bool             hand_launch_thrown;  ///< true after the pilot presses the launch key
    static double           hand_launch_velocity_rel;  ///< velocity_rel applied on throw
    static double           hand_launch_phi;
    static double           hand_launch_theta;
    static double           hand_launch_psi;
    static double           hand_launch_posX;
    static double           hand_launch_posY;
    static double           hand_launch_altitude;
    static double           hand_launch_dZRot;
    static constexpr float  hand_launch_platform_height_ft = 6.0f;  ///< virtual platform AGL [ft]
    static constexpr float  hand_launch_platform_radius_ft = 4.0f;  ///< virtual platform extent [ft]
    static double           hand_launch_throw_velocity_mps;         ///< throw velocity [m/s]

    static bool             realtime_throttle;  ///< true => sleep_until pacing on (default), false => FTRT
    static uint32_t         rng_seed;           ///< seed for the noise RNG; 0 = auto from time(nullptr)
    static float            duration_sec;       ///< sim-time exit limit; 0 = run until SIGINT
    static uint16_t         command_port;       ///< TCP port for the hand-launch command surface; 0 = disabled

    static void lockFDM(void) { pthread_mutex_lock(&fdm_lock); }
    static void unlockFDM(void) { pthread_mutex_unlock(&fdm_lock); }

private:
    static pthread_mutex_t  fdm_lock;
};


/** This macro logs a line of text to the console */
#define LOG(_x)     do{                                               \
                        LogMessageEvent msg(_x);                      \
                        EventDispatcher::getInstance()->raise(&msg);  \
                      }while(0)

#endif //GLOBAL_H
