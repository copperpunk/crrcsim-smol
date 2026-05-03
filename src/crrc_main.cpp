/*
 * CRRCsim - the Charles River Radio Control Club Flight Simulator Project
 *
 * Copyright (C) 2000, 2001, 2004 Jan Edward Kansky (original author)
 * Copyright (C) 2004-2010 Jens Wilhelm Wulf
 * Copyright (C) 2004-2009 Jan Reucker
 * Copyright (C) 2004 Kees Lemmens
 * Copyright (C) 2005 Joel Lienard
 * Copyright (C) 2005 Lionel Cailler
 * Copyright (C) 2005, 2008 Olivier Bordes
 * Copyright (C) 2006 Todd Templeton
 * Copyright (C) 2007, 2008 Chris Bayley
 * Copyright (C) 2009 Joel Lienard
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
  
/*****************************************************************************
Title: CRRCsim, the Charles River Radio Control Club Flight Simulator Project
Authors:
Jan Kansky:  Programming
Mark Drela:  Aerodynamics

Purpose:  The idea for CRRCsim is to take away any last excuse you might
have for not using a flight simulator to keep your thumbs certified for RC
flying.   It's free, it works, enjoy.  This is an open source project, so if
you don't like the way something works, help us fix it!

Thanks:
  Bruce Jackson for the LaRCsim framework.
  Flight Gear project for the sky sphere.

Contacts:
If you'd like to help with CRRCSIM, then send me an email!
    email                : kansky@ll.mit.edu
*****************************************************************************/

#include "i18n.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <crrc_config.h>

#include "global.h"
#include "defines.h"
#include "crrc_loadair.h"
#include "crrc_main.h"
#include "crrc_system.h"
#include "crrc_sound.h"
#include "mod_landscape/crrc_scenery.h"
#include "SimStateHandler.h"
#include "mod_windfield/windfield.h"
#include "GUI/crrc_gui_main.h"
#include "GUI/crrc_joy.h"
#include "mod_misc/SimpleXMLTransfer.h"
#include "mod_misc/lib_conversions.h"
#include "config.h"
#include "mod_misc/filesystools.h"
#include "zoom.h"
#include "CTime.h"
#include "mod_mode/T_GameHandler.h"
#include "mod_mode/F3F/handlerF3F.h"
#include "mod_fdm/formats/airtoxml.h"
#include "mod_fdm/xmlmodelfile.h"
#include "mod_misc/crrc_rand.h"
#include "mod_video/glconsole.h"
#include "crrc_fdm.h"
#include "mod_misc/ls_constants.h"
#include "mod_misc/scheduler.h"
#include "aircraft.h"
#include "global_video.h"
#include "mod_video/crrc_graphics.h"

#include "mod_main/eventhandler.h"
#include "mod_main/crrc_checkopts.h"
#include "sim_command_listener.h"

#include <chrono>
#include <csignal>
#include <thread>

#include "mod_inputdev/inputdev_serial2/inputdev_serial2.h"
#include "mod_inputdev/inputdev_mnav/inputdev_mnav.h"
#include "mod_inputdev/inputdev_apm/inputdev_apm.h"
#include "mod_inputdev/inputdev_audio/inputdev_audio.h"
#include "mod_inputdev/inputdev_parallel/inputdev_parallel.h"
#include "mod_inputdev/inputdev_software/inputdev_software.h"
#include "mod_inputdev/inputdev_serpic/inputdev_serpic.h"
#include "mod_inputdev/inputdev_serial/inputdev_serial.h"
#include "mod_inputdev/inputdev_zhenhua/inputdev_zhenhua.h"
#include "mod_inputdev/inputdev_ct6a/inputdev_ct6a.h"

#ifdef linux
#include "mod_inputdev/inputdev_rctran/inputdev_rctran.h"
#include "mod_inputdev/inputdev_rctran2/inputdev_rctran2.h"
#endif

#include "record.h"
#include "robots.h"


#include <math.h>
#include <string>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

static volatile sig_atomic_t fdm_thread_running = 1;

static void sigint_handler(int)
{
    static volatile sig_atomic_t caught = 0;
    if (caught++) {
        signal(SIGINT, SIG_DFL);
        raise(SIGINT);
        return;
    }
    fdm_thread_running = 0;
    if (Global::Simulation)
        Global::Simulation->quit();
}

//#define LOG_FRAMES

/*****************************************************************************/
//Configuration Data
//TInputDev  inputDev;

CTime   *crrc_time;


// Pointer to scenery object and sky sphere
CRRCMath::Vector3 player_pos;

// Variometer sound
T_VariometerSound *vario_sound = NULL;
int vario_sound_channel = -1;

// Enviroment interface to FDM
FDMEnviroment* fdmenv = 0;


/*****************************************************************************/
void activate_test_mode()
{
  double dDist;

  TSimInputs inp  = TSimInputs();
  
  Global::testmode.test_mode  = TRUE;
  
  // Set distance to plane to make it visible in a good size, set 
  // fixed flFieldOfView.
  {            
    double dAircraftSize;
    float  flFieldOfView; // degrees
   
    Global::testmode.flAutozoom = flAutozoom;
    flAutozoom          = 0; // autozoom disabled
    
    dAircraftSize = Global::aircraft->getFDM()->getAircraftSize();
    // this is just to read flFieldOfView
    flFieldOfView = zoom_calc(dAircraftSize);
    
    // 1 * 2* tan(M_PI * flFieldOfView /2 / 180) * dDist  = 2*dAircraftSize
    dDist = dAircraftSize / ( 1 * tan(M_PI * flFieldOfView / 2 / 180) );
  }
  
  // enter testmode
  CRRCMath::Vector3 ppos = Global::scenery->getPlayerPosition();
  CRRCMath::Vector3 planeposn(ppos.r[0] - dDist, ppos.r[1], ppos.r[2]);
  Global::aircraft->enterTestmode(planeposn); 
  initialize_flight_model();
  Global::aircraft->getFDMInterface()->update(&inp, 0, 0);
  Video::UpdateCamera(1);//Only once here to initialize. Then, the camera is still
 
}

