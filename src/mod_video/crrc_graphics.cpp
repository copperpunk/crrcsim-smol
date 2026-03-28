/*
 * CRRCsim - the Charles River Radio Control Club Flight Simulator Project
 *
 * Copyright (C) 2005-2010 Jan Reucker (original author)
 * Copyright (C) 2005, 2006, 2007, 2008 Jens Wilhelm Wulf
 * Copyright (C) 2008 Olivier Bordes
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
  

/** \file crrc_graphics.cpp
 *
 *  Functions dealing with graphics.
 */

#include <errno.h>
#include "../i18n.h"
#include "../global.h"
#include "../aircraft.h"
#include "../SimStateHandler.h"
#include "../mod_mode/T_GameHandler.h"
#include "crrc_graphics.h"
#include "../crrc_main.h"
#include "../GUI/crrc_gui_main.h"
#include "../mod_inputdev/inputdev_audio/inputdev_audio.h"
#include "../mod_windfield/windfield.h"
#include "../crrc_loadair.h"
#include "../mod_misc/lib_conversions.h"
#include "../crrc_system.h"
#include "../defines.h"
#include "../mod_landscape/crrc_scenery.h"
#include "crrc_sky.h"
#include "glconsole.h"
#include "gloverlay.h"
#include "../zoom.h"
#include "fonts.h"

// Debug and error handling settings
#define DONT_REPEAT_GL_ERRORS  1

namespace Video
{

// Flags for SDL video mode
Uint32 SDL_video_flags;

#define LIGHT_X (1000.0f / 1.72)
#define LIGHT_Y (8000.0f / 1.72)
#define LIGHT_Z (-1000.0f / 1.72)
ssgRoot               *scene    = NULL ;
static ssgContext     *context  = NULL ;
sgVec3                lightposn = {LIGHT_X, LIGHT_Y, LIGHT_Z};

int window_xsize;  // Size of window in x direction
int window_ysize;  // Size of window in y direction
int screen_xsize;  // Size of screen in x direction
int screen_ysize;  // Size of screen in y direction

float shadow_matrix[4][4];  // Matrix used to calculate shadow on ground
float slope_shadow_matrix[4][4];  // Matrix used to calculate shadow on slope
float top_shadow_matrix[4][4];  // Matrix used to calculate shadow on slope

T_VideoBitDepthInfo vidbits;

/**
 * Sloppy camera setting
 */
float flSloppyCam = 0;

/**
 * Position where the player is looking at (may be different
 * from position of plane; needed for sloppy camera).
 */
CRRCMath::Vector3 looking_pos;

CameraMode camera_mode = CAM_PILOT;
static CRRCMath::Vector3 saved_pilot_pos;
static CRRCMath::Vector3 camera_up(0, 1, 0);
static float chase_behind = 10.0;
static float chase_above  = 3.0;

/**
 * The console overlay
 */
static GlConsole* console = NULL;


/** \brief get a pointer to the global rendering context
 *
 * \return pointer to global rendering context
 */
ssgContext* getGlobalRenderingContext()
{
  return context;
}


/** \brief Dump some info about the current state of the stacks
 *
 *  \param pFile the stream to print the info to
 */
void dumpGLStackInfo(FILE* pFile)
{
  GLint mv_cur, mv_max;
  GLint pr_cur, pr_max;
  
  glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &mv_cur);
  glGetIntegerv(GL_PROJECTION_STACK_DEPTH, &pr_cur);
  glGetIntegerv(GL_MAX_MODELVIEW_STACK_DEPTH, &mv_max);
  glGetIntegerv(GL_MAX_PROJECTION_STACK_DEPTH, &pr_max);
  
  fprintf(pFile, "Modelview stack: %d / %d    Projection stack: %d / %d\n",
          mv_cur, mv_max, pr_cur, pr_max);
}


/** \brief Evaluate any pending OpenGL error 
 *
 *  Prints an error message if the OpenGL state machine
 *  error flag is set to any value other than GL_NO_ERROR.
 *  
 *  \return true if an OpenGL error occured
 */
bool evaluateOpenGLErrors()
{
  static GLenum lastGLerror = GL_NO_ERROR;
  bool has_error = false;
  GLenum err;

  if ((err = glGetError()) != GL_NO_ERROR)
  {
    #if DONT_REPEAT_GL_ERRORS > 0
    if (err != lastGLerror)
    #endif
    {
      const GLubyte *s = gluErrorString(err);
      if (s != NULL)
      {
        fprintf(stderr, "OpenGL error: %s\n", s);
      }
      Video::dumpGLStackInfo(stderr);
      lastGLerror = err;
    }
    has_error = true;
  }
  return has_error;
}


/*****************************************************************************/
/** \brief Calculate the shadow matrix
 *
 *  The shadow matrix projects the shadow-casting object
 *  onto the "ground" plane (given as Ax + By + Cz + D = 0).
 *
 *  This technique is described in 
 *  http://www.opengl.org/resources/tutorials/advanced/advanced98/notes/notes.html
 *
 *  \param ground (A, B, C, D) of ground plane
 *  \param light  light source position in homogenous coordinates (with w = 0 for a directional light)
 *  \param m Shadow matrix will be copied to this array
 */
static void ShadowMatrix(float ground[4],float light[4],float m[4][4])
{
  float dot;

  dot = ground[0] * light[0] +
        ground[1] * light[1] +
        ground[2] * light[2] +
        ground[3] * light[3];

  m[0][0] = dot - light[0] * ground[0];
  m[1][0] = 0.0 - light[0] * ground[1];
  m[2][0] = 0.0 - light[0] * ground[2];
  m[3][0] = 0.0 - light[0] * ground[3];

  m[0][1] = 0.0 - light[1] * ground[0];
  m[1][1] = dot - light[1] * ground[1];
  m[2][1] = 0.0 - light[1] * ground[2];
  m[3][1] = 0.0 - light[1] * ground[3];

  m[0][2] = 0.0 - light[2] * ground[0];
  m[1][2] = 0.0 - light[2] * ground[1];
  m[2][2] = dot - light[2] * ground[2];
  m[3][2] = 0.0 - light[2] * ground[3];

  m[0][3] = 0.0 - light[3] * ground[0];
  m[1][3] = 0.0 - light[3] * ground[1];
  m[2][3] = 0.0 - light[3] * ground[2];
  m[3][3] = dot - light[3] * ground[3];
}


