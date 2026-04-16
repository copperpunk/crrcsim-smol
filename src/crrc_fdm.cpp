/*
 * CRRCsim - the Charles River Radio Control Club Flight Simulator Project
 *
 * Copyright (C) 2008-2009 Jens Wilhelm Wulf (original author)
 * Copyright (C) 2008 Jan Reucker
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
  

#include "crrc_fdm.h"

#include "global.h"
#include "mod_landscape/crrc_scenery.h"
#include "mod_windfield/windfield.h"
#include "mod_env/earth/atmos_62.h"
#include "mod_env/earth/ls_gravity.h"
#include "mod_fdm/fdm.h"

CRRC_FDM_Env::CRRC_FDM_Env(SimpleXMLTransfer* cfg)
{
  // instantiate list of controllers from global config file,
  // so these controllers are used no matter which model is loaded
  controllers.clear();  
  int idx = cfg->indexOfChild("controllers");
  if (idx >= 0)
    Controller::LoadList(cfg->getChildAt(idx), controllers);
}

float CRRC_FDM_Env::GetSceneryHeight(float x_north, float y_east)
{
  float terrain_height = Global::scenery->getHeight(x_north, y_east);
  if (Global::hand_launch_mode && !Global::hand_launch_thrown)
  {
    const float dx = x_north - static_cast<float>(Global::hand_launch_posX);
    const float dy = y_east  - static_cast<float>(Global::hand_launch_posY);
    const float r2 = Global::hand_launch_platform_radius_ft
                   * Global::hand_launch_platform_radius_ft;
    if (dx * dx + dy * dy < r2)
    {
      const float platform_top = terrain_height + Global::hand_launch_platform_height_ft;
      return platform_top;
    }
  }
  return terrain_height;
}

int CRRC_FDM_Env::CalculateWind(double  X_cg,      double  Y_cg,     double  Z_cg,
                                double& Vel_north, double& Vel_east, double& Vel_down)
{
  return(calculate_wind(X_cg,      Y_cg,     Z_cg,
                        Vel_north, Vel_east, Vel_down));
}

double CRRC_FDM_Env::GetRho(double altitude)
{
  double origin_altitude = Global::scenery->getOriginAltitude();
  return(ls_atmos_rho(altitude + origin_altitude));
}

double CRRC_FDM_Env::GetG(double altitude)
{
  return(ls_gravity_g(altitude));
}

void CRRC_FDM_Env::ControllerCallback(double      dt, 
                                      FDMBase*    fdm,
                                      TSimInputs* pInputsFromUser,
                                      TSimInputs* pInputsToFDM)
{
  pInputsToFDM->CopyFrom(pInputsFromUser);

  // Process controllers
  for (unsigned int n=0; n<controllers.size(); n++)
    controllers[n]->Calc(dt, fdm, pInputsFromUser, pInputsToFDM);
}

void CRRC_FDM_Env::ResetControllers()
{
  for (unsigned int n=0; n<controllers.size(); n++)
    controllers[n]->Reset();
}

CRRC_FDM_Env::~CRRC_FDM_Env()
{
  for (unsigned int n=0; n<controllers.size(); n++)
    delete controllers[n];
}

void CRRC_FDM_Env::AddLogMsg(std::string message)
{
  LOG(message);
}