void leave_test_mode()
{
  Global::testmode.test_mode  = FALSE;
  Global::aircraft->leaveTestmode();
  flAutozoom = Global::testmode.flAutozoom;
}

/*****************************************************************************/

/** \brief Calculate Z rotation for simulated SAL
 *
 *  The angular velocity is calculated using an estimated
 *  rotation radius for the C-of-G of 0.8 m (stretched arm)
 *  plus half the wingspan. The value is negative, so a right-hand
 *  SAL is simulated.
 *
 *  \param vel relative launch velocity
 *
 *  \return body rotation around Z axis in rad/s
 */
double calculate_z_rotation(double vel)
{
  double radius = (0.8 / 0.3048) + (Global::aircraft->getFDM()->getWingspan() / 2.0);
  double velocity = vel * Global::aircraft->getFDM()->getTrimmedFlightVelocity(); // ft/s
  return (-1 * velocity / radius);
}


void initialize_flight_model()
{
  if (Video::camera_mode != Video::CAM_PILOT)
  {
    player_pos = Global::scenery->getPlayerPosition();
    Video::camera_mode = Video::CAM_PILOT;
  }

  float Altitude;

  double velocity_rel = cfgfile->getDouble("launch.velocity_rel", 1);
  double dZRot = 0.0;
  
  if (cfgfile->getInt("launch.sal", 0) == 1)
  {
    dZRot = calculate_z_rotation(velocity_rel);
  }
  double wind_direction = (cfg->wind->getDirection()*M_PI/180);
  double posX, posY;
  
  int StartFromPlayer = cfgfile->getInt("launch.rel_to_player", 1);
  std::string CurrentStartPositionName = cfg->getCurLocCfgPtr(cfgfile)->getString("start.position","");
  if (Global::scenery->getNumStartPosition() == 0) 
    StartFromPlayer = 1;
  
  if (StartFromPlayer == 1)
  {
    // default relative position is similar to what has been used on original 'Cape Cod' and 'Davis':
    double launchx = cfgfile->getDouble("launch.rel_front", MODELSTART_REL_FRONT);
    double launchy = cfgfile->getDouble("launch.rel_right", MODELSTART_REL_RIGHT);
    posX = -player_pos.r[2] + launchx*cos(wind_direction) - launchy*sin(wind_direction);
    posY =  player_pos.r[0] + launchx*sin(wind_direction) + launchy*cos(wind_direction);
  }
  else
  {
    CRRCMath::Vector3 start_pos = Global::scenery->getStartPosition(CurrentStartPositionName);
    posX = -start_pos.r[2];
    posY =  start_pos.r[0];
  }
  double phi,theta,psi,height;
  float plane[4];
  phi = 0;
  theta = cfgfile->getDouble("launch.angle", 0);
  psi = wind_direction + cfgfile->getDouble("launch.heading_offset", 0) * M_PI / 180.0;
  Altitude = cfgfile->getDouble("launch.altitude", 6);
  if (Global::hand_launch_mode)
  {
    // Override config to spawn wings-level on the virtual platform.
    Altitude = Global::hand_launch_platform_height_ft;
    theta = 0.0;
  }
  double zlow = Global::aircraft->getFDM()->getZLow();
  height = Global::scenery->getHeightAndPlane(posX, posY, plane);
  if(Altitude == 0)
    //start on ground : calculate phi et theta so that the airplane is parallel to the ground
    {
    double p_n = -plane[2];//north
    double p_e = plane[0];//est
    double p_u = plane[1];//up
    double h1 = -(plane[3]+p_n*(posX+cos(psi)) + p_e*(posY+sin(psi)) )/p_u;
    double h2 = -(plane[3]+p_n*(posX+sin(psi)) + p_e*(posY+cos(psi)) )/p_u;
    //double h0 = -(plane[3]+p_n*posX + p_e*posY )/p_u;
   theta =atan(h1-height);
    phi = -atan(h2-height);
    zlow = zlow/cos(theta);
    zlow = zlow/cos(phi);    
    //printf ("START theta : %.1f phi: %.1f \n",theta, phi);////
    //printf ("START h: %.1f h0: %.1f h1: %.1f h2: %.1f \n",height,h0,h1,h2);
    }
  Altitude = Altitude + zlow + height;
  printf ("START ALTITUDE : %.1f (%.1f+%.1f)\n",Altitude,zlow,height );////

  double init_velocity_rel = velocity_rel;
  if (Global::hand_launch_mode)
  {
    // Convert the env-var throw velocity from m/s to CRRCSim's relative form
    // (multiple of trim flight velocity in ft/s). The config's launch.velocity_rel
    // is typically 0 for ground-start presets and is ignored in hand-launch mode.
    const double trim_ftps = Global::aircraft->getFDM()->getTrimmedFlightVelocity();
    const double throw_ftps = Global::hand_launch_throw_velocity_mps * M_TO_FT;
    Global::hand_launch_velocity_rel = throw_ftps / trim_ftps;
    Global::hand_launch_phi = phi;
    Global::hand_launch_theta = theta;
    Global::hand_launch_psi = psi;
    Global::hand_launch_posX = posX;
    Global::hand_launch_posY = posY;
    Global::hand_launch_altitude = Altitude;
    Global::hand_launch_dZRot = dZRot;
    Global::hand_launch_thrown = false;
    init_velocity_rel = 0.0;
  }

  Global::aircraft->getFDMInterface()->initAirplaneState(
                                 init_velocity_rel,
                                 phi,
                                 theta,
                                 psi,
                                 posX,
                                 posY,
                                 -1*Altitude,
                                 0.0,
                                 0.0,
                                 dZRot);
  
  Global::Simulation->resume();
  
  {
    SimpleXMLTransfer* header = Global::gameHandler->GetRecordHeader();
    SimpleXMLTransfer* config = Global::aircraft->GetLatestConfig();
    header->setAttribute("airplane.file",     config->getString("airplane.file"));
    header->setAttribute("airplane.graphics", config->getString("airplane.graphics", "0"));
    Global::recorder->Start(header);
    delete header;
  }
}