/*****************************************************************************/
unsigned short getshort(FILE *inf)
{
  unsigned char buf[2];
  
  (void)fread(buf,2,1,inf);
  return (buf[0]<<8)+(buf[1]<<0);
}

/*****************************************************************************/
unsigned short getrgbchar(FILE *inf)
{
  unsigned char buf[1];

  (void)fread(buf,1,1,inf);
  return (buf[0]);
}


/** \brief Read pixel data from an SGI .rgb image.
 *
 *  Reads an .rgb file, allocates memory for the pixels
 *  and sets *w and *h to image width and height.
 *
 *  \param name file name
 *  \param w will be set to image width
 *  \param h will be set to image height
 *  \return pointer to the pixel data or NULL on error
 */
unsigned char * read_rgbimage(const char *name, int *w, int *h)
{
  unsigned char *image, *temp;
  FILE *image_in;
  unsigned char input_char;
  unsigned short int input_short;
  unsigned char header[512];
  unsigned long int loop;

  if ( (image_in = fopen(name, "rb")) == NULL)
  {
    printf("%s\n", name);
    std::string s = "read_rgbimage: Unable to open ";
    s += name;
    perror(s.c_str());
    return NULL;
  }
  input_short=getshort(image_in);
  if (input_short == 0x01da)
  {
    input_char=getrgbchar(image_in);
    if (input_char == 0)
    {
      input_char=getrgbchar(image_in);
      input_short=getshort(image_in);
      if (input_short == 3)
      {
        input_short=getshort(image_in);
        *w=input_short;
        input_short=getshort(image_in);
        *h=input_short;
        image=(unsigned char*)malloc(*w * *h *4 *sizeof(unsigned char));
        temp=(unsigned char*)malloc(*w * *h *sizeof(unsigned char));
        if ((image == NULL) || (temp == NULL))
        {
          fprintf(stderr, "Error allocating memory for %s\n", name);
          // just in case one of the two was malloc'ed correctly:
          free(image);
          free(temp);
          return NULL;
        }
        input_short=getshort(image_in);
        if (input_short == 4)
        {
          (void)fread(header,sizeof(unsigned char),500,image_in);
          (void)fread(temp, sizeof image[0], *w * *h, image_in);
          for (loop=0;loop<(unsigned long int)(*w * *h);loop++)
          {
            image[loop*4+0]=temp[loop];
          }
          (void)fread(temp, sizeof image[0], *w * *h, image_in);
          for (loop=0;loop<(unsigned long int)(*w * *h);loop++)
          {
            image[loop*4+1]=temp[loop];
          }
          (void)fread(temp, sizeof image[0], *w * *h, image_in);
          for (loop=0;loop<(unsigned long int)(*w * *h);loop++)
          {
            image[loop*4+2]=temp[loop];
          }
          (void)fread(temp, sizeof image[0], *w * *h, image_in);
          for (loop=0;loop<(unsigned long int)(*w * *h);loop++)
          {
            image[loop*4+3]=temp[loop];
          }
          free(temp);
          return image;
        }
        else
        {
          std::string s = "Error loading texture ";
          s += name;
          s += ":\nThis file isn't a 4 channel RGBA file.";
          fprintf(stderr, "%s\n", s.c_str());
          return NULL;
        }
      }
      else
      {
        std::string s = "Error loading texture ";
        s += name;
        s += ":\nNot a useable RGB file.";
        fprintf(stderr, "%s\n", s.c_str());
        return NULL;
      }
    }
    else
    {
      std::string s = "Error loading texture ";
      s += name;
      s += ":\nRLE encoded SGI files are not supported.";
      fprintf(stderr, "%s\n", s.c_str());
      return NULL;
    }
  }
  else
  {
    std::string s = "Error loading texture ";
    s += name;
    s += ":\nFile doesn't appear to be an SGI rgb file!";
    fprintf(stderr, "%s\n", s.c_str());
    return NULL;
  }
  return NULL;
}


/**
 * <code>w</code> and <code>h</code> need to be set before calling
 * this function.
 *
 * \todo .bw images are always square, so we can calculate
 * <code>w</code> and <code>h</code> from the number of read bytes.
 */
unsigned char * read_bwimage(const char *name, int *w, int *h)
  // From Skyfly.c, Thanks dude
{
  unsigned char   *image;
  FILE            *image_in;
  int             img_bytes;
  int             bytes_read;

  if ( (image_in = fopen(name, "rb")) == NULL)
  {
    return NULL;
  }

  img_bytes = *w * *h;
  image = (unsigned char *)malloc(sizeof(unsigned char) * img_bytes);

  bytes_read = fread(image, sizeof image[0], img_bytes, image_in);
  fclose(image_in);
  if (bytes_read != img_bytes)
  {
    free(image);
    return NULL;
  }
  return image;
}


/*****************************************************************************/
/** \brief Create a texture from raw pixel data.
 *
 *  Generates an OpenGL texture from raw image data which must
 *  be loaded before using <code>read_XXXimage</code>. The
 *  texture is completely independent of the raw data, so you
 *  can free() the pixel_data after generating the texture.
 *
 *  \param pixel_data   pointer to the raw pixel data
 *  \param pixel_format format of the pixel data (e.g. GL_ALPHA for 8 bpp images,
 *                      GL_RGB for 24 bpp images and GL_RGBA for 32 bpp images)
 *  \param format format of the texture to be generated (e.g. GL_LUMINANCE,
 *                GL_ALPHA, GL_RGBA, ...)
 *  \param width  width of the original image
 *  \param height height of the original image
 *  \param use_mipmaps specify whether or not to generate mipmaps
 *  \return OpenGL texture handle
 */
GLuint make_texture(unsigned char *pixel_data, GLint pixel_format, GLint format,
                    GLsizei width, GLsizei height, bool use_mipmaps)
{
  GLuint tex;

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT,1);
  glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  if (!use_mipmaps)
  {
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, pixel_format, width, height,
                 0, format, GL_UNSIGNED_BYTE, pixel_data);
  }
  else
  {
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, pixel_format, width,
                      height, format, GL_UNSIGNED_BYTE,
                      pixel_data);
  }

  return tex;
}


bool ssgLoadJPG ( const char *fname, ssgTextureInfo* info );

/** \brief Perform the basic scenegraph initialization
 *
 *  Initializes the PLIB SSG library, creates the scenegraph
 *  root, a rendering context and the sun.
 */
void initialize_scenegraph()
{
  // Initialize SSG
  ssgInit();
  
  // add to SSG function for read JPEG Textures 
  ::ssgAddTextureFormat ( ".jpg", ssgLoadJPG);
  
  // font
  puSetDefaultFonts ( FONT_HELVETICA_14, FONT_HELVETICA_14 );
  
  // Some basic OpenGL setup
  sgVec4 skycol;
  sgSetVec4 ( skycol, 0.53f, 0.75f, 0.93f, 1.0f ) ;
  glClearColor ( skycol[0], skycol[1], skycol[2], skycol[3] ) ;

  glEnable ( GL_DEPTH_TEST ) ;

  // Set up the viewing parameters
  context = new ssgContext();
  context->setFOV     ( 35.0f, 0 ) ;
  context->setNearFar ( 1.0f, 10000.0f ) ;
  context->makeCurrent();

  ssgModelPath("");
  ssgTexturePath("textures");  
  
  // Create a root node
  scene    = new ssgRoot();

  // Set up a light source
  sgVec4 lightamb;
  sgSetVec4(lightamb , 0.2f, 0.2f, 0.2f, 1.0f);
  ssgGetLight(0)->setPosition(lightposn);
  ssgGetLight(0)->setColour(GL_AMBIENT, lightamb);

  ssgGetLight(0)->setPosition(lightposn);


}


/** \brief Setup some basic OpenGL stuff
 *
 *  Calculates the shadow matrices for the different locations,
 *  performs some basic OpenGL initialization and creates the
 *  sky sphere object. Most of this stuff will get obsolete
 *  very soon...
 *
 *  \param boFlatShading  use flat shading instead of smooth shading
 */
void initialize_window(bool boFlatShading)
{
  GLfloat mat_specular[]={1.0,1.0,1.0,1.0};
  GLfloat mat_diffuse[]={0.3,0.3,0.3,1.0};
  GLfloat light_position[]={0.0,5000.0,3000.0,0.0};
  GLfloat white_light[]={1.0,1.0,1.0,1.0};
  GLfloat ground[4];
  GLfloat light[4];

  ground[0]=0;
  ground[1]=1;
  ground[2]=0;
  ground[3]=0;

  /// \todo The light initialization seems to depend
  /// on the scenery, but it is only done once at
  /// startup and not after a scenery change.
  if (Global::scenery->getID() == Scenery::CAPE_COD)
  {
    light[0] = 0;
    light[1] = 5000;
    light[2] = 0;
    light[3] = 0.0;
  }
  else
  {
    light[0] = 0;
    light[1] = 5000.;
    light[2] = -3000.;
    light[3] = 0.0;
  }
  ShadowMatrix(ground,light,shadow_matrix);

  ground[0] = -1;
  ground[1] =  1;
  ground[2] =  0;
  ground[3] = -100.1;
  ShadowMatrix(ground,light,slope_shadow_matrix);

  ground[0] = 0;
  ground[1] = 1;
  ground[2] = 0;
  ground[3] = -100.1;
  ShadowMatrix(ground,light,top_shadow_matrix);

  glClearColor(0.53,0.75,0.93,1.0);
  if (boFlatShading)
  {
    glShadeModel(GL_FLAT);
    printf("Using flat shading.\n");
  }
  glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
  glMaterialfv(GL_FRONT,GL_SPECULAR,mat_specular);
  glMaterialfv(GL_FRONT,GL_DIFFUSE,mat_diffuse);
  glLightfv(GL_LIGHT0,GL_POSITION,light_position);
  glLightfv(GL_LIGHT0,GL_DIFFUSE,white_light);
  glLightfv(GL_LIGHT0,GL_SPECULAR,white_light);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
}


void adjust_zoom(float field_of_view)
{
  glViewport(0,0,(GLsizei)window_xsize,(GLsizei)window_ysize);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(field_of_view,
                 (GLfloat)window_xsize/(GLfloat)window_ysize,3,9500.0);
  glMatrixMode(GL_MODELVIEW);
  
  context->setFOV(field_of_view,field_of_view * window_ysize/ window_xsize);
}