/*****************************************************************************/

void throw_hand_launched_aircraft()
{
  if (!Global::hand_launch_mode || Global::hand_launch_thrown)
  {
    return;
  }
  Global::aircraft->getFDMInterface()->initAirplaneState(
      Global::hand_launch_velocity_rel,
      Global::hand_launch_phi,
      Global::hand_launch_theta,
      Global::hand_launch_psi,
      Global::hand_launch_posX,
      Global::hand_launch_posY,
      -1 * Global::hand_launch_altitude,
      0.0,
      0.0,
      Global::hand_launch_dZRot);
  Global::hand_launch_thrown = true;
  printf("Hand-launch: throw applied (%.2f m/s, velocity_rel=%.2f).\n",
         Global::hand_launch_throw_velocity_mps,
         Global::hand_launch_velocity_rel);
}

/*****************************************************************************/

void Init_mod_windfield()
{
  initialize_wind_field(cfg->getCurLocCfgPtr(cfgfile));

  // Check if we're using the standard slope windfield
  // or the dynamic soaring windfield
  cfg->checkDynamicSoaring();
}

/*****************************************************************************/
void calc_fps()
{
  static int last_time = 0;
  int cur_time, time_diff;
  static int avg_diff = 10;
  int avg_fps;

  cur_time = SDL_GetTicks();
  time_diff = (cur_time - last_time);

  if (time_diff < 1)
    time_diff = 10;

  avg_diff += ((time_diff << 8) - avg_diff) >> 5;

  avg_fps = (int)rintf(1000.0 / (avg_diff >> 8));
  Global::nFPS = avg_fps;

  last_time = cur_time;
}

/**
 * Reverts to mouse input.
 */
std::string input_method_failed(std::string msg, bool boRevertToMouse = true)
{
  if (msg.length())
    msg += "\n";

  msg += _("Failed to initialize selected input method.\n");
  if (boRevertToMouse)
  {
    msg += _("Reverting to mouse input.");

    // close previous interface
    if (Global::TXInterface != (T_TX_Interface*)0)
    {
      delete Global::TXInterface;
      Global::TXInterface = (T_TX_Interface*)0;
    }

    // select and open mouse interface
    cfgfile->setAttributeOverwrite("inputMethod.method", Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_mouse]);

    Global::TXInterface  = new T_TX_InterfaceSoftware(T_TX_Interface::eIM_mouse, 2);
    if (Global::TXInterface->init(cfgfile))
      msg += "\nEven mouse input failed:\n" + Global::TXInterface->getErrMsg();
  }

  return(msg);
}

/**
 * (Re)configures the input method according to the configuration
 * in <tt>cfgfile</tt>.
 * Returns a message, if something (bad) happened.
 */
std::string reconfigureInputMethod(bool boRevertToMouse)
{
  printf("std::string reconfigureInputMethod()\n");

  std::string method = strU(cfgfile->getString(
        "inputMethod.method",
        Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_mouse]));
  std::string msg;

  printf("New input method: %s\n", method.c_str());

  // stop old input method, holding the FDM lock so the FDM thread
  // doesn't call getInputData() on a deleted interface
  Global::lockFDM();
  if (Global::TXInterface != (T_TX_Interface*)0)
  {
    Global::inputDev->closeJoystick();
    delete Global::TXInterface;
    Global::TXInterface = (T_TX_Interface*)0;
  }
  Global::unlockFDM();

  // create new one
  if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_keyboard])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceSoftware(T_TX_Interface::eIM_keyboard, 4);
    Global::TXInterface->init(cfgfile);
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_mouse])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceSoftware(T_TX_Interface::eIM_mouse, 2);
    Global::TXInterface->init(cfgfile);
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_joystick])) == 0)
  {
#if TEST_WITHOUT_JOYSTICK == 0
    msg = Global::inputDev->openJoystick();
    if (msg.length())
      return(input_method_failed(msg, boRevertToMouse));
#endif
    
    Global::TXInterface  = new T_TX_InterfaceSoftware(T_TX_Interface::eIM_joystick, Global::inputDev->getJoystickNumAxes());
    Global::TXInterface->init(cfgfile);
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_audio])) == 0)
  {
    #if PORTAUDIO > 0
    Global::TXInterface  = new T_TX_InterfaceAudio();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
    #else
      return(input_method_failed("This version of CRRCsim was built without support for the AUDIO interface.\n", boRevertToMouse));
    #endif
  }