/***********************************************/
#define NPT_OSCILLO 500
void oscillo()
{
#if PORTAUDIO > 0
  // TEST_MODE
  int i;

#if 0
  glDisable(GL_LIGHTING);
  glMatrixMode (GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity ();
  gluOrtho2D (0, window_xsize-1, 0, window_ysize);
#endif
  glTranslatef(10,10,0);
  glColor3f (0.1, 0.2, 0.2); //oscillo border
  glRectf(-5,-5,NPT_OSCILLO+5,200+5);
  glTranslatef(0,0,0.1);
  glColor3f (0, 0, 0);  //oscillo screen
  glRectf(0,0,NPT_OSCILLO,200);
  glTranslatef(0,0,0.1);

  glColor3f (1, 0., 0.); //oscillo border
  glBegin(GL_LINE_LOOP);
  glVertex2i(0,0);
  glVertex2i(0,200);
  glVertex2i(NPT_OSCILLO,200);
  glVertex2i(NPT_OSCILLO,0);
  glEnd();
  glColor3f (0, 1, 0.);
  glBegin(GL_LINE_STRIP);
  for(i = 0; i < NPT_OSCILLO; i++)
  {
    //draw curve
    int v;
    v = (int)( 100+ 100. * get_audio_signal(i));
    glVertex2i(i,v);
  }
  glEnd();
#if 0
  glPopMatrix();
  glEnable(GL_LIGHTING);
#endif

#endif
}


void makeShadowMatrix(sgMat4 sm, float x, float y)
{
  sgVec4  plane, light;
  (void)Global::scenery->getHeightAndPlane(x, y, plane);

  light[0] = lightposn[0];
  light[1] = lightposn[1];
  light[2] = lightposn[2];
  light[3] = 0;

  sgNormaliseVec4(light);
  ShadowMatrix(plane, light, sm);
}

/**
 * Converts a vector from FDM to graphics representation
 */
CRRCMath::Vector3 FDM2Graphics(CRRCMath::Vector3 const& v)
{
  return CRRCMath::Vector3(v.r[1], -v.r[2], -v.r[0]);
}


/*****************************************************************************/
/** \brief The per-frame OpenGL display routine
 *
 *  This function does the complete OpenGL drawing. First
 *  the 3D scene is drawn, then some 2D overlays
 *  (wind and battery indicator, GUI).
 */
void display()
{
  CRRCMath::Vector3 plane_pos = FDM2Graphics(Global::aircraft->getPos());

  // Prepare the current frame buffer and reset
  // the modelview matrix (for non-SSG drawing)
  GLbitfield clearmask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
   if (vidbits.stencil)
    {
    clearmask |= GL_STENCIL_BUFFER_BIT;
    }
  glClear(clearmask);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glPushMatrix();
  //~ glBlendFunc(GL_ONE, GL_ZERO);
  //~ glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

  // Set up the viewing transformation (for SSG drawing)
  sgVec3 viewpos, planepos, up;
  
  sgSetVec3(viewpos, player_pos.r[0], player_pos.r[1], player_pos.r[2]);                
  sgSetVec3(planepos, looking_pos.r[0], looking_pos.r[1], looking_pos.r[2]);
  sgSetVec3(up, camera_up.r[0], camera_up.r[1], camera_up.r[2]);
  context->setCameraLookAt(viewpos, planepos, up);

  // 3D scene
  if( Global::scenery != NULL)
  {
    // 3D scene: sky sphere
    draw_sky(&viewpos, Global::Simulation->getTotalTime());
  
    // 3D scene: airplane
    if (Global::aircraft->getModel() != NULL)
    {
      // For SSG rendering, this call does not draw anything,
      // but calculates the airplane's transformation matrix
      glDisable(GL_TEXTURE_2D);
      glEnable(GL_LIGHTING);
      glEnable(GL_LIGHT0);
      Global::aircraft->getModel()->draw(Global::aircraft->getFDM());
    }
  
    // 3D scene: scenery
    Global::scenery->draw(Global::Simulation->getTotalTime());
  }
  // Lighting setup for SSG scene graph
  sgVec4 lightamb, lightdif;
  sgSetVec4(lightamb, 0.4f, 0.4f, 0.4f, 1.0f);
  sgSetVec4(lightdif, 0.8f, 0.8f, 0.8f, 1.0f);
  ssgGetLight(0)->setPosition(lightposn);
  ssgGetLight(0)->setColour(GL_AMBIENT, lightamb);
  ssgGetLight(0)->setColour(GL_DIFFUSE, lightdif);
  ssgGetLight(0)->setColour(GL_SPECULAR, lightdif);

  // Draw the scenegraph (airplane model, shadow)
  ssgCullAndDraw(scene);
  context->forceBasicState();

  // ssgCullAndDraw() ends up with an identity modelview matrix,
  // so we have to set up our own viewing transformation for
  // the thermals
  glLoadIdentity();
  gluLookAt(player_pos.r[0], player_pos.r[1], player_pos.r[2],
            looking_pos.r[0], looking_pos.r[1], looking_pos.r[2],
            camera_up.r[0], camera_up.r[1], camera_up.r[2]);
  
  if (Global::training_mode==TRUE)
  {
    draw_thermals(Global::aircraft->getPos());
  }
  
  // 3D scene: game-mode-specific stuff (pylons etc.)
  Global::gameHandler->draw();

  glPopMatrix();


  // Overlay: game handler
  Global::gameHandler->display_infos(window_xsize, window_ysize);

  // Overlay: scope for audio interface
  if ( Global::testmode.test_mode
       &&
       (Global::TXInterface->inputMethod() == T_TX_Interface::eIM_audio) )
  {
    GlOverlay::setupRenderingState(window_xsize, window_ysize);
    oscillo();
    GlOverlay::restoreRenderingState();
  }

  // Overlay: wind direction indicator
  {
    double dx  = (plane_pos.r[2] - player_pos.r[2]);
    double dy  = (player_pos.r[0] - plane_pos.r[0]);
    double dir = atan2(dy, dx);

    GlOverlay::setupRenderingState(window_xsize, window_ysize);
    draw_wind(dir);
    GlOverlay::restoreRenderingState();
  }

  // Overlay: battery capacity/fuel left
  {
    int r   = window_ysize >> 5;
    int w   = r >> 1;
    int h   = window_ysize >> 3;
    int ht  = (int)(Global::aircraft->getFDM()->getBatCapLeft() * h);
                    
#if 0
    glDisable(GL_LIGHTING);
    glMatrixMode (GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity ();
    gluOrtho2D (0, window_xsize-1, 0, window_ysize);
#endif
    GlOverlay::setupRenderingState(window_xsize, window_ysize);
    
    // Background
    glColor3f (0, 0, 0);
    glRectf(window_xsize-w, r+ht,
            window_xsize-1, r+h);
    glTranslatef(0,0,0.1);
  
    // Indicator
    glColor3f (0, 1, 0.);
    glRectf(window_xsize-w, r,
            window_xsize-1, r+ht);
    
#if 0
    glPopMatrix();
    glEnable(GL_LIGHTING);    
    glMatrixMode(GL_MODELVIEW);
#endif
    GlOverlay::restoreRenderingState();

  }

  // Overlay: flight instruments (top-left)
  {
    GlOverlay::setupRenderingState(window_xsize, window_ysize);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double phi   = Global::aircraft->getFDM()->getPhi();
    double theta = Global::aircraft->getFDM()->getTheta();
    CRRCMath::Vector3 vel = Global::aircraft->getFDM()->getVel();
    double alt_ft = Global::aircraft->getFDM()->getAlt();
    double alt_m  = alt_ft * 0.3048;
    double gnd_speed_ft = sqrt(vel.r[0]*vel.r[0] + vel.r[1]*vel.r[1]);
    double gnd_speed_ms = gnd_speed_ft * 0.3048;
    double cog_deg = atan2(vel.r[1], vel.r[0]) * 180.0 / M_PI;
    if (cog_deg < 0) cog_deg += 360.0;

    int margin = 10;
    int ai_size = 80;
    int ai_cx = margin + ai_size;
    int ai_cy = window_ysize - margin - ai_size;

    // Attitude indicator background
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
    glRectf(ai_cx - ai_size, ai_cy - ai_size,
            ai_cx + ai_size, ai_cy + ai_size);

    // Clip to the AI square
    glEnable(GL_SCISSOR_TEST);
    glScissor(ai_cx - ai_size, ai_cy - ai_size, ai_size * 2, ai_size * 2);

    glPushMatrix();
    glTranslatef(ai_cx, ai_cy, 0);

    // Rotate for roll
    float roll_deg = phi * 180.0 / M_PI;
    glRotatef(roll_deg, 0, 0, 1);

    // Sky/ground split offset by pitch
    float pitch_px = -theta * (180.0 / M_PI) * (ai_size / 30.0);

    // Sky (blue)
    glColor3f(0.3f, 0.5f, 0.8f);
    glRectf(-ai_size, pitch_px, ai_size, ai_size * 2);

    // Ground (brown)
    glColor3f(0.45f, 0.35f, 0.2f);
    glRectf(-ai_size, -ai_size * 2, ai_size, pitch_px);

    // Horizon line
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(-ai_size, pitch_px);
    glVertex2f(ai_size, pitch_px);
    glEnd();

    // Pitch ladder (10-degree marks)
    glLineWidth(1.0f);
    for (int p = -30; p <= 30; p += 10)
    {
      if (p == 0) continue;
      float y = pitch_px - p * (ai_size / 30.0);
      float hw = (p % 20 == 0) ? 20.0f : 12.0f;
      glBegin(GL_LINES);
      glVertex2f(-hw, y);
      glVertex2f(hw, y);
      glEnd();
    }

    glPopMatrix();
    glDisable(GL_SCISSOR_TEST);

    // Aircraft reference (fixed wings + dot)
    glColor3f(1.0f, 1.0f, 0.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex2f(ai_cx - 25, ai_cy);
    glVertex2f(ai_cx - 8, ai_cy);
    glVertex2f(ai_cx + 8, ai_cy);
    glVertex2f(ai_cx + 25, ai_cy);
    glEnd();
    glPointSize(5.0f);
    glBegin(GL_POINTS);
    glVertex2f(ai_cx, ai_cy);
    glEnd();
    glLineWidth(1.0f);

    // Text readouts (right of AI)
    int tx = ai_cx + ai_size + 10;
    int ty = ai_cy + ai_size - 16;
    int line_h = 18;

    glColor3f(0.0f, 1.0f, 0.4f);
    char buf[64];

    if (!Global::flightModeString.empty())
    {
      FONT_HELVETICA_14.drawString(Global::flightModeString.c_str(), tx, ty);
      ty -= line_h;
    }

    ty -= 4;

    snprintf(buf, sizeof(buf), "SPD  %5.1f m/s", gnd_speed_ms);
    FONT_HELVETICA_14.drawString(buf, tx, ty);
    ty -= line_h;

    snprintf(buf, sizeof(buf), "ALT  %5.1f m", alt_m);
    FONT_HELVETICA_14.drawString(buf, tx, ty);
    ty -= line_h;

    snprintf(buf, sizeof(buf), "COG  %5.1f\xb0", cog_deg);
    FONT_HELVETICA_14.drawString(buf, tx, ty);
    ty -= line_h + 4;

    double pitch_deg = theta * 180.0 / M_PI;
    double yaw_deg   = Global::aircraft->getFDM()->getPsi() * 180.0 / M_PI;
    if (yaw_deg < 0) yaw_deg += 360.0;

    snprintf(buf, sizeof(buf), "R  %+6.1f\xb0", (double)roll_deg);
    FONT_HELVETICA_14.drawString(buf, tx, ty);
    ty -= line_h;

    snprintf(buf, sizeof(buf), "P  %+6.1f\xb0", pitch_deg);
    FONT_HELVETICA_14.drawString(buf, tx, ty);
    ty -= line_h;

    snprintf(buf, sizeof(buf), "Y  %6.1f\xb0", yaw_deg);
    FONT_HELVETICA_14.drawString(buf, tx, ty);

    glDisable(GL_BLEND);
    GlOverlay::restoreRenderingState();
  }

  // Overlay: console
  console->render(window_xsize, window_ysize);

  // Overlay: gui
  Global::gui->draw();
  
  // check for any OpenGL errors
  evaluateOpenGLErrors();

  // Force pipeline flushing and flip front and back buffer
  glFlush();
  SDL_GL_SwapBuffers();
}


/*****************************************************************************/
#if 0
// This was a test for Win32, didn't work...
void rebuild_video_context(int w, int h)
{
  printf("rebuilding context in %d x %d\n", w, h);
  if (SDL_WasInit(SDL_INIT_VIDEO))
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
  {
    crrc_exit(CRRC_EXIT_FAILURE, SDL_GetError());
  }

  printf("Setting up new surface...\n");
  setupScreen(w, h, cfgfile->getInt("video.fullscreen.fUse", 0));
  delete scenery;
  //  delete SkySphere;
  printf("Setting up new Sky...\n");
  SkySphere = new CSkySphere(8000.0,
                             cfgfile->getInt("video.textures.fUse_textures", 1));
  printf("Setting up new scenery...\n");
  scenery = new BuiltinScenery(cfg->location());
  if (read_airplane_file(cfgfile->getString("airplane.file", "models/allegro.xml").c_str()))
  {
    // Failed to load airplane file.
    // Using some fallback.
    cfgfile->setAttributeOverwrite("airplane.file", FileSysTools::getDataPath("models/allegro.xml"));

    if (read_airplane_file(cfgfile->getString("airplane.file").c_str()))
    {
      // The fallback failed, too. Abort.
      fprintf(stderr, "Unable to load airplane file %s\n", cfgfile->getString("airplane.file").c_str()));
      crrc_exit(CRRC_EXIT_FAILURE, "Unable to load airplane file");
    }
  }
  setWindowTitleString();
  initialize_window(cfg);
}
#endif


/** \brief Add an OpenGL string to a std::string
 *
 *  Reads a predefined OpenGL info string and adds
 *  it to the string or adds "<unknown>" if the
 *  result was NULL.
 *
 *  \param s String
 *  \param which Symbolic name of the GL string
 */
void AddGLString(std::string& s, GLenum which)
{
  const GLubyte *str;
  
  str = glGetString(which);
  if (str != NULL)
  {
    s += (const char *)str;
  }
  else
  {
    s += "<unknown>";
  }
}


/** \brief Get info string for current video mode
 *
 *  This function returns a std::string containing
 *  some information about the current video context,
 *  e.g. driver information, bpp for different buffers...
 *
 *  \param indent string to be applied at the beginning of each line
 *  \return video mode info string
 */
std::string GetVideoInfoString(const char *indent)
{
  std::string s;
  
  s += indent;
  s += "Renderer:    ";
  AddGLString(s, GL_RENDERER);
  s += "\n";
  s += indent;
  s += "Vendor:      ";
  AddGLString(s, GL_VENDOR);
  s += "\n";
  s += indent;
  s += "GL version:  ";
  AddGLString(s, GL_VERSION);
  s += "\n";
  s += indent;
  s += "RGBA bpp:    ";
  s += itoStr(vidbits.red, ' ', 0);
  s += "/";
  s += itoStr(vidbits.green, ' ', 0);
  s += "/";
  s += itoStr(vidbits.blue, ' ', 0);
  s += "/";
  s += itoStr(vidbits.alpha, ' ', 0);
  s += "\n";
  s += indent;
  s += "Depth bpp:   ";
  s += itoStr(vidbits.depth, ' ', 0);
  s += "\n";
  s += indent;
  s += "Stencil bpp: ";
  s += itoStr(vidbits.stencil, ' ', 0);
  s += "\n";
  
  return s;
}


SDL_Surface* video_setup_colordepth(int& nX, int& nY, int& nFullscreen, int color_depth)
{
  SDL_Surface* screen;

  int cbits = (color_depth <= 16) ?  5 :  8;
  int zbits = (color_depth <= 16) ? 16 : 24;

  /* setup GL attributes */
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, cbits);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, cbits);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, cbits);
//  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, cbits);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, zbits);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  // Load defaults?
  if (nX < 100 || nY < 100)
  {
    SimpleXMLTransfer* item;
    nFullscreen = cfgfile->getInt("video.fullscreen.fUse", 0);
    if (nFullscreen)
    {
      item = cfgfile->getChild("video.resolution.fullscreen", true);
      nX = item->getInt("x", 0);// "0" : auto resolution
      nY = item->getInt("y", 0);// "0" : auto resolution
    }
    else
    {
      item = cfgfile->getChild("video.resolution.window", true);
      nX = item->getInt("x", 800);
      nY = item->getInt("y", 600);
    }

    printf("Loading default videomode from config...\n");
  }

  /* setup SDL fullscreen flag */
  if (nFullscreen)
  {
    SDL_video_flags |= SDL_FULLSCREEN;
    SDL_video_flags &= ~SDL_RESIZABLE;
  }
 else
  {
    SDL_video_flags &= ~SDL_FULLSCREEN;
    SDL_video_flags |= SDL_RESIZABLE;
  }