#ifdef linux
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_rctran])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceRCTRAN();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_rctran2])) == 0)
  {
    Global::TXInterface  = new T_TX_Interface_RCTran2();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
#endif
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_parallel]))  == 0)
  {
    printf("Trying to init parallel\n");
    Global::TXInterface  = new T_TX_InterfaceParallel();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_serial2])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceSerial2();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_mnav])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceMNAV();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_apm])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceAPM();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_serpic])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceSerPIC();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_zhenhua])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceZhenHua();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg(), boRevertToMouse));
  }
  else if (method.compare(
      strU(Global::inputDev->InputMethodStrings[T_TX_Interface::eIM_CT6A])) == 0)
  {
    Global::TXInterface  = new T_TX_InterfaceCT6A();
    if (Global::TXInterface->init(cfgfile))
      return(input_method_failed(Global::TXInterface->getErrMsg()));
  }
  else
    return(input_method_failed("", boRevertToMouse));

  return("");
}

/**
 * Reading of (mostly initial) values from the xml config into
 * global variables (at least global to this file).
 * Maybe those variables can be encapsulated somewhere else...
 */
void read_config_into_globals()
{
  zoom_reset();
  Global::training_mode = cfgfile->getInt("training_mode.fUse", 0);
  Global::wind_mode = cfgfile->getInt("wind_mode.fUse", 2);
  Global::nVerbosity = cfgfile->getInt("nVerbosity.level", 0);
  Global::HUDCompass = cfgfile->getInt("HUDCompass.fUse", 0);
  Global::dt = cfgfile->getDouble("simulation.flightModel.dt", 0.0025);
  Global::realtime_throttle =
      cfgfile->getInt("simulation.realtime_throttle", 1) != 0;
  Global::rng_seed =
      static_cast<uint32_t>(cfgfile->getInt("simulation.rng_seed", 0));
  Global::duration_sec =
      static_cast<float>(cfgfile->getDouble("simulation.duration_sec", 0.0));
  Global::command_port =
      static_cast<uint16_t>(cfgfile->getInt("simulation.command_port", 0));
  Video::read_config(cfgfile);
}

void write_globals_into_config()
{
  cfgfile->setAttributeOverwrite("training_mode.fUse", Global::training_mode);
  cfgfile->setAttributeOverwrite("wind_mode.fUse", Global::wind_mode);
  cfgfile->setAttributeOverwrite("nVerbosity.level", Global::nVerbosity);
  cfgfile->setAttributeOverwrite("HUDCompass.fUse", Global::HUDCompass);
  cfgfile->setAttributeOverwrite("simulation.flightModel.dt",
                                 doubleToString(Global::dt));
}

/*****************************************************************************/
void initializeRandomNumberGenerator()
{
  time_t sometime = time(0);
  srand((unsigned int) sometime);
  CRRC_Random::insertData(rand());
  std::cout << "RAND_MAX = " << RAND_MAX << "\n";
}

/*****************************************************************************/
/** 
 *  This function tries to perform some cleanup when
 *  exiting. If errmsg != NULL, a system message box
 *  will be opened with the message string.
 *
 *  \param exit_code should be CRRC_EXIT_SUCCESS or CRRC_EXIT_FAILURE
 *  \param errmsg    optional message to be displayed on exit
 */
void crrc_exit(int exit_code, const char *errmsg)
{
  // ToDo: clean up allocated objects
  SDL_Quit();
  
  if ((errmsg != NULL) && (*errmsg != '\0'))
  {
    if (exit_code == CRRC_EXIT_SUCCESS)
    {
      SystemMessage(errmsg, SM_INFO);
    }
    else
    {
      SystemMessage(errmsg, SM_ERROR);
    }
  }
  
  exit(exit_code);
}

/**
 * Description: see header file
 */
void loadAirplane()
{
  Global::aircraft->load(cfgfile, fdmenv);
}


void set_aux(int aux_num, int setting)
{
  if (aux_num <= 0 || aux_num > TSimInputs::NUM_AUX_INPUTS)
    return;
  switch(setting)
  {
    case 1:
      Global::inputs.aux[aux_num - 1] = -0.5;
      break;
    case 2:
      Global::inputs.aux[aux_num - 1] =  0.0;
      break;
    case 3:
      Global::inputs.aux[aux_num - 1] =  0.5;
      break;
    default:
      break;
  }
}

/** Raise an event containing the actual input values
 *
 *  \param inputs   input value structure with current values
 */