// try to setup...

  screen = SDL_SetVideoMode(nX, nY, color_depth, SDL_video_flags);

  // If that one failed, take window mode from config file:
  if (!screen)
  {
    printf("Failed to setup videomode %ix%i, fullscreen=%i\n", nX, nY, nFullscreen);
    nX = cfgfile->getInt("video.resolution.window.x", 800);
    nY = cfgfile->getInt("video.resolution.window.y", 600);
    nFullscreen = 0;
    SDL_video_flags &= ~SDL_FULLSCREEN;
    screen = SDL_SetVideoMode(nX, nY, color_depth, SDL_video_flags);
  }

  // Did that one fail, too?
  if (!screen)
  {
    printf("Failed to setup videomode %ix%i, fullscreen=%i\n", nX, nY, nFullscreen);
    SDL_video_flags &= ~SDL_FULLSCREEN;
    if (nFullscreen)
    {
      SDL_video_flags |= SDL_FULLSCREEN;
    }
    nX = 640;
    nY = 480;
    screen = SDL_SetVideoMode(nX, nY, color_depth, SDL_video_flags);
  }

  return(screen);
}

int setupScreen(int nX, int nY, int nFullscreen)
{
  int resolution_auto = 0;
  if((nX==0)&&(nY==0)&&(nFullscreen==0))//intialization
  {
    const SDL_VideoInfo* vi = SDL_GetVideoInfo();
    screen_xsize = vi->current_w;
    screen_ysize = vi->current_h;
    printf("Screen resolution : %d x %d \n",screen_xsize, screen_ysize);
  }
  int color_depth = cfgfile->getInt("video.color_depth", 24);

  SDL_video_flags = SDL_OPENGL;
#ifdef linux
  SDL_video_flags |= SDL_RESIZABLE;
#endif
  if((nX==0)&&(nY==0)&&(nFullscreen==1))//resolution auto
  {
    nX = screen_xsize;
    nY = screen_ysize;
    resolution_auto = 1;
  }
  SDL_Surface* screen = video_setup_colordepth(nX, nY, nFullscreen, color_depth);

  if (!screen)
  {
    color_depth = 32;
    screen = video_setup_colordepth(nX, nY, nFullscreen, color_depth);
  }
  if (!screen)
  {
    color_depth = 16;
    screen = video_setup_colordepth(nX, nY, nFullscreen, color_depth);
  }
  if (!screen)
  {
    color_depth = 8;
    screen = video_setup_colordepth(nX, nY, nFullscreen, color_depth);
  }

  if (!screen)
  {
    printf("Failed to setup videomode %ix%i, fullscreen=%i\n", nX, nY, nFullscreen);
    fprintf(stderr, "Unable to setup any video mode. Exiting.\n");
    crrc_exit(CRRC_EXIT_FAILURE,
    #ifdef WIN32
        "Unable to setup any video mode.\nSee stdout.txt for more information."
    #else
        "Unable to setup any video mode.\nSee stdout for more information."
    #endif
      );
  }

  // Write back to config.
  if (resolution_auto ) 
    {
    nX = 0;
    nY = 0;
    }
  if (nFullscreen)
  {
    cfgfile->setAttributeOverwrite("video.fullscreen.fUse", "1");
    cfgfile->setAttributeOverwrite("video.resolution.fullscreen.x", nX);
    cfgfile->setAttributeOverwrite("video.resolution.fullscreen.y", nY);
  }
  else
  {
    cfgfile->setAttributeOverwrite("video.fullscreen.fUse", "0");
    cfgfile->setAttributeOverwrite("video.resolution.window.x", nX);
    cfgfile->setAttributeOverwrite("video.resolution.window.y", nY);
  }
  cfgfile->setAttributeOverwrite("video.color_depth", color_depth);
  
  window_xsize = screen->w;
  window_ysize = screen->h;
  
  // Store the received buffer depths for fast access
  SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &(vidbits.red));
  SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &(vidbits.green));
  SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &(vidbits.blue));
  SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &(vidbits.alpha));
  SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &(vidbits.depth));
  SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &(vidbits.stencil));
  
  std::string s = GetVideoInfoString("  ");
  printf("Using the following rendering mode:\n%s", s.c_str());
  printf("  ");
  Video::dumpGLStackInfo(stdout);

  return(0);
}

/*****************************************************************************/
void setWindowTitleString()
{
  SDL_WM_SetCaption("CRRCSim: Charles River RC Flight Simulator", "CRRCSim");
}

/*****************************************************************************/
void reshape(int w, int h)
{
  window_xsize = w;
  window_ysize = h;

  // save current screen resolution
  SimpleXMLTransfer* res = cfgfile->getChild("video.resolution");
  if (cfgfile->getInt("video.fullscreen.fUse"))
    res = res->getChild("fullscreen");
  else
    res = res->getChild("window");
  res->setAttributeOverwrite("x", window_xsize);
  res->setAttributeOverwrite("y", window_ysize);
}


/** \brief Resize the application window
 *
 *  Currently this only works for Linux. On other platforms
 *  you have to restart CRRCsim to activate the new resolution.
 *  This is a limitation of SDL on non-Linux platforms.
 *
 *  \param w new window width
 *  \param h new window height
 *
 *  \todo Find a window resize solution for non-Linux platforms.
 */
void resize_window(int w, int h)
{
#ifdef linux
  SDL_Surface *screen;
  SDL_video_flags &= ~SDL_FULLSCREEN;
  screen = SDL_SetVideoMode(w, h,
                            cfgfile->getInt("video.color_depth"),
                            SDL_video_flags);
  if (screen)
    reshape(screen->w, screen->h);
#else
  // until we find a solution, just ignore the event
  // --------------------------------------------------
  // This solution does not work on Win98:
  // rebuild_video_context(event.resize.w, event.resize.h)
  // --------------------------------------------------
#endif
}