void raiseInputEvent(TSimInputs const& inputs)
{
  static AxisUpdateEvent event;
  
  event.set(inputs.aileron,
            inputs.elevator,
            inputs.rudder,
            inputs.throttle,
            inputs.flap,
            inputs.spoiler,
            inputs.retract,
            inputs.pitch);
  EventDispatcher::getInstance()->raise(&event);
}
/*****************************************************************************
*
* try to load the configured scenery. On error case, load default scenery
*
**/
void load_initial_scenery(T_Config *cfg)
{
  std::string sceneryfile = cfg->getLocationName();
  int sky_variant = cfg->getSkyVariant();
  Global::scenery = loadScenery(FileSysTools::getDataPath(sceneryfile).c_str(),
                                sky_variant);
  if (Global::scenery == NULL)
  {
    fprintf(stderr, "Unable to initialize scenery from file %s,\n",
                        sceneryfile.c_str());
    fprintf(stderr, "reverting to default scenery \"scenery/davis-orig.xml\"\n");
        
    // try the default scenery
    Global::scenery = loadScenery(FileSysTools::getDataPath("scenery/davis-orig.xml").c_str());
    if (Global::scenery == NULL)
    {
      std::string s;
      s = "Unable to initialize default scenery from file \"scenery/davis-orig.xml\"";
      crrc_exit(CRRC_EXIT_FAILURE, s.c_str());
    }
    else
    {
      // set configuration file back to default
      cfg->setLocation("scenery/davis-orig.xml", cfgfile);
    }
  }
  // re-read the wind configuration to activate any scenery defaults if
  // no wind config is present in the cfgfile for this location
  cfg->wind->read(cfgfile, cfg);
}


/*
  FDM thread. All FDM physics updates happen within this thread,
  decoupling it from the display
 */
static void *fdm_thread(void *)
{
    // advance by one step to get the display started
    Global::lockFDM();
    Global::aircraft->getFDMInterface()->update(&Global::inputs, Global::dt, 1);
    Global::Simulation->incSimSteps(1);
    Global::unlockFDM();

    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(Global::dt));
    auto next_cycle = clock::now();

    while (fdm_thread_running) {
        next_cycle += period;

        Global::lockFDM();
        if (Global::TXInterface != NULL &&
            Global::TXInterface->getInputData(&Global::inputs)) {
            Global::aircraft->getFDMInterface()->update(&Global::inputs, Global::dt, 1);
            Global::Simulation->incSimSteps(1);
        }
        Global::unlockFDM();

        if (Global::duration_sec > 0.0f) {
            float sim_time_sec = static_cast<float>(
                Global::dt * Global::Simulation->SimSteps());
            if (sim_time_sec >= Global::duration_sec) {
                printf("Sim duration %.2f sec reached at sim_time=%.3f, exiting.\n",
                       Global::duration_sec, sim_time_sec);
                Global::Simulation->setState(STATE_EXIT);
                fdm_thread_running = false;
                break;
            }
        }

        if (Global::realtime_throttle) {
            std::this_thread::sleep_until(next_cycle);
        }
    }
    return NULL;
}
  


/*****************************************************************************/
int main(int argc,char **argv)
{
  float field_of_view;

  // Parse smol SIL automation flags FIRST. crrc_checkversionopt and crrc_checkopts
  // both use getopt(), which on GNU/Linux permutes argv (moves non-option args to
  // the end). Filter our flags out into a separate argv[] so:
  //   1) the GNU-permutation does not scramble their positional pairing, and
  //   2) crrc_checkopts does not reject them as unknown options.
  // The override application (Global::* writes) happens later, AFTER
  // read_config_into_globals() runs so CLI beats config-file values.
  bool cli_no_realtime  = false;
  bool cli_headless     = false;
  bool cli_set_rng_seed = false; uint32_t cli_rng_seed = 0;
  bool cli_set_duration = false; float    cli_duration = 0.0f;
  bool cli_set_port     = false; uint16_t cli_port     = 0;
  const char* cli_launch_mode = nullptr;
  std::vector<char*> filtered_argv;
  filtered_argv.push_back(argv[0]);
  for (int ai = 1; ai < argc; ai++) {
    if (!strcmp(argv[ai], "--no-realtime")) {
      cli_no_realtime = true;
    } else if (!strcmp(argv[ai], "--headless")) {
      cli_headless = true;
    } else if (!strcmp(argv[ai], "--rng-seed") && ai + 1 < argc) {
      cli_rng_seed = static_cast<uint32_t>(strtoul(argv[++ai], nullptr, 10));
      cli_set_rng_seed = true;
    } else if (!strcmp(argv[ai], "--duration") && ai + 1 < argc) {
      cli_duration = static_cast<float>(strtod(argv[++ai], nullptr));
      cli_set_duration = true;
    } else if (!strcmp(argv[ai], "--command-port") && ai + 1 < argc) {
      cli_port = static_cast<uint16_t>(strtoul(argv[++ai], nullptr, 10));
      cli_set_port = true;
    } else if (!strcmp(argv[ai], "--launch-mode") && ai + 1 < argc) {
      cli_launch_mode = argv[++ai];
    } else {
      filtered_argv.push_back(argv[ai]);
    }
  }
  int    filtered_argc     = static_cast<int>(filtered_argv.size());
  char** filtered_argv_ptr = filtered_argv.data();

  if (crrc_checkversionopt(filtered_argc, filtered_argv_ptr))
  {
    crrc_exit(CRRC_EXIT_SUCCESS);
  }

  const char* launch_mode_env = getenv("CRRCSIM_LAUNCH_MODE");
  Global::hand_launch_mode = (launch_mode_env != nullptr) &&
                             (strcmp(launch_mode_env, "hand") == 0);
  if (Global::hand_launch_mode)
  {
    const char* throw_vel_env = getenv("CRRCSIM_HAND_LAUNCH_VEL_MPS");
    if (throw_vel_env == nullptr)
    {
      fprintf(stderr,
              "CRRCSIM_LAUNCH_MODE=hand requires CRRCSIM_HAND_LAUNCH_VEL_MPS to be set "
              "(throw velocity in m/s).\n");
      return EXIT_FAILURE;
    }
    errno = 0;
    char* endptr = nullptr;
    Global::hand_launch_throw_velocity_mps = strtod(throw_vel_env, &endptr);
    if (errno != 0 || endptr == throw_vel_env || *endptr != '\0' ||
        Global::hand_launch_throw_velocity_mps <= 0.0)
    {
      fprintf(stderr,
              "CRRCSIM_HAND_LAUNCH_VEL_MPS must be a positive number (got '%s').\n",
              throw_vel_env);
      return EXIT_FAILURE;
    }
    printf("CRRCSIM_LAUNCH_MODE=hand: aircraft will spawn stationary; press L to throw "
           "(vel=%.2f m/s).\n", Global::hand_launch_throw_velocity_mps);
  }

  Global::lockFDM();

  /*
    the FDM code runs in a separate thread to decouple the display
    update from FDM.
   */
  pthread_t fdm_thread_id;
  pthread_attr_t thread_attr;
  pthread_attr_init(&thread_attr);
  pthread_attr_setdetachstate(&thread_attr, 0);
  pthread_create(&fdm_thread_id, &thread_attr, fdm_thread, NULL);
  pthread_attr_destroy(&thread_attr);
     
  try
  {
    //Internationalisation
    char *loc = setlocale (LC_MESSAGES, "");
    printf("Locale : %s\n",loc);
    bindtextdomain ("crrcsim","./locale/");
    textdomain ("crrcsim");
    bind_textdomain_codeset ("crrcsim","iso-8859-15");//Plib does not work with UTF-8
    Global::TXInterface = (T_TX_Interface*)0;

    Global::inputDev = new TInputDev();
    FileSysTools::SetAppname("crrcsim");
    Global::testmode.test_mode = FALSE;

    Global::Simulation = new SimStateHandler();
    
    Global::aircraft = new Aircraft();
    
    std::string startup_time;
    getSystemTimeString(startup_time);
    printf("CRRCsim %s started at %s\n", PACKAGE_VERSION, startup_time.c_str());
    printf("Running on %s\n", getOSVersionString());
    printf("Using plib version %d.%d.%d\n", PLIB_MAJOR_VERSION,
                                            PLIB_MINOR_VERSION,
                                            PLIB_TINY_VERSION);
    printf("Compiled with SDL version %d.%d.%d\n",  SDL_MAJOR_VERSION,
                                                    SDL_MINOR_VERSION,
                                                    SDL_PATCHLEVEL);
    printf("(Linked SDL version is %d.%d.%d)\n",  SDL_Linked_Version()->major,
                                                  SDL_Linked_Version()->minor,
                                                  SDL_Linked_Version()->patch);
#ifdef CRRC_DATA_PATH
    printf("Configured data path: %s\n", CRRC_DATA_PATH);
#endif

    {
      std::vector<std::string> search_path;
      
      FileSysTools::getSearchPathList(search_path, "");
      std::cout << "Data file search path:" << std::endl;
      for (std::vector<std::string>::size_type i = 0; i < search_path.size(); i++)
      {
        std::cout << "  " << search_path[i] << std::endl;
      }
    }

    initializeRandomNumberGenerator();

    {
      unsigned int SDLFlags = SDL_INIT_JOYSTICK | SDL_INIT_TIMER;
      int nRetCodeCmdline;
      int i;

      try
      {
        // ***** Read configuration, parse commandline... ***********************
        // -g is the only flag that needs handling before T_Config construction.
        // Use the smol-filtered argv so getopt-permuted argv from earlier passes
        // does not affect the search. (smol flags excluded — they don't carry -g.)
        for (i = 1; i < filtered_argc - 1; i++)
        {
          if (!strcmp(filtered_argv_ptr[i], "-g"))
            T_Config::putConfigFilePath(filtered_argv_ptr[i+1]);
        }

        cfg = new T_Config(cfgfile); // This will also set up cfgfile
        cfg->read(cfgfile);
        
        {
          int nNum = air_to_xml();
          if (nNum != 0)
          {
            // todo: files have been converted; issue message.
          }
        }
                    
        Global::inputDev->init(cfgfile);
        fdmenv = new CRRC_FDM_Env(cfgfile);  
        
        // command line options override settings read from the config file
        // (smol SIL flags filtered out — crrc_checkopts uses getopt and would
        //  reject them as unknown options).
        nRetCodeCmdline = crrc_checkopts(filtered_argc, filtered_argv_ptr, cfgfile, cfg);

        if (nRetCodeCmdline)
          crrc_exit(CRRC_EXIT_FAILURE);

        // must be after crrc_checkopts because crrc_checkopts can change
        //   video.enabled and sound.enabled based on command line options
        if (cfgfile->getInt("video.enabled", 1))
          SDLFlags |= SDL_INIT_VIDEO;
        SDL_Init(SDLFlags);
        signal(SIGINT, sigint_handler);
        SDL_EnableUNICODE(1); // We need this to pass keys to pui
        SDL_EnableKeyRepeat(50, 150);
        
        
        Global::recorder = new FlightRecorder(FileSysTools::getHomePath());
        Global::robots = new Robots();
        
        read_config_into_globals();

        // CLI overrides config-file values (applied after read_config_into_globals).
        if (cli_no_realtime)  Global::realtime_throttle = false;
        if (cli_set_rng_seed) Global::rng_seed = cli_rng_seed;
        if (cli_set_duration) Global::duration_sec = cli_duration;
        if (cli_set_port)     Global::command_port = cli_port;
        if (cli_headless)     cfgfile->setAttributeOverwrite("video.enabled", 0);
        if (cli_launch_mode && !strcmp(cli_launch_mode, "hand")) {
          Global::hand_launch_mode = true;
        }
        if (Global::realtime_throttle == false && Global::duration_sec <= 0.0f) {
          fprintf(stderr,
                  "WARNING: --no-realtime without --duration: CRRCSim will run "
                  "unbounded as fast as possible until SIGINT.\n");
        }
        if (Global::rng_seed == 0) {
          Global::rng_seed = static_cast<uint32_t>(time(nullptr));
        }
        printf("smol SIL config: realtime_throttle=%d rng_seed=%u "
               "duration_sec=%.1f command_port=%u\n",
               Global::realtime_throttle, Global::rng_seed,
               Global::duration_sec, Global::command_port);

        StartSimCommandListener(Global::command_port);

        std::string msg = reconfigureInputMethod();
        if (msg.length())
          printf("%s", msg.c_str());

        // ***** Video setup ****************************************************
        if (cfgfile->getInt("video.enabled", 1))
        {
          Video::setupScreen(0, 0, 0);
        }
        
        // ***** Setting window caption *****************************************
        if (cfgfile->getInt("video.enabled", 1))
          Video::setWindowTitleString();

        Global::soundserver = (CRRCAudioServer*)0;

        // ***** Video **********************************************************

        if (cfgfile->getInt("video.enabled", 1))
          Video::initialize_scenegraph();
        Global::scenery  = new SceneryNull();/*Temporarily an empty scenery.
                    It allows to have a graphic context to display a message
                    during the load of the configured scenery.*/

        Init_mod_windfield();

        player_pos = Global::scenery->getPlayerPosition();
        // Create invisible GUI
        if (cfgfile->getInt("video.enabled", 1))
          Global::gui = new CGUIMain(false);
        else
          Global::gui = NULL;
        
        //initialise generic game mode
        Global::gameHandler= new T_GameHandler();
  
        // load airplane
        {
          bool airplane_failed = false;
          try
          {
            // load the airplane specified in the config file
            loadAirplane();
            initialize_flight_model();
          }
          catch (std::runtime_error& e)
          {
            fprintf(stderr, "%s\n", e.what());
            airplane_failed = true;
          }
          if (airplane_failed)
          {
            // Failed to load airplane file.
            // Using some fallback.
            cfgfile->setAttributeOverwrite("airplane.file", FileSysTools::getDataPath("models/allegro.xml"));
            try
            {
              loadAirplane();
              initialize_flight_model();
            }
            catch (std::runtime_error& e)
            {
              std::string s = "Unable to load airplane file:\n";
              s += e.what();
              fprintf(stderr, "%s\n", s.c_str());
              crrc_exit(CRRC_EXIT_FAILURE, s.c_str());
            }
          }
        }
      }
      catch (XMLException e)
      {
        std::string s = "XMLException: ";
        s += e.what();
        fprintf(stderr, "%s\n", s.c_str());
        crrc_exit(CRRC_EXIT_FAILURE, s.c_str());
      }
    }
      
    if (Global::soundserver != (CRRCAudioServer*)0)
    {
      Global::soundserver->pause(false);
    }
    Global::Simulation->reset();
    
    //~ nVerbosity = 3;
    crrc_time = new CTime(cfgfile);
    
    if (cfgfile->getInt("video.enabled", 1))
      Video::initConsole();
    
#ifdef LOG_FRAMES
    FILE* fp = fopen("frames.dat", "w");
    if (fp == NULL)
    {
      crrc_exit(CRRC_EXIT_FAILURE, "Failed.");
    }
#endif
   //load configured scenery or default scenery
    if (cfgfile->getInt("video.enabled", 1))
    {
      load_initial_scenery(cfg);
      cfg->read(cfgfile);
      Video::setWindowTitleString();
    }
    player_pos = Global::scenery->getPlayerPosition();
    Init_mod_windfield();
    
    //initialise f3f game mode
     if (cfgfile->getInt("game.f3f.enabled",0))
     {
       delete Global::gameHandler;
       Global::gameHandler= new HandlerF3F();
     }  
        
    Global::Simulation->reset();
    
    Scheduler scheduler;
    EventHandler eventHandler(&scheduler);

    Global::TXInterface->getInputData(&Global::inputs);

    Global::unlockFDM();

    while (Global::Simulation->getState() != STATE_EXIT)
    {
      // Headless mode: FDM thread handles physics + sensor I/O.
      // Main loop just sleeps until exit (SIGINT).
      if (!Global::gui)
      {
        if (Global::realtime_throttle) {
          SDL_Delay(100);
        }
        continue;
      }

      crrc_time->update();
      scheduler.Run();

      raiseInputEvent(Global::inputs);

      if (Global::training_mode)
      {
        Global::inputs.heli_fixed_z = -Global::scenery->getPlayerPosition().r[1];
      }
      else
      {
        Global::inputs.heli_fixed_z = EOM01_FIXED_Z_OFF;
      }

      Global::Simulation->doIdle(&Global::inputs);

      Global::inputs.ClearKeys();

      // random data
      {
        CRRC_Random::insertData(SDL_GetTicks());
        CRRC_Random::insertData(Global::inputs.getRandNum());
      }

      // get aircraft position from FDM
      CRRCMath::Vector3 vFdmPos = Global::aircraft->getPos();
      CRRCMath::Vector3 vAircraftPos(     vFdmPos.r[0],
                                     -1 * vFdmPos.r[2],
                                          vFdmPos.r[1]);
      float  distance_to_model = (vAircraftPos - player_pos).length();

      field_of_view = zoom_calc(distance_to_model);
      if (Global::gui)
      {
        Video::adjust_zoom(field_of_view);
      }
      calc_fps();
      
      #if 0
      Global::verboseString += " X: " + ftoStr(vFdmPos.r[0], 2, 2, true, false);
      Global::verboseString += " Y: " + ftoStr(vFdmPos.r[1], 2, 2, true, false);
      Global::verboseString += " Z: " + ftoStr(vFdmPos.r[2], 2, 2, true, false);
      Global::verboseString += " Phi: " + ftoStr(Global::aircraft->getFDM()->getPhi() * SG_RADIANS_TO_DEGREES, 2, 2, true, false);
      Global::verboseString += " Theta: " + ftoStr(Global::aircraft->getFDM()->getTheta() * SG_RADIANS_TO_DEGREES, 2, 2, true, false);
      Global::verboseString += " Psi: " + ftoStr(Global::aircraft->getFDM()->getPsi() * SG_RADIANS_TO_DEGREES, 2, 2, true, false);
      if (Global::gui)
        Global::gui->setVerboseText(Global::verboseString.c_str());
      #else
      switch (Global::nVerbosity)
      {
       case 3:
        Global::verboseString += "FPS: " + itoStr(Global::nFPS, ' ', 1) + " ";
        //fallthrough
       case 2:
        Global::verboseString += "FoV: " + ftoStr(field_of_view, 2, 1, false, false);
        //fallthrough
       case 1:
        {
          int NrOfMixers = T_TX_Mixer::NUM_MIXERS;
          int mixer_on = false;
          std::string drate = "OFF";
          std::string mixers = "";
          
          if (Global::TXInterface->mixer && Global::TXInterface->mixer->enabled)
          {
            if (Global::TXInterface->mixer->dr_enabled)
              drate = "ON";
              
            for (int n=0; n<NrOfMixers; n++)
              if (Global::TXInterface->mixer->mixer_enabled[n])
              {
                if (mixer_on)
                  mixers += ",";
                mixers += itoStr(n+1, ' ', 1);
                mixer_on = true;
              }
          }
          if (!mixer_on)
            mixers += "-"; 

          Global::verboseString += 
              "\nAil: " + ftoStr(Global::inputs.aileron,  2, 2, true, false)
            + " Ele: "  + ftoStr(Global::inputs.elevator, 2, 2, true, false)
            + " Rud: "  + ftoStr(Global::inputs.rudder,   2, 2, true, false)
            + " Thr: "  + ftoStr(Global::inputs.throttle, 2, 2, true, false)
            + " | D/r: " + drate
            + "\nFlp: " + ftoStr(Global::inputs.flap,     2, 2, true, false)
            + " Spo: "  + ftoStr(Global::inputs.spoiler,  2, 2, true, false)
            + " Ret: "  + ftoStr(Global::inputs.retract,  2, 2, true, false)
            + " Pit: "  + ftoStr(Global::inputs.pitch,    2, 2, true, false)
            + " | Mix: " + mixers;
        }
        if (Global::gui)
          Global::gui->setVerboseText(Global::verboseString.c_str());
        else
        {
          static int verbose_print_c = 0;
          if (++verbose_print_c >= 30)
          {
            Global::verboseString += "\n";
            std::cout << Global::verboseString;
            verbose_print_c = 0;
          }
        }
        break;

       default:
        if (Global::gui)
        {
          Global::gui->setVerboseText("");
        }
        break;
      }
      #endif

      if (Global::gui)
      {
          Global::gui->doHUDCompass(field_of_view);
      }
      
      if (Global::gui)
      {
        Video::display();
      }
      Global::verboseString = "";

#ifdef LOG_FRAMES
      fprintf(fp, "%lu\n", (unsigned long)SDL_GetTicks());
#endif
      
      // sound calculations
      if (Global::soundserver != (CRRCAudioServer*)0)
      {
        soundUpdate3D(distance_to_model,
                      Global::aircraft->getFDM()->getPropFreq(),
                      -1*vFdmPos.r[2],
                      Global::aircraft->getFDM()->getVRelAirmass()/Global::aircraft->getFDM()->getTrimmedFlightVelocity());
      }
    }
#ifdef LOG_FRAMES
    fclose(fp);
#endif
    
    Global::recorder->Stop();
  }
  catch (std::exception& e)
  {
    std::string s = "Caught exception: ";
    s += e.what();
    crrc_exit(CRRC_EXIT_FAILURE, s.c_str());
  }

  fdm_thread_running = 0;
  pthread_join(fdm_thread_id, NULL);

  delete fdmenv;
  if (vario_sound != (T_VariometerSound*)0)
  {
    Global::soundserver->stopChannel(vario_sound_channel);
    delete vario_sound;
  }
  delete Global::aircraft;
  delete Global::scenery;
  Video::cleanup();
  if (Global::soundserver != (CRRCAudioServer*)0)
  {
    delete Global::soundserver;
  }
  delete Global::Simulation;

  crrc_exit(CRRC_EXIT_SUCCESS, NULL);

  // crrc_exit() will never return, keep the compiler happy anyway:
  return 0;
}