void cleanup()
{
  delete console;
  cleanup_sky();
}


void read_config(SimpleXMLTransfer* cf)
{
  flSloppyCam   = cf->getDouble("video.camera.sloppy", 0.0);
  chase_behind  = cf->getDouble("video.camera.chase_behind", 10.0);
  chase_above   = cf->getDouble("video.camera.chase_above", 3.0);
}


/** drawSolidCube() is a modified version of drawBox
 *  from GLUT 3.7, file glut_shapes.c. It serves as
 *  a replacement for glutSolidCube().
 *  Please keep the following license information.
 */

/* Copyright (c) Mark J. Kilgard, 1994, 1997. */

/**
(c) Copyright 1993, Silicon Graphics, Inc.

ALL RIGHTS RESERVED

Permission to use, copy, modify, and distribute this software
for any purpose and without fee is hereby granted, provided
that the above copyright notice appear in all copies and that
both the copyright notice and this permission notice appear in
supporting documentation, and that the name of Silicon
Graphics, Inc. not be used in advertising or publicity
pertaining to distribution of the software without specific,
written prior permission.
*/
void drawSolidCube(GLfloat size)
{
  static GLfloat n[6][3] =
  {
    {-1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {1.0, 0.0, 0.0},
    {0.0, -1.0, 0.0},
    {0.0, 0.0, 1.0},
    {0.0, 0.0, -1.0}
  };
  static GLint faces[6][4] =
  {
    {0, 1, 2, 3},
    {3, 2, 6, 7},
    {7, 6, 5, 4},
    {4, 5, 1, 0},
    {5, 6, 2, 1},
    {7, 4, 0, 3}
  };
  GLfloat v[8][3];
  GLint i;

  v[0][0] = v[1][0] = v[2][0] = v[3][0] = -size / 2;
  v[4][0] = v[5][0] = v[6][0] = v[7][0] = size / 2;
  v[0][1] = v[1][1] = v[4][1] = v[5][1] = -size / 2;
  v[2][1] = v[3][1] = v[6][1] = v[7][1] = size / 2;
  v[0][2] = v[3][2] = v[4][2] = v[7][2] = -size / 2;
  v[1][2] = v[2][2] = v[5][2] = v[6][2] = size / 2;

  for (i = 5; i >= 0; i--) {
    glBegin(GL_QUADS);
    glNormal3fv(&n[i][0]);
    glVertex3fv(&v[faces[i][0]][0]);
    glVertex3fv(&v[faces[i][1]][0]);
    glVertex3fv(&v[faces[i][2]][0]);
    glVertex3fv(&v[faces[i][3]][0]);
    glEnd();
  }
}

/* end modified GLUT code */

static void updateCameraChase(float flDeltaT);

void cycleCameraMode()
{
  if (camera_mode == CAM_PILOT)
  {
    saved_pilot_pos = player_pos;
    camera_mode = CAM_FPV;
    printf("Camera: FPV\n");
  }
  else if (camera_mode == CAM_FPV)
  {
    camera_mode = CAM_CHASE;
    updateCameraChase(100.0);
    printf("Camera: Chase\n");
  }
  else
  {
    player_pos = saved_pilot_pos;
    camera_mode = CAM_PILOT;
    printf("Camera: Pilot\n");
  }
}

static void updateCameraPilot(float flDeltaT)
{
  camera_up = CRRCMath::Vector3(0, 1, 0);
  double  phimax    = flSloppyCam*zoom_get();
  double  max       = cos(phimax)*cos(phimax);

  CRRCMath::Vector3 plane_pos = FDM2Graphics(Global::aircraft->getPos());
  CRRCMath::Vector3 look_dir  = looking_pos - player_pos;
  CRRCMath::Vector3 plane_dir = plane_pos - player_pos;

  if (plane_dir.angle_cos_sqr(look_dir) < max)
  {
    double k = (plane_dir.inner(plane_dir)) / (plane_dir.inner(look_dir));
    look_dir = look_dir * k;
    CRRCMath::Vector3 diff_dir = look_dir - plane_dir;
    double ddl = diff_dir.length();
    if (ddl > 0.001)
    {
      double tan_phi = fabs(tan(acos(sqrt(max))));
      look_dir = plane_dir + diff_dir*(plane_dir.length()*tan_phi/ddl);
      looking_pos = look_dir + player_pos;
    }
  }
  else
  {
    CRRCMath::Vector3 diff_pos = plane_pos - looking_pos;
    looking_pos = looking_pos + diff_pos*(0.02*flDeltaT);
  }
}

static void updateCameraFPV(float)
{
  CRRCMath::Vector3 plane_pos = FDM2Graphics(Global::aircraft->getPos());

  double phi   = Global::aircraft->getFDM()->getPhi();
  double theta = Global::aircraft->getFDM()->getTheta();
  double psi   = Global::aircraft->getFDM()->getPsi();

  double cp = cos(phi),   sp = sin(phi);
  double ct = cos(theta), st = sin(theta);
  double cy = cos(psi),   sy = sin(psi);

  // Body-frame forward (1,0,0) rotated to NED world frame
  CRRCMath::Vector3 fwd_ned(ct*cy, ct*sy, -st);

  // Camera offset in body frame: slightly above CG so the nose is visible
  // Body frame: X=forward, Y=right, Z=down → offset (0, 0, -0.15) is 0.15 ft up
  double bx = 0, by = 0, bz = -0.15;
  CRRCMath::Vector3 offset_ned(
    ct*cy*bx + (sp*st*cy - cp*sy)*by + (cp*st*cy + sp*sy)*bz,
    ct*sy*bx + (sp*st*sy + cp*cy)*by + (cp*st*sy - sp*cy)*bz,
    -st*bx   +  sp*ct*by             +  cp*ct*bz);

  // Body-frame up (0,0,-1) rotated to NED world frame
  CRRCMath::Vector3 up_ned(
    (cp*st*cy + sp*sy) * -1.0,
    (cp*st*sy - sp*cy) * -1.0,
    cp*ct * -1.0);

  player_pos  = plane_pos + FDM2Graphics(offset_ned);
  looking_pos = player_pos + FDM2Graphics(fwd_ned);
  camera_up   = FDM2Graphics(up_ned);
}

static void updateCameraChase(float flDeltaT)
{
  camera_up = CRRCMath::Vector3(0, 1, 0);
  CRRCMath::Vector3 plane_pos = FDM2Graphics(Global::aircraft->getPos());
  CRRCMath::Vector3 vel = Global::aircraft->getFDM()->getVel();

  double gnd_speed = sqrt(vel.r[0]*vel.r[0] + vel.r[1]*vel.r[1]);

  double behind_north, behind_east;
  if (gnd_speed > 3.28)
  {
    behind_north = -vel.r[0] / gnd_speed;
    behind_east  = -vel.r[1] / gnd_speed;
  }
  else
  {
    double psi = Global::aircraft->getFDM()->getPsi();
    behind_north = -cos(psi);
    behind_east  = -sin(psi);
  }

  CRRCMath::Vector3 offset_ned(
    behind_north * chase_behind,
    behind_east  * chase_behind,
    -chase_above);

  CRRCMath::Vector3 desired = plane_pos + FDM2Graphics(offset_ned);

  double k = 1.0 - exp(-3.0 * flDeltaT);
  player_pos  = player_pos + (desired - player_pos) * k;
  looking_pos = plane_pos;
}

void UpdateCamera(float flDeltaT)
{
  switch (camera_mode)
  {
    case CAM_PILOT: updateCameraPilot(flDeltaT); break;
    case CAM_FPV:   updateCameraFPV(flDeltaT);   break;
    case CAM_CHASE: updateCameraChase(flDeltaT);  break;
  }
}


/**
 * Get the size of the current window.
 * \param x   x size in pixels will be written to this variable
 * \param y   y size in pixels will be written to this variable
 */
void getWindowSize(int& x, int& y)
{
  x = window_xsize;
  y = window_ysize;
}

/**
 * Read the "sloppy cam" setting from mod_video
 */
float getSloppyCam()
{
  return flSloppyCam;
}

/**
 * Write the "sloppy cam" setting
 * \param flValue  New value for sloppy cam
 */
void setSloppyCam(float flValue)
{
  flSloppyCam = flValue;
}


/**
 * Initialize the console overlay
 */
void initConsole()
{
  console = new GlConsole(400, 125, 15, 15);
  if (console == NULL)
  {
    crrc_exit(CRRC_EXIT_FAILURE, "Unable to initialize console.");
  }
  console->setAutoHide(4000, 1000);
  LOG(_("CRRCsim successfully started!"));
  LOG(_("Press <ESC> to show the setup menu."));
}

} // end of namespace Video::
