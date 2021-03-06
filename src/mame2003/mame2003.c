/*********************************************************************

	mame2003.c
    
    a port of mame 0.78 to the libretro API
    
*********************************************************************/    

#include <stdint.h>
#include <string/stdstring.h>
#include <libretro.h>
#include <file/file_path.h>

#include "mame.h"
#include "driver.h"
#include "state.h"
#include "log.h"
#include "input.h"
#include "inptport.h"
#include "fileio.h"
#include "controls.h"
#include "usrintrf.h"


static const struct GameDriver  *game_driver;
static float              delta_samples;
int                       samples_per_frame = 0;
int 			  orig_samples_per_frame =0;
short*                    samples_buffer;
short*                    conversion_buffer;
int                       usestereo = 1;
int16_t                   prev_pointer_x;
int16_t                   prev_pointer_y;
unsigned                  retroColorMode;
unsigned long             lastled = 0;
int16_t                   XsoundBuffer[2048];

extern const struct KeyboardInfo retroKeys[];
extern int          retroKeyState[512];
extern int          retroJsState[72];
extern int16_t      mouse_x[4];
extern int16_t      mouse_y[4];
extern struct       osd_create_params videoConfig;
extern int16_t      analogjoy[4][4];
struct ipd          *default_inputs; /* pointer the array of structs with default MAME input mappings and labels */

static struct retro_variable_default  default_options[OPT_end + 1];    /* need the plus one for the NULL entries at the end */
static struct retro_variable          current_options[OPT_end + 1];

static struct retro_input_descriptor empty[] = { { 0 } };

retro_log_printf_t                 log_cb;

struct                             retro_perf_callback perf_cb;
retro_environment_t                environ_cb                    = NULL;
retro_video_refresh_t              video_cb                      = NULL;
static retro_input_poll_t          poll_cb                       = NULL;
static retro_input_state_t         input_cb                      = NULL;
static retro_audio_sample_batch_t  audio_batch_cb                = NULL;
retro_set_led_state_t              led_state_cb                  = NULL;

bool old_dual_joystick_state = false; /* used to track when this core option changes */

/******************************************************************************

  private function prototypes

******************************************************************************/
static void   set_content_flags(void);
static void   init_core_options(void);
       void   init_default(struct retro_variable_default *option, const char *key, const char *value);
static void   update_variables(bool first_time);
static void   set_variables(bool first_time);
static struct retro_variable_default *spawn_effective_default(int option_index);
static void   check_system_specs(void);
       void   retro_describe_controls(void);
       int    get_mame_ctrl_id(int display_idx, int retro_ID);


/******************************************************************************

  implementation of key libretro functions

******************************************************************************/

void retro_init (void)
{
  struct retro_log_callback log;
  if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
    log_cb = log.log;
  else
    log_cb = NULL;

#ifdef LOG_PERFORMANCE
  environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb);
#endif

  check_system_specs();
}

static void check_system_specs(void)
{
   /* TODO - set variably */
   /* Midway DCS - Mortal Kombat/NBA Jam etc. require level 9 */
   unsigned level = 10;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_set_environment(retro_environment_t cb)
{
  environ_cb = cb;
}

static void init_core_options(void)
{
  init_default(&default_options[OPT_FRAMESKIP],           APPNAME"_frameskip",           "Frameskip; 0|1|2|3|4|5");
#if defined(__IOS__)
  init_default(&default_options[OPT_MOUSE_DEVICE],        APPNAME"_mouse_device",        "Mouse Device; pointer|mouse|disabled");
#else
  init_default(&default_options[OPT_MOUSE_DEVICE],        APPNAME"_mouse_device",        "Mouse Device; mouse|pointer|disabled");
#endif
  init_default(&default_options[OPT_CROSSHAIR_ENABLED],   APPNAME"_crosshair_enabled",   "Show Lightgun crosshair; enabled|disabled");
  init_default(&default_options[OPT_SKIP_DISCLAIMER],     APPNAME"_skip_disclaimer",     "Skip Disclaimer; disabled|enabled");
  init_default(&default_options[OPT_SKIP_WARNINGS],       APPNAME"_skip_warnings",       "Skip Warnings; disabled|enabled");    
  init_default(&default_options[OPT_DISPLAY_SETUP],       APPNAME"_display_setup",       "Display MAME menu; disabled|enabled");
  init_default(&default_options[OPT_BRIGHTNESS],          APPNAME"_brightness",
                                                                                        "Brightness; 1.0|0.2|0.3|0.4|0.5|0.6|0.7|0.8|0.9|1.1|1.2|1.3|1.4|1.5|1.6|1.7|1.8|1.9|2.0");
  init_default(&default_options[OPT_GAMMA],               APPNAME"_gamma",
                                                                                        "Gamma correction; 1.2|0.5|0.6|0.7|0.8|0.9|1.1|1.2|1.3|1.4|1.5|1.6|1.7|1.8|1.9|2.0");
  init_default(&default_options[OPT_BACKDROP],            APPNAME"_enable_backdrop",     "EXPERIMENTAL: Use Backdrop artwork (Restart); disabled|enabled");
  init_default(&default_options[OPT_NEOGEO_BIOS],         APPNAME"_neogeo_bios", 
                                                                                        "Specify Neo Geo BIOS (Restart); default|euro|euro-s1|us|us-e|asia|japan|japan-s2|unibios33|unibios20|unibios13|unibios11|unibios10|debug|asia-aes");
  init_default(&default_options[OPT_STV_BIOS],            APPNAME"_stv_bios",            "Specify Sega ST-V BIOS (Restart); default|japan|japana|us|japan_b|taiwan|europe");  
  init_default(&default_options[OPT_USE_ALT_SOUND],       APPNAME"_use_alt_sound",       "Use CD soundtrack (Restart); enabled|disabled");
  init_default(&default_options[OPT_SHARE_DIAL],          APPNAME"_dialsharexy",         "Share 2 player dial controls across one X/Y device; disabled|enabled");
  init_default(&default_options[OPT_DUAL_JOY],            APPNAME"_dual_joysticks",      "Dual Joystick Mode (!NETPLAY); disabled|enabled");
  init_default(&default_options[OPT_RSTICK_BTNS],         APPNAME"_rstick_to_btns",      "Right Stick to Buttons; enabled|disabled");
  init_default(&default_options[OPT_TATE_MODE],           APPNAME"_tate_mode",           "TATE Mode; disabled|enabled");
  init_default(&default_options[OPT_VECTOR_RESOLUTION],   APPNAME"_vector_resolution_multiplier", 
                                                                                         "EXPERIMENTAL: Vector resolution multiplier (Restart); 1|2|3|4|5|6");
  init_default(&default_options[OPT_VECTOR_ANTIALIAS],    APPNAME"_vector_antialias",    "EXPERIMENTAL: Vector antialias; disabled|enabled");
  init_default(&default_options[OPT_VECTOR_TRANSLUCENCY], APPNAME"_vector_translucency", "Vector translucency; enabled|disabled");
  init_default(&default_options[OPT_VECTOR_BEAM],         APPNAME"_vector_beam_width",   "EXPERIMENTAL: Vector beam width; 1|2|3|4|5");
  init_default(&default_options[OPT_VECTOR_FLICKER],      APPNAME"_vector_flicker",      "Vector flicker; 20|0|10|20|30|40|50|60|70|80|90|100");
  init_default(&default_options[OPT_VECTOR_INTENSITY],    APPNAME"_vector_intensity",    "Vector intensity; 1.5|0.5|1|2|2.5|3");
  init_default(&default_options[OPT_NVRAM_BOOTSTRAP],     APPNAME"_nvram_bootstraps",    "NVRAM Bootstraps; enabled|disabled");
  init_default(&default_options[OPT_SAMPLE_RATE],         APPNAME"_sample_rate",         "Sample Rate (KHz); 48000|8000|11025|22050|44100");
  init_default(&default_options[OPT_DCS_SPEEDHACK],       APPNAME"_dcs_speedhack",       "DCS Speedhack; enabled|disabled");
  init_default(&default_options[OPT_INPUT_INTERFACE],     APPNAME"_input_interface",     "Input interface; retropad|mame_keyboard|simultaneous");  
  init_default(&default_options[OPT_MAME_REMAPPING],      APPNAME"_mame_remapping",      "Activate MAME Remapping (!NETPLAY); disabled|enabled");
  
  init_default(&default_options[OPT_end], NULL, NULL);
  set_variables(true);
}

static void set_variables(bool first_time)
{
  static struct retro_variable_default  effective_defaults[OPT_end + 1];
  static unsigned effective_options_count;         /* the number of core options in effect for the current content */
  int option_index   = 0; 

  for(option_index = 0; option_index < (OPT_end + 1); option_index++)
  {
    switch(option_index)
    {
      case OPT_CROSSHAIR_ENABLED:
         if(!options.content_flags[CONTENT_LIGHTGUN])
           continue;
         break; 
      case OPT_STV_BIOS:
         if(!options.content_flags[CONTENT_STV])
           continue; /* only offer BIOS selection when it is relevant */
         break;
      case OPT_NEOGEO_BIOS:
          if(!options.content_flags[CONTENT_NEOGEO])
            continue; /* only offer BIOS selection when it is relevant */
          break;
      case OPT_USE_ALT_SOUND:
         if(!options.content_flags[CONTENT_ALT_SOUND])
           continue;
         break;
      case OPT_SHARE_DIAL:
         if(!options.content_flags[CONTENT_DIAL])
           continue;
         break;
      case OPT_DUAL_JOY:
         if(!options.content_flags[CONTENT_DUAL_JOYSTICK])
           continue;
         break;
      case OPT_VECTOR_RESOLUTION:
      case OPT_VECTOR_ANTIALIAS:
      case OPT_VECTOR_TRANSLUCENCY:
      case OPT_VECTOR_BEAM:
      case OPT_VECTOR_FLICKER:
      case OPT_VECTOR_INTENSITY:
         if(!options.content_flags[CONTENT_VECTOR])
           continue;
         break;
      case OPT_DCS_SPEEDHACK:
         if(!options.content_flags[CONTENT_DCS_SPEEDHACK])
           continue;
         break;
      case OPT_NVRAM_BOOTSTRAP:
         if(!options.content_flags[CONTENT_NVRAM_BOOTSTRAP])
           continue;
         break;
    }
   effective_defaults[effective_options_count] = first_time ? default_options[option_index] : *spawn_effective_default(option_index);
   effective_options_count++;
  }
  environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)effective_defaults);
}

static struct retro_variable_default *spawn_effective_default(int option_index)
{
  static struct retro_variable_default *encoded_default = NULL;
  /* search for the string "; " as the delimiter between the option display name and the values */
  /* stringify the current value for this option */
  /* see if the current option string is already listed first in the original default -- is it the first in the pipe-delimited list? if so, just return default_options[option_index] */
  /* if the current selected option is not in the original defaults string at all, log an error message. that shouldn't be possible. */
  /* otherwise, create a copy of default_options[option_index].defaults_string. First add the stringified current option as the first in the pipe-delimited list for this copied string, and then remove the option from wherever it was originally in the defaults string */
  return encoded_default;
}

void init_default(struct retro_variable_default *def, const char *key, const char *label_and_values)
{
  def->key = key;
  def->defaults_string = label_and_values;
}

static void update_variables(bool first_time)
{
  struct retro_led_interface ledintf;
  struct retro_variable var;
  int index;

  for(index = 0; index < OPT_end; index++)
  {
    var.value = NULL;
    var.key = default_options[index].key;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && !string_is_empty(var.value)) /* the frontend sends a value for this core option */
    {
      current_options[index].value = var.value; /* keep the state of core options matched with the frontend */
      current_options[index].value = var.value; /* keep the state of core options matched with the frontend */
      
      switch(index)
      {
        case OPT_FRAMESKIP:
          options.frameskip = atoi(var.value);
          break;

        case OPT_INPUT_INTERFACE:
          if(strcmp(var.value, "retropad") == 0)
            options.input_interface = RETRO_DEVICE_JOYPAD;
          else if(strcmp(var.value, "mame_keyboard") == 0)
            options.input_interface = RETRO_DEVICE_KEYBOARD;
          else
            options.input_interface = 0; /* retropad and keyboard simultaneously. "old-school mame2003 input mode" */
          break;

        case OPT_MOUSE_DEVICE:
          if(strcmp(var.value, "pointer") == 0)
            options.mouse_device = RETRO_DEVICE_POINTER;
          else if(strcmp(var.value, "mouse") == 0)
            options.mouse_device = RETRO_DEVICE_MOUSE;
          else
            options.mouse_device = 0;
          break;

        case OPT_CROSSHAIR_ENABLED:
          if(strcmp(var.value, "enabled") == 0)
            options.crosshair_enable = 1;
          else
            options.crosshair_enable = 0;
          break;
          
        case OPT_SKIP_DISCLAIMER:
          if(strcmp(var.value, "enabled") == 0)
            options.skip_disclaimer = true;
          else
            options.skip_disclaimer = false;
          break;

        case OPT_SKIP_WARNINGS:
          if(strcmp(var.value, "enabled") == 0)
            options.skip_warnings = true;
          else
            options.skip_warnings = false;
          break;

        case OPT_DISPLAY_SETUP:
          if(strcmp(var.value, "enabled") == 0)
            options.display_setup = 1;
          else
            options.display_setup = 0;
          break;

        case OPT_BRIGHTNESS:
          options.brightness = atof(var.value);
          if(!first_time)
            palette_set_global_brightness(options.brightness);    
          break;

        case OPT_GAMMA:
          options.gamma = atof(var.value);
          if(!first_time)
            palette_set_global_gamma(options.gamma);
          break;

          /* TODO: Add overclock option. Below is the code from the old MAME osd to help process the core option.*/
          /*

          double overclock;
          int cpu, doallcpus = 0, oc;

          if (code_pressed(KEYCODE_LSHIFT) || code_pressed(KEYCODE_RSHIFT))
            doallcpus = 1;
          if (!code_pressed(KEYCODE_LCONTROL) && !code_pressed(KEYCODE_RCONTROL))
            increment *= 5;
          if( increment :
            overclock = timer_get_overclock(arg);
            overclock += 0.01 * increment;
            if (overclock < 0.01) overclock = 0.01;
            if (overclock > 2.0) overclock = 2.0;
            if( doallcpus )
              for( cpu = 0; cpu < cpu_gettotalcpu(); cpu++ )
                timer_set_overclock(cpu, overclock);
            else
              timer_set_overclock(arg, overclock);
          }

          oc = 100 * timer_get_overclock(arg) + 0.5;

          if( doallcpus )
            sprintf(buf,"%s %s %3d%%", ui_getstring (UI_allcpus), ui_getstring (UI_overclock), oc);
          else
            sprintf(buf,"%s %s%d %3d%%", ui_getstring (UI_overclock), ui_getstring (UI_cpu), arg, oc);
          displayosd(bitmap,buf,oc/2,100/2);
        */

        case OPT_BACKDROP:
          if(strcmp(var.value, "enabled") == 0)
            options.use_artwork = ARTWORK_USE_BACKDROPS;
          else
            options.use_artwork = ARTWORK_USE_NONE;
          break;

        case OPT_STV_BIOS:
          if(!options.content_flags[CONTENT_STV])
            break;
          if(options.content_flags[CONTENT_DIEHARD]) /* catch required bios for this one game. */
            options.bios = "us";
          else
            options.bios = (strcmp(var.value, "default") == 0) ? NULL : var.value;
          break;

        case OPT_NEOGEO_BIOS:
          if(!options.content_flags[CONTENT_NEOGEO])
            break;
          options.bios = (strcmp(var.value, "default") == 0) ? NULL : var.value;
          break;

        case OPT_USE_ALT_SOUND:
          if(options.content_flags[CONTENT_ALT_SOUND])
          {
            if(strcmp(var.value, "enabled") == 0)
              options.use_samples = true;
            else
              options.use_samples = false;
          }
          break;

        case OPT_SHARE_DIAL:
          if(options.content_flags[CONTENT_DIAL])
          {
            if(strcmp(var.value, "enabled") == 0)
              options.dial_share_xy = 1;
            else
              options.dial_share_xy = 0;
            break;
          }
          else
          {
            options.dial_share_xy = 0;
            break;
          }          

        case OPT_DUAL_JOY:
          if(options.content_flags[CONTENT_DUAL_JOYSTICK])
          {
            if(strcmp(var.value, "enabled") == 0)
              options.dual_joysticks = true;
            else
              options.dual_joysticks = false;
            
            if(first_time)
              old_dual_joystick_state = options.dual_joysticks;
            else if(old_dual_joystick_state != options.dual_joysticks)
            {
              char cfg_file_path[PATH_MAX_LENGTH];
              char buffer[PATH_MAX_LENGTH];
              osd_get_path(FILETYPE_CONFIG, buffer);
              snprintf(cfg_file_path, PATH_MAX_LENGTH, "%s%s%s.cfg", buffer, path_default_slash(), options.romset_filename_noext);
              buffer[0] = '\0';
              
              if(path_is_valid(cfg_file_path))
              {            
                if(!remove(cfg_file_path) == 0)
                  snprintf(buffer, PATH_MAX_LENGTH, "%s.cfg exists but cannot be deleted!\n", options.romset_filename_noext);
                else
                  snprintf(buffer, PATH_MAX_LENGTH, "%s.cfg exists but cannot be deleted!\n", options.romset_filename_noext);
              }
              log_cb(RETRO_LOG_INFO, LOGPRE "%s Reloading input maps.\n", buffer);
              usrintf_showmessage_secs(4, "%s Reloading input maps.", buffer);
              
              load_input_port_settings();
              old_dual_joystick_state = options.dual_joysticks;
            }
            break;
          }
          else /* always disabled except when options.content_flags[CONTENT_DUAL_JOYSTICK] has been set to true */
          {
            options.dual_joysticks = false; 
            break;
          }

        case OPT_RSTICK_BTNS:
          if(strcmp(var.value, "enabled") == 0)
            options.rstick_to_btns = 1;
          else
            options.rstick_to_btns = 0;
          break;

        case OPT_TATE_MODE:
          if(strcmp(var.value, "enabled") == 0)
            options.tate_mode = 1;
          else
            options.tate_mode = 0;
          break;

        case OPT_VECTOR_RESOLUTION:
          options.vector_resolution_multiplier = atoi(var.value);
          break;

        case OPT_VECTOR_ANTIALIAS:
          if(strcmp(var.value, "enabled") == 0)
            options.antialias = 1; /* integer: 1 to enable antialiasing on vectors _ does not work as of 2018/04/17*/
          else
            options.antialias = 0;
          break;

        case OPT_VECTOR_TRANSLUCENCY:
          if(strcmp(var.value, "enabled") == 0)
            options.translucency = 1; /* integer: 1 to enable translucency on vectors */
          else
            options.translucency = 0;
          break;

        case OPT_VECTOR_BEAM:
          options.beam = atoi(var.value); /* integer: vector beam width */
          break;

        case OPT_VECTOR_FLICKER:
          options.vector_flicker = (int)(2.55 * atof(var.value)); /* why 2.55? must be an old mame family recipe */
          break;

        case OPT_VECTOR_INTENSITY:
          options.vector_intensity_correction = atof(var.value); /* float: vector beam intensity */
          break;

        case OPT_NVRAM_BOOTSTRAP:
          if(strcmp(var.value, "enabled") == 0)
            options.nvram_bootstrap = true;
          else
            options.nvram_bootstrap = false;
          break;

        case OPT_SAMPLE_RATE:
          options.samplerate = atoi(var.value);
          break;

        case OPT_DCS_SPEEDHACK:
          if(strcmp(var.value, "enabled") == 0)
            options.activate_dcs_speedhack = 1;
          else
            options.activate_dcs_speedhack = 0;
          break;

        case OPT_MAME_REMAPPING:
          if(strcmp(var.value, "enabled") == 0)
            options.mame_remapping = true;
          else
            options.mame_remapping = false;
          if(!first_time)
            setup_menu_init();
          break;
      }
    }
  }
  
  if(!options.content_flags[CONTENT_ALT_SOUND])
    options.use_samples = true;

  ledintf.set_led_state = NULL;
  environ_cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &ledintf);
  led_state_cb = ledintf.set_led_state;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   const int orientation = game_driver->flags & ORIENTATION_MASK;
   const bool rotated = ((orientation == ROT90) || (orientation == ROT270));
   
   const int width = rotated ? videoConfig.height : videoConfig.width;
   const int height = rotated ? videoConfig.width : videoConfig.height;
   
   info->geometry.base_width = width;
   info->geometry.base_height = height;
   info->geometry.max_width = width;
   info->geometry.max_height = height;
   info->geometry.aspect_ratio = (rotated && !options.tate_mode) ? (float)videoConfig.aspect_y / (float)videoConfig.aspect_x : (float)videoConfig.aspect_x / (float)videoConfig.aspect_y;
   
   if (Machine->drv->frames_per_second < 60.0 )
       info->timing.fps = 60.0; 
   else 
      info->timing.fps = Machine->drv->frames_per_second; /* qbert is 61 fps */

   if  ( (Machine->drv->frames_per_second * 1000 < options.samplerate) || ( Machine->drv->frames_per_second < 60) ) 
   {
	info->timing.sample_rate = Machine->drv->frames_per_second * 1000;
	log_cb(RETRO_LOG_INFO, LOGPRE "Sample timing rate too high for framerate required dropping to %f",  Machine->drv->frames_per_second * 1000);
   }       
   else
   {
	info->timing.sample_rate = options.samplerate;
	log_cb(RETRO_LOG_INFO, LOGPRE "Sample rate set to %d",options.samplerate); 
   }
}

unsigned retro_api_version(void)
{
  return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info)
{
  info->library_name = "MAME 2003-plus";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
  info->library_version = "0.78" GIT_VERSION;
  info->valid_extensions = "zip";
  info->need_fullpath = true;
  info->block_extract = true;
}

static struct retro_controller_description controllers[] = {
  { "Gamepad",	       RETRO_DEVICE_JOYPAD},
  { "8-Button",        PAD_8BUTTON },
  { "6-Button",        PAD_6BUTTON },
  { "Classic Gamepad", PAD_CLASSIC },
};

static struct retro_controller_description unsupported_controllers[] = {
  { "UNSUPPORTED (Gamepad)",	       RETRO_DEVICE_JOYPAD},
  { "UNSUPPORTED (8-Button)",        PAD_8BUTTON },
  { "UNSUPPORTED (6-Button)",        PAD_6BUTTON },
  { "UNSUPPORTED (Classic Gamepad)", PAD_CLASSIC },
};

static struct retro_controller_info retropad_subdevice_ports[] = {
  { controllers, 4 },
  { controllers, 4 },
  { controllers, 4 },
  { controllers, 4 },
  { controllers, 4 },
  { controllers, 4 },
  { 0 },
};

bool retro_load_game(const struct retro_game_info *game)
{
  int              driverIndex    = 0;
  int              port_index;
  char             *driver_lookup = NULL;
  int              orientation    = 0;
  unsigned         rotateMode     = 0;
  static const int uiModes[]      = {ROT0, ROT90, ROT180, ROT270};

  if(string_is_empty(game->path))
  {
    log_cb(RETRO_LOG_ERROR, LOGPRE "Content path is not set. Exiting!\n");
    return false;
  }

  log_cb(RETRO_LOG_INFO, LOGPRE "Content path: %s.\n", game->path);    
  if(!path_is_valid(game->path))
  {
    log_cb(RETRO_LOG_ERROR, LOGPRE "Content path is not valid. Exiting!");
    return false;
  }

  driver_lookup = strdup(path_basename(game->path));
  path_remove_extension(driver_lookup);

  log_cb(RETRO_LOG_INFO, LOGPRE "Content lookup name: %s.\n", driver_lookup);

  for (driverIndex = 0; driverIndex < total_drivers; driverIndex++)
  {
    const struct GameDriver *needle = drivers[driverIndex];

    if ((strcasecmp(driver_lookup, needle->description) == 0) 
      || (strcasecmp(driver_lookup, needle->name) == 0) )
    {
      log_cb(RETRO_LOG_INFO, LOGPRE "Total MAME drivers: %i. Matched game driver: %s.\n", (int) total_drivers, needle->name);
      game_driver = needle;
      options.romset_filename_noext = driver_lookup;
      break;
    }
  }

  if(driverIndex == total_drivers)
  {
      log_cb(RETRO_LOG_ERROR, LOGPRE "Total MAME drivers: %i. MAME driver not found for selected game!", (int) total_drivers);
      return false;
  }

  if(!init_game(driverIndex))
    return false;  
  set_content_flags();

  options.libretro_content_path = strdup(game->path);
  path_basedir(options.libretro_content_path);

  /* Get system directory from frontend */
  options.libretro_system_path = NULL;
  environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY,&options.libretro_system_path);
  if (options.libretro_system_path == NULL || options.libretro_system_path[0] == '\0')
  {
      log_cb(RETRO_LOG_INFO, LOGPRE "libretro system path not set by frontend, using content path\n");
      options.libretro_system_path = options.libretro_content_path;
  }
  
  /* Get save directory from frontend */
  options.libretro_save_path = NULL;
  environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY,&options.libretro_save_path);
  if (options.libretro_save_path == NULL || options.libretro_save_path[0] == '\0')
  {
      log_cb(RETRO_LOG_INFO,  LOGPRE "libretro save path not set by frontent, using content path\n");
      options.libretro_save_path = options.libretro_content_path;
  }

  log_cb(RETRO_LOG_INFO, LOGPRE "content path: %s\n", options.libretro_content_path);
  log_cb(RETRO_LOG_INFO, LOGPRE " system path: %s\n", options.libretro_system_path);
  log_cb(RETRO_LOG_INFO, LOGPRE "   save path: %s\n", options.libretro_save_path);

  /* Setup Rotation */
  rotateMode = 0;        
  orientation = drivers[driverIndex]->flags & ORIENTATION_MASK;
  
  rotateMode = (orientation == ROT270) ? 1 : rotateMode;
  rotateMode = (orientation == ROT180) ? 2 : rotateMode;
  rotateMode = (orientation == ROT90) ? 3 : rotateMode;
  
  environ_cb(RETRO_ENVIRONMENT_SET_ROTATION, &rotateMode);
  options.ui_orientation = uiModes[rotateMode];

  init_core_options();
  update_variables(true);
  
  for(port_index = DISP_PLAYER6 - 1; port_index > (options.ctrl_count - 1); port_index--)
  {
    retropad_subdevice_ports[port_index].types       = &unsupported_controllers;
    retropad_subdevice_ports[port_index].num_types   = 4;
  }

  environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)retropad_subdevice_ports);
  
  if(!run_game(driverIndex))
    return false;
  
  return true;
}

static void set_content_flags(void)
{
  int i = 0;

  extern struct GameDriver driver_neogeo;
  extern struct GameDriver driver_stvbios;
  const struct InputPortTiny *input = game_driver->input_ports;

extern	const char* ost_drivers[]; 
  /************ DRIVERS WITH MULTIPLE BIOS OPTIONS ************/
  if (game_driver->clone_of == &driver_neogeo
   ||(game_driver->clone_of && game_driver->clone_of->clone_of == &driver_neogeo))
  {
    options.content_flags[CONTENT_NEOGEO] = true;
    log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as a Neo Geo game.\n");
  }
  else if (game_driver->clone_of == &driver_stvbios
   ||(game_driver->clone_of && game_driver->clone_of->clone_of == &driver_stvbios))
  {
    options.content_flags[CONTENT_STV] = true;
    log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as a ST-V game.\n");
  }

  /************ DIE HARD: ARCADE ************/
  if(strcasecmp(game_driver->name, "diehard") == 0)
  {
    options.content_flags[CONTENT_DIEHARD] = true;
    log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as \"Die Hard: Arcade\". BIOS will be set to \"us\".\n");
  }

  /************ DRIVERS WITH ALTERNATE SOUNDTRACKS ************/
  while(ost_drivers[i])
  {
    if(strcmp(ost_drivers[i], game_driver->name) == 0)
    {
      options.content_flags[CONTENT_ALT_SOUND] = true;
      log_cb(RETRO_LOG_INFO, LOGPRE "Content has an alternative audio option controlled via core option.\n");
      break;
    }
    i++;
  }

  /************ DRIVERS WITH VECTOR VIDEO DISPLAYS ************/  
  if(Machine->drv->video_attributes & VIDEO_TYPE_VECTOR)
  {
    options.content_flags[CONTENT_VECTOR] = true;
    log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using a vector video display.\n");
  }
  
  /************ INPUT-BASED CONTENT FLAGS ************/
	while ((input->type & ~IPF_MASK) != IPT_END)
	{
		/* skip analog extension fields */
		if ((input->type & ~IPF_MASK) != IPT_EXTENSION)
		{
			switch (input->type & IPF_PLAYERMASK)
			{
				case IPF_PLAYER1:
					if (options.player_count < 1) options.player_count = 1;
					break;
				case IPF_PLAYER2:
					if (options.player_count < 2) options.player_count = 2;
					break;
				case IPF_PLAYER3:
					if (options.player_count < 3) options.player_count = 3;
					break;
				case IPF_PLAYER4:
					if (options.player_count < 4) options.player_count = 4;
					break;
				case IPF_PLAYER5:
					if (options.player_count < 5) options.player_count = 5;
					break;
				case IPF_PLAYER6:
					if (options.player_count < 6) options.player_count = 6;
					break;
			}
			switch (input->type & ~IPF_MASK)
			{
				case IPT_JOYSTICKRIGHT_UP:
				case IPT_JOYSTICKRIGHT_DOWN:
				case IPT_JOYSTICKRIGHT_LEFT:
				case IPT_JOYSTICKRIGHT_RIGHT:
				case IPT_JOYSTICKLEFT_UP:
				case IPT_JOYSTICKLEFT_DOWN:
				case IPT_JOYSTICKLEFT_LEFT:
				case IPT_JOYSTICKLEFT_RIGHT:
					if (input->type & IPF_2WAY)
          {
						/*control = "doublejoy2way";*/
          }
					else if (input->type & IPF_4WAY)
          {
						/*control = "doublejoy4way";*/
          }
					else
          {
						/*control = "doublejoy8way";*/
          }
          options.content_flags[CONTENT_DUAL_JOYSTICK] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using \"dual joystick\" controls.\n");
					break;
				case IPT_BUTTON1:
					if (options.button_count < 1) options.button_count = 1;
					break;
				case IPT_BUTTON2:
					if (options.button_count < 2) options.button_count = 2;
					break;
				case IPT_BUTTON3:
					if (options.button_count < 3) options.button_count = 3;
					break;
				case IPT_BUTTON4:
					if (options.button_count < 4) options.button_count = 4;
					break;
				case IPT_BUTTON5:
					if (options.button_count < 5) options.button_count = 5;
					break;
				case IPT_BUTTON6:
					if (options.button_count <6 ) options.button_count = 6;
					break;
				case IPT_BUTTON7:
					if (options.button_count < 7) options.button_count = 7;
					break;
				case IPT_BUTTON8:
					if (options.button_count < 8) options.button_count = 8;
					break;
				case IPT_BUTTON9:
					if (options.button_count < 9) options.button_count = 9;
					break;
				case IPT_BUTTON10:
					if (options.button_count < 10) options.button_count = 10;
					break;
				case IPT_PADDLE:
          options.content_flags[CONTENT_PADDLE] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using paddle controls.\n");
          break;
				case IPT_DIAL:
          options.content_flags[CONTENT_DIAL] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using dial controls.\n");
					break;
				case IPT_TRACKBALL_X:
				case IPT_TRACKBALL_Y:
          options.content_flags[CONTENT_TRACKBALL] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using trackball controls.\n");
					break;
				case IPT_AD_STICK_X:
				case IPT_AD_STICK_Y:
          options.content_flags[CONTENT_AD_STICK] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using Analog/Digital stick controls.\n");
					break;
				case IPT_LIGHTGUN_X:
				case IPT_LIGHTGUN_Y:
          options.content_flags[CONTENT_LIGHTGUN] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as using Analog/Digital stick controls.\n");
					break;
				case IPT_SERVICE :
          options.content_flags[CONTENT_HAS_SERVICE] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as having a service button.\n");
					break;
				case IPT_TILT :
          options.content_flags[CONTENT_HAS_TILT] = true;
          log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as having a tilt feature.\n");
					break;
			}
		}
		++input;
	}

  /************ DRIVERS FLAGGED IN CONTROLS.C WITH ALTERNATING CONTROLS ************/  
  if(game_driver->ctrl_dat->alternating_controls) 
  { 
    options.content_flags[CONTENT_ALTERNATING_CTRLS] = true;
    /* there may or may not be some need to have a ctrl_count different than player_count, perhaps because of some
       alternating controls layout. this is a place to check some condition and make the two numbers different
       if that should ever prove useful. */
    if(true)       
      options.ctrl_count = options.player_count;
  }
  else
    options.ctrl_count = options.player_count;
 
  log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as supporting %i players with %i distinct controls.\n", options.player_count, options.ctrl_count);
  log_cb(RETRO_LOG_INFO, LOGPRE "Content identified as supporting %i button controls.\n", options.button_count);

  
  
  /************ DRIVERS FLAGGED IN CONTROLS.C WITH MIRRORED CONTROLS ************/  
  if(game_driver->ctrl_dat->mirrored_controls) 
  { 
    options.content_flags[CONTENT_MIRRORED_CTRLS] = true;
    log_cb(RETRO_LOG_INFO, LOGPRE "Content identified by controls.c as having mirrored multiplayer control labels.\n");
  }
  else
    log_cb(RETRO_LOG_INFO, LOGPRE "Content identified by controls.c as having non-mirrored multiplayer control labels.\n");


  /************ DCS DRIVERS WITH SPEEDDUP HACKS ************/
  while(/*dcs_drivers[i]*/true)
  {
    if(/*strcmp(dcs_drivers[i], game_driver->name) == 0*/true)
    {
      options.content_flags[CONTENT_DCS_SPEEDHACK] = true;
      /*log_cb(RETRO_LOG_INFO, LOGPRE "DCS content has a speedup hack controlled via core option.\n");*/
      break;
    }
    i++;
  }
  
  /************ DRIVERS WITH NVRAM BOOTSTRAP PATCHES ************/
  if(game_driver->bootstrap != NULL)
  {
    options.content_flags[CONTENT_NVRAM_BOOTSTRAP] = true;
    log_cb(RETRO_LOG_INFO, LOGPRE "Content has an NVRAM bootstrap controlled via core option.\n");
  }  

}

void retro_reset (void)
{
    machine_reset(); /* use MAME function */
}

/* get pointer axis vector from coord */
int16_t get_pointer_delta(int16_t coord, int16_t *prev_coord)
{
   int16_t delta = 0;
   if (*prev_coord == 0 || coord == 0)
   {
      *prev_coord = coord;
   }
   else
   {
      if (coord != *prev_coord)
      {
         delta = coord - *prev_coord;
         *prev_coord = coord;
      }
   }
   
   return delta;
}

void retro_run (void)
{
   int i;
   bool pointer_pressed;
   const struct KeyboardInfo *thisInput;
   bool updated = false;

   poll_cb();

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      update_variables(false);

   /* Keyboard */
   thisInput = retroKeys;
   while(thisInput->name)
   {
      retroKeyState[thisInput->code] = input_cb(0, RETRO_DEVICE_KEYBOARD, 0, thisInput->code);
      thisInput ++;
   }
   
   for (i = 0; i < 4; i ++)
   {
      unsigned int offset = (i * 18);

      /* Analog joystick */
      analogjoy[i][0] = input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
      analogjoy[i][1] = input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
      analogjoy[i][2] = input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
      analogjoy[i][3] = input_cb(i, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
      if ( (options.rstick_to_btns) && (options.content_flags[CONTENT_DUAL_JOYSTICK]) )
      /* Joystick */

      {
         /* if less than 0.5 force, ignore and read buttons as usual */
         retroJsState[0 + offset] = analogjoy[i][3] >  0x4000 ? 1 : input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
         retroJsState[1 + offset] = analogjoy[i][2] < -0x4000 ? 1 : input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
      }
      else
      {
         retroJsState[0 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
         retroJsState[1 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
      }
      retroJsState[2 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
      retroJsState[3 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
      retroJsState[4 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
      retroJsState[5 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
      retroJsState[6 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
      retroJsState[7 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
      if ( (options.rstick_to_btns) && (options.content_flags[CONTENT_DUAL_JOYSTICK]) )
      {
         retroJsState[8 + offset] = analogjoy[i][2] >  0x4000 ? 1 : input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
         retroJsState[9 + offset] = analogjoy[i][3] < -0x4000 ? 1 : input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
      }
      else
      {
         retroJsState[8 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
         retroJsState[9 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
      }
      retroJsState[10 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
      retroJsState[11 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
      retroJsState[12 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2);
      retroJsState[13 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2);
      retroJsState[14 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
      retroJsState[15 + offset] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3);
      
      if (options.mouse_device)
      {
         if (options.mouse_device == RETRO_DEVICE_MOUSE)
         {
            retroJsState[16 + offset] = input_cb(i, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT);
            retroJsState[17 + offset] = input_cb(i, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT);
            mouse_x[i] = input_cb(i, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
            mouse_y[i] = input_cb(i, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
         }
         else /* RETRO_DEVICE_POINTER */
         {
            pointer_pressed = input_cb(i, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
            retroJsState[16 + offset] = pointer_pressed;
            retroJsState[17 + offset] = 0; /* padding */
            mouse_x[i] = pointer_pressed ? get_pointer_delta(input_cb(i, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X), &prev_pointer_x) : 0;
            mouse_y[i] = pointer_pressed ? get_pointer_delta(input_cb(i, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y), &prev_pointer_y) : 0;
         }
      }
      else
      {
         retroJsState[16 + offset] = 0;
         retroJsState[17 + offset] = 0;
      }
   }

   mame_frame();

}

void retro_unload_game(void)
{
    mame_done();
    /* do we need to be freeing things here? */
    
    free(options.romset_filename_noext); 
}

void retro_deinit(void)
{
#ifdef LOG_PERFORMANCE
   perf_cb.perf_log();
#endif
}


size_t retro_serialize_size(void)
{
    extern size_t state_get_dump_size(void);
    
    return state_get_dump_size();
}

bool retro_serialize(void *data, size_t size)
{
   int cpunum;
	if( ( retro_serialize_size() ) && (data)  && (size) )
	{
		/* write the save state */
		state_save_save_begin(data);

		/* write tag 0 */
		state_save_set_current_tag(0);
		if(state_save_save_continue())
		{
		    return false;
		}

		/* loop over CPUs */
		for (cpunum = 0; cpunum < cpu_gettotalcpu(); cpunum++)
		{
			cpuintrf_push_context(cpunum);

			/* make sure banking is set */
			activecpu_reset_banking();

			/* save the CPU data */
			state_save_set_current_tag(cpunum + 1);
			if(state_save_save_continue())
			    return false;

			cpuintrf_pop_context();
		}

		/* finish and close */
		state_save_save_finish();
		
		return true;
	}

	return false;
}

bool retro_unserialize(const void * data, size_t size)
{
    int cpunum;
	/* if successful, load it */
	if ( (retro_serialize_size() ) && ( data ) && ( size ) && ( !state_save_load_begin((void*)data, size) ) )
	{
        /* read tag 0 */
        state_save_set_current_tag(0);
        if(state_save_load_continue())
            return false;

        /* loop over CPUs */
        for (cpunum = 0; cpunum < cpu_gettotalcpu(); cpunum++)
        {
            cpuintrf_push_context(cpunum);

            /* make sure banking is set */
            activecpu_reset_banking();

            /* load the CPU data */
            state_save_set_current_tag(cpunum + 1);
            if(state_save_load_continue())
                return false;

            cpuintrf_pop_context();
        }

        /* finish and close */
        state_save_load_finish();

        
        return true;
	}

	return false;
}

/******************************************************************************

  Sound

  osd_start_audio_stream() is called at the start of the emulation to initialize
  the output stream, then osd_update_audio_stream() is called every frame to
  feed new data. osd_stop_audio_stream() is called when the emulation is stopped.

  The sample rate is fixed at Machine->sample_rate. Samples are 16-bit, signed.
  When the stream is stereo, left and right samples are alternated in the
  stream.

  osd_start_audio_stream() and osd_update_audio_stream() must return the number
  of samples (or couples of samples, when using stereo) required for next frame.
  This will be around Machine->sample_rate / Machine->drv->frames_per_second,
  the code may adjust it by SMALL AMOUNTS to keep timing accurate and to
  maintain audio and video in sync when using vsync. Note that sound emulation,
  especially when DACs are involved, greatly depends on the number of samples
  per frame to be roughly constant, so the returned value must always stay close
  to the reference value of Machine->sample_rate / Machine->drv->frames_per_second.
  Of course that value is not necessarily an integer so at least a +/- 1
  adjustment is necessary to avoid drifting over time.

******************************************************************************/

int osd_start_audio_stream(int stereo)
{
    if  ( ( Machine->drv->frames_per_second * 1000 < options.samplerate) || (Machine->drv->frames_per_second < 60) )   Machine->sample_rate = Machine->drv->frames_per_second * 1000;
    else Machine->sample_rate = options.samplerate;

	delta_samples = 0.0f;
	usestereo = stereo ? 1 : 0;

	/* determine the number of samples per frame */
	samples_per_frame = Machine->sample_rate / Machine->drv->frames_per_second;
	orig_samples_per_frame = samples_per_frame;

	if (Machine->sample_rate == 0) return 0;

	samples_buffer = (short *) calloc(samples_per_frame+16, 2 + usestereo * 2);
	if (!usestereo) conversion_buffer = (short *) calloc(samples_per_frame+16, 4);
	
	return samples_per_frame;
}


int osd_update_audio_stream(INT16 *buffer)
{
	int i,j;
	
	if ( Machine->sample_rate !=0 && buffer )
	{
   		memcpy(samples_buffer, buffer, samples_per_frame * (usestereo ? 4 : 2));
		if (usestereo)
			audio_batch_cb(samples_buffer, samples_per_frame);
		else
		{
			for (i = 0, j = 0; i < samples_per_frame; i++)
        		{
				conversion_buffer[j++] = samples_buffer[i];
				conversion_buffer[j++] = samples_buffer[i];
		        }
         		audio_batch_cb(conversion_buffer,samples_per_frame);
		}	
		
			
		//process next frame
			
		if ( samples_per_frame  != orig_samples_per_frame ) samples_per_frame = orig_samples_per_frame;
		
		// dont drop any sample frames some games like mk will drift with time
   		delta_samples += (Machine->sample_rate / Machine->drv->frames_per_second) - orig_samples_per_frame;
		if ( delta_samples >= 1.0f )
		{
		
			int integer_delta = (int)delta_samples;
			if (integer_delta <= 16 )
                        { 
				log_cb(RETRO_LOG_DEBUG,"sound: Delta added value %d added to frame\n",integer_delta);
				samples_per_frame += integer_delta;
			}
			else if(integer_delta >= 16) log_cb(RETRO_LOG_INFO, "sound: Delta not added to samples_per_frame too large integer_delta:%d\n", integer_delta);
			else log_cb(RETRO_LOG_DEBUG,"sound(delta) no contitions met\n");	
			delta_samples -= integer_delta; 

		}
	}
        return samples_per_frame;
}

void osd_stop_audio_stream(void)
{
}

/******************************************************************************

Miscellaneous

******************************************************************************/

unsigned retro_get_region (void) {return RETRO_REGION_NTSC;}
void *retro_get_memory_data(unsigned type) {return 0;}
size_t retro_get_memory_size(unsigned type) {return 0;}
bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info){return false;}
void retro_cheat_reset(void){}
void retro_cheat_set(unsigned unused, bool unused1, const char* unused2){}
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_cb = cb; }


/******************************************************************************

	RetroPad Keymapping

******************************************************************************/

  /* Assuming the standard RetroPad layout:
   *
   *   [L2]                                 [R2]
   *   [L]                                   [R]
   *
   *     [^]                               [X]
   *
   * [<]     [>]    [start] [selct]    [Y]     [A]
   *
   *     [v]                               [B]
   *
   *
   * or standard RetroPad fight stick layout:
   *
   *   [start] [selct]
   *                                         [X]  [R]  [L]
   *     [^]                            [Y]
   *
   * [<]     [>]                             [A]  [R2] [L2]
   *                                    [B]
   *     [v]
   *
   *
   *
   * key: [MAME button/Street Fighter II move]
   *
   * PAD_GAMEPAD
   * ========================
   * Uses the fight stick & pad layout popularised by Street Figher IV.
   * Needs an 8+ button controller by default.
   *
   * [8/-]                                     [6/HK]  |
   * [7/-]                                     [3/HP]  |
   *                                                   |        [2/MP]  [3/HP]  [7/-]
   *     [^]                               [2/MP]      |  [1/LP]
   *                                                   |
   * [<]     [>]    [start] [selct]    [1/LP]  [5/MK]  |        [5/MK]  [6/HK]  [8/-]
   *                                                   |  [4/LK]
   *     [v]                               [4/LK]      |
   *                                                   |
   *
   * PAD_6BUTTON
   * ========================
   * Only needs a 6+ button controller by default, doesn't suit 8+ button fight sticks.
   *
   * [7/-]                                      [8/-]  |
   * [3/HP]                                    [6/HK]  |
   *                                                   |        [2/MP]  [6/HK]  [3/HP]
   *     [^]                               [2/MP]      |  [1/LP]
   *                                                   |
   * [<]     [>]    [start] [selct]    [1/LP]  [5/MK]  |        [5/MK]  [8/-]   [7/-]
   *                                                   |  [4/LK]
   *     [v]                               [4/LK]      |
   *                                                   |
   *
   * PAD_CLASSIC
   * ========================
   * Uses current MAME's default Xbox 360 controller layout.
   * Not sensible for 6 button fighters, but may suit other games.
   *
   * [7/-]                                     [8/-]   |
   * [5/MK]                                    [6/HK]  |
   *                                                   |        [4/WK]  [6/HK]  [5/MK]
   *     [^]                               [4/WK]      |  [3/HP]
   *                                                   |
   * [<]     [>]    [start] [selct]    [3/HP]  [2/MP]  |        [2/MP]  [8/-]   [7/-]
   *                                                   |  [1/LP]
   *     [v]                               [1/LP]      |
   *                                                   |
   */


/* libretro presents "Player 1", "Player 2", "Player 3", etc while internally using indexed data starting at 0, 1, 2 */
/* MAME presents "Player 1", "Player 2," "Player 3", and indexes them via enum values like JOYCODE_1_BUTTON1,        */
/* JOYCODE_2_BUTTON1, JOYCODE_3_BUTTON1 or with #define-d masks IPF_PLAYER1, IPF_PLAYER2, IPF_PLAYER3.               */
/*                                                                                                                   */
/* We are by convention passing "display" value used for mapping to MAME enums and player # masks to our macros.     */
/* (Display Index - 1) can be used for indexed data structures.                                                      */   

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
  environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, empty); /* is this necessary? it was in the sample code */
  options.retropad_layout[in_port] = device;
  retro_describe_controls();
}

#define NUMBER_OF_RETRO_TYPES (RETRO_DEVICE_ID_JOYPAD_R3 + 1)

void retro_describe_controls(void)
{
  int retro_type   = 0;
  int display_idx  = 0;
  
  struct retro_input_descriptor desc[(DISP_PLAYER6 * NUMBER_OF_RETRO_TYPES) +  1]; /* second + 1 for the final zeroed record. */
  struct retro_input_descriptor *needle = &desc[0];
  
  for(display_idx = DISP_PLAYER1; (display_idx <= options.ctrl_count && display_idx <= DISP_PLAYER6); display_idx++)
  {
    for(retro_type = RETRO_DEVICE_ID_JOYPAD_B; retro_type < NUMBER_OF_RETRO_TYPES; retro_type++)
    {
      const char *control_name;
      int mame_ctrl_id = get_mame_ctrl_id(display_idx, retro_type);

      if(mame_ctrl_id >= IPT_BUTTON1 && mame_ctrl_id <= IPT_BUTTON10)
      {
        if((mame_ctrl_id - IPT_BUTTON1 + 1) > options.button_count)
        {
          continue;
        }
      }

      switch(retro_type)
      {
        case RETRO_DEVICE_ID_JOYPAD_SELECT: control_name = "Coin";  break;
        case  RETRO_DEVICE_ID_JOYPAD_START: control_name = "Start"; break;
        default:                            control_name = game_driver->ctrl_dat->get_name(mame_ctrl_id); break;
      }

      if(string_is_empty(control_name))
        continue;

      needle->port = display_idx - 1;
      needle->device = RETRO_DEVICE_JOYPAD;
      needle->index = 0;
      needle->id = retro_type;
      needle->description = control_name;
      log_cb(RETRO_LOG_INFO, LOGPRE"Describing controls for: display_idx: %i | retro_type: %i | id: %i | desc: %s\n", display_idx, retro_type, needle->id, needle->description);
      needle++;
    }
  }

  /* the extra final record remains zeroed to indicate the end of the description to the frontend */ 
  needle->port = 0;
  needle->device = 0;
  needle->index = 0;
  needle->id = 0;
  needle->description = NULL;

  environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);
  
}

int get_mame_ctrl_id(int display_idx, int retro_ID)
{
  int player_flag;

  /* A few games have different control names per-player. The MAME player masks are only applied to those
     identified by mirrored_controls == false in controls.c.
  */
  if(!options.content_flags[CONTENT_MIRRORED_CTRLS])
  {
    switch(display_idx)
    {
      case 1: player_flag = IPF_PLAYER1; break;
      case 2: player_flag = IPF_PLAYER2; break;
      case 3: player_flag = IPF_PLAYER3; break;
      case 4: player_flag = IPF_PLAYER4; break;
      case 5: player_flag = IPF_PLAYER5; break;
      case 6: player_flag = IPF_PLAYER6; break;    
    }
  }
  else
    player_flag = 0;

  switch(retro_ID) /* universal default mappings */
  {
    case RETRO_DEVICE_ID_JOYPAD_LEFT:   return (player_flag | IPT_JOYSTICK_LEFT);
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return (player_flag | IPT_JOYSTICK_RIGHT);
    case RETRO_DEVICE_ID_JOYPAD_UP:     return (player_flag | IPT_JOYSTICK_UP);
    case RETRO_DEVICE_ID_JOYPAD_DOWN:   return (player_flag | IPT_JOYSTICK_DOWN);

    case RETRO_DEVICE_ID_JOYPAD_SELECT:
    {
      switch(display_idx)
      {
        case 1: return IPT_COIN1;
        case 2: return IPT_COIN2;
        case 3: return IPT_COIN3;
        case 4: return IPT_COIN4;
        case 5: return IPT_COIN5;
        case 6: return IPT_COIN6;
      }
    }
    case RETRO_DEVICE_ID_JOYPAD_START:
    {
      switch(display_idx)
      {
        case 1: return IPT_START1;
        case 2: return IPT_START2;
        case 3: return IPT_START3;
        case 4: return IPT_START4;
        case 5: return IPT_START5;
        case 6: return IPT_START6;
      }
    }
  }
  log_cb(RETRO_LOG_DEBUG, "display_idx: %i | options.retropad_layout[display_idx - 1]: %i\n", display_idx, options.retropad_layout[display_idx - 1]);
  switch(options.retropad_layout[display_idx - 1])
  {
    case RETRO_DEVICE_JOYPAD:
    {
      switch(retro_ID)
      {
        case RETRO_DEVICE_ID_JOYPAD_B:  return (player_flag | IPT_BUTTON4);
        case RETRO_DEVICE_ID_JOYPAD_Y:  return (player_flag | IPT_BUTTON1);
        case RETRO_DEVICE_ID_JOYPAD_X:  return (player_flag | IPT_BUTTON2);
        case RETRO_DEVICE_ID_JOYPAD_A:  return (player_flag | IPT_BUTTON5);
        case RETRO_DEVICE_ID_JOYPAD_L:  return (player_flag | IPT_BUTTON7);
        case RETRO_DEVICE_ID_JOYPAD_R:  return (player_flag | IPT_BUTTON3);
        case RETRO_DEVICE_ID_JOYPAD_L2: return (player_flag | IPT_BUTTON8);
        case RETRO_DEVICE_ID_JOYPAD_R2: return (player_flag | IPT_BUTTON6);
        case RETRO_DEVICE_ID_JOYPAD_L3: return (player_flag | IPT_BUTTON9);
        case RETRO_DEVICE_ID_JOYPAD_R3: return (player_flag | IPT_BUTTON10);
      }
      return 0;
    }
    case PAD_8BUTTON:
    {
      switch(retro_ID)
      {
	case RETRO_DEVICE_ID_JOYPAD_B:  return (player_flag | IPT_BUTTON4);
	case RETRO_DEVICE_ID_JOYPAD_Y:  return (player_flag | IPT_BUTTON1);
	case RETRO_DEVICE_ID_JOYPAD_X:  return (player_flag | IPT_BUTTON2);
	case RETRO_DEVICE_ID_JOYPAD_A:  return (player_flag | IPT_BUTTON5);
	case RETRO_DEVICE_ID_JOYPAD_L:  return (player_flag | IPT_BUTTON3);
	case RETRO_DEVICE_ID_JOYPAD_R:  return (player_flag | IPT_BUTTON7);
	case RETRO_DEVICE_ID_JOYPAD_L2: return (player_flag | IPT_BUTTON6);
	case RETRO_DEVICE_ID_JOYPAD_R2: return (player_flag | IPT_BUTTON8);
	case RETRO_DEVICE_ID_JOYPAD_L3: return (player_flag | IPT_BUTTON9);
	case RETRO_DEVICE_ID_JOYPAD_R3: return (player_flag | IPT_BUTTON10);
      }
      return 0;
    }
    case PAD_6BUTTON:
    {
      switch(retro_ID)
      {      
        case RETRO_DEVICE_ID_JOYPAD_B:  return (player_flag | IPT_BUTTON4);
        case RETRO_DEVICE_ID_JOYPAD_Y:  return (player_flag | IPT_BUTTON1);
        case RETRO_DEVICE_ID_JOYPAD_X:  return (player_flag | IPT_BUTTON2);
        case RETRO_DEVICE_ID_JOYPAD_A:  return (player_flag | IPT_BUTTON5);
        case RETRO_DEVICE_ID_JOYPAD_L:  return (player_flag | IPT_BUTTON3);
        case RETRO_DEVICE_ID_JOYPAD_R:  return (player_flag | IPT_BUTTON6);
        case RETRO_DEVICE_ID_JOYPAD_L2: return (player_flag | IPT_BUTTON7);
        case RETRO_DEVICE_ID_JOYPAD_R2: return (player_flag | IPT_BUTTON8);
        case RETRO_DEVICE_ID_JOYPAD_L3: return (player_flag | IPT_BUTTON9);
        case RETRO_DEVICE_ID_JOYPAD_R3: return (player_flag | IPT_BUTTON10);
      }
      return 0;
    }
    case PAD_CLASSIC:
    {
      switch(retro_ID)
      {
        case RETRO_DEVICE_ID_JOYPAD_B:  return (player_flag | IPT_BUTTON1);
        case RETRO_DEVICE_ID_JOYPAD_Y:  return (player_flag | IPT_BUTTON3);
        case RETRO_DEVICE_ID_JOYPAD_X:  return (player_flag | IPT_BUTTON4);
        case RETRO_DEVICE_ID_JOYPAD_A:  return (player_flag | IPT_BUTTON2);
        case RETRO_DEVICE_ID_JOYPAD_L:  return (player_flag | IPT_BUTTON5);
        case RETRO_DEVICE_ID_JOYPAD_R:  return (player_flag | IPT_BUTTON6);
        case RETRO_DEVICE_ID_JOYPAD_L2: return (player_flag | IPT_BUTTON7);
        case RETRO_DEVICE_ID_JOYPAD_R2: return (player_flag | IPT_BUTTON8);
        case RETRO_DEVICE_ID_JOYPAD_L3: return (player_flag | IPT_BUTTON9);
        case RETRO_DEVICE_ID_JOYPAD_R3: return (player_flag | IPT_BUTTON10);
      }
      return 0;
    }
  }
  return 0;  
}

#define DIRECTIONAL_COUNT           8  /* map Left, Right Up, Down as well as B, Y, A, X */
#define DIRECTIONAL_COUNT_NO_DBL    4
#define BUTTON_COUNT_PER           12 /* 10 MAME buttons plus Start and Select          */
#define MOUSE_BUTTON_PER            2

#define PER_PLAYER_CTRL_COUNT                   (DIRECTIONAL_COUNT + BUTTON_COUNT_PER + MOUSE_BUTTON_PER)
#define PER_PLAYER_CTRL_COUNT_NO_DBL_NO_MOUSE   (DIRECTIONAL_COUNT_NO_DBL + BUTTON_COUNT_PER)


#define EMIT_RETROPAD_GAMEPAD(DISPLAY_IDX) \
  {"RetroPad"   #DISPLAY_IDX " Left",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_LEFT,   JOYCODE_##DISPLAY_IDX##_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " Right",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_RIGHT,  JOYCODE_##DISPLAY_IDX##_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " Up",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_UP,     JOYCODE_##DISPLAY_IDX##_UP}, \
  {"RetroPad"   #DISPLAY_IDX " Down",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_DOWN,   JOYCODE_##DISPLAY_IDX##_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_RIGHT_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_RIGHT_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_RIGHT_UP}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_RIGHT_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_BUTTON4}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_BUTTON2}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_BUTTON5}, \
  {"RetroPad"   #DISPLAY_IDX " L",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L,      JOYCODE_##DISPLAY_IDX##_BUTTON7}, \
  {"RetroPad"   #DISPLAY_IDX " R",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R,      JOYCODE_##DISPLAY_IDX##_BUTTON3}, \
  {"RetroPad"   #DISPLAY_IDX " L2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L2,     JOYCODE_##DISPLAY_IDX##_BUTTON8}, \
  {"RetroPad"   #DISPLAY_IDX " R2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R2,     JOYCODE_##DISPLAY_IDX##_BUTTON6}, \
  {"RetroPad"   #DISPLAY_IDX " L3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L3,     JOYCODE_##DISPLAY_IDX##_BUTTON9}, \
  {"RetroPad"   #DISPLAY_IDX " R3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R3,     JOYCODE_##DISPLAY_IDX##_BUTTON10}, \
  {"RetroPad"   #DISPLAY_IDX " Start",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_START,  JOYCODE_##DISPLAY_IDX##_START}, \
  {"RetroPad"   #DISPLAY_IDX " Select",      ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_SELECT, JOYCODE_##DISPLAY_IDX##_SELECT}, \
  {"RetroMouse" #DISPLAY_IDX " Left Click",  ((DISPLAY_IDX - 1) * 18) + 16,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroMouse" #DISPLAY_IDX " Right Click", ((DISPLAY_IDX - 1) * 18) + 17,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON2},
  
#define EMIT_RETROPAD_8BUTTON(DISPLAY_IDX) \
  {"RetroPad"   #DISPLAY_IDX " Left",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_LEFT,   JOYCODE_##DISPLAY_IDX##_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " Right",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_RIGHT,  JOYCODE_##DISPLAY_IDX##_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " Up",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_UP,     JOYCODE_##DISPLAY_IDX##_UP}, \
  {"RetroPad"   #DISPLAY_IDX " Down",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_DOWN,   JOYCODE_##DISPLAY_IDX##_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_RIGHT_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_RIGHT_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_RIGHT_UP}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_RIGHT_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_BUTTON4}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_BUTTON2}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_BUTTON5}, \
  {"RetroPad"   #DISPLAY_IDX " L",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L,      JOYCODE_##DISPLAY_IDX##_BUTTON3}, \
  {"RetroPad"   #DISPLAY_IDX " R",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R,      JOYCODE_##DISPLAY_IDX##_BUTTON7}, \
  {"RetroPad"   #DISPLAY_IDX " L2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L2,     JOYCODE_##DISPLAY_IDX##_BUTTON6}, \
  {"RetroPad"   #DISPLAY_IDX " R2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R2,     JOYCODE_##DISPLAY_IDX##_BUTTON8}, \
  {"RetroPad"   #DISPLAY_IDX " L3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L3,     JOYCODE_##DISPLAY_IDX##_BUTTON9}, \
  {"RetroPad"   #DISPLAY_IDX " R3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R3,     JOYCODE_##DISPLAY_IDX##_BUTTON10}, \
  {"RetroPad"   #DISPLAY_IDX " Start",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_START,  JOYCODE_##DISPLAY_IDX##_START}, \
  {"RetroPad"   #DISPLAY_IDX " Select",      ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_SELECT, JOYCODE_##DISPLAY_IDX##_SELECT}, \
  {"RetroMouse" #DISPLAY_IDX " Left Click",  ((DISPLAY_IDX - 1) * 18) + 16,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroMouse" #DISPLAY_IDX " Right Click", ((DISPLAY_IDX - 1) * 18) + 17,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON2},

#define EMIT_RETROPAD_6BUTTON(DISPLAY_IDX) \
  {"RetroPad"   #DISPLAY_IDX " Left",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_LEFT,   JOYCODE_##DISPLAY_IDX##_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " Right",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_RIGHT,  JOYCODE_##DISPLAY_IDX##_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " Up",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_UP,     JOYCODE_##DISPLAY_IDX##_UP}, \
  {"RetroPad"   #DISPLAY_IDX " Down",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_DOWN,   JOYCODE_##DISPLAY_IDX##_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_RIGHT_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_RIGHT_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_RIGHT_UP}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_RIGHT_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_BUTTON4}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_BUTTON2}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_BUTTON5}, \
  {"RetroPad"   #DISPLAY_IDX " L",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L,      JOYCODE_##DISPLAY_IDX##_BUTTON3}, \
  {"RetroPad"   #DISPLAY_IDX " R",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R,      JOYCODE_##DISPLAY_IDX##_BUTTON6}, \
  {"RetroPad"   #DISPLAY_IDX " L2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L2,     JOYCODE_##DISPLAY_IDX##_BUTTON7}, \
  {"RetroPad"   #DISPLAY_IDX " R2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R2,     JOYCODE_##DISPLAY_IDX##_BUTTON8}, \
  {"RetroPad"   #DISPLAY_IDX " L3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L3,     JOYCODE_##DISPLAY_IDX##_BUTTON9}, \
  {"RetroPad"   #DISPLAY_IDX " R3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R3,     JOYCODE_##DISPLAY_IDX##_BUTTON10}, \
  {"RetroPad"   #DISPLAY_IDX " Start",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_START,  JOYCODE_##DISPLAY_IDX##_START}, \
  {"RetroPad"   #DISPLAY_IDX " Select",      ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_SELECT, JOYCODE_##DISPLAY_IDX##_SELECT}, \
  {"RetroMouse" #DISPLAY_IDX " Left Click",  ((DISPLAY_IDX - 1) * 18) + 16,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroMouse" #DISPLAY_IDX " Right Click", ((DISPLAY_IDX - 1) * 18) + 17,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON2},
  
#define EMIT_RETROPAD_CLASSIC(DISPLAY_IDX) \
  {"RetroPad"   #DISPLAY_IDX " Left",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_LEFT,   JOYCODE_##DISPLAY_IDX##_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " Right",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_RIGHT,  JOYCODE_##DISPLAY_IDX##_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " Up",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_UP,     JOYCODE_##DISPLAY_IDX##_UP}, \
  {"RetroPad"   #DISPLAY_IDX " Down",        ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_DOWN,   JOYCODE_##DISPLAY_IDX##_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_RIGHT_DOWN}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_RIGHT_LEFT}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_RIGHT_UP}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_RIGHT_RIGHT}, \
  {"RetroPad"   #DISPLAY_IDX " B",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_B,      JOYCODE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroPad"   #DISPLAY_IDX " Y",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_Y,      JOYCODE_##DISPLAY_IDX##_BUTTON3}, \
  {"RetroPad"   #DISPLAY_IDX " X",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_X,      JOYCODE_##DISPLAY_IDX##_BUTTON4}, \
  {"RetroPad"   #DISPLAY_IDX " A",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_A,      JOYCODE_##DISPLAY_IDX##_BUTTON2}, \
  {"RetroPad"   #DISPLAY_IDX " L",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L,      JOYCODE_##DISPLAY_IDX##_BUTTON5}, \
  {"RetroPad"   #DISPLAY_IDX " R",           ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R,      JOYCODE_##DISPLAY_IDX##_BUTTON6}, \
  {"RetroPad"   #DISPLAY_IDX " L2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L2,     JOYCODE_##DISPLAY_IDX##_BUTTON7}, \
  {"RetroPad"   #DISPLAY_IDX " R2",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R2,     JOYCODE_##DISPLAY_IDX##_BUTTON8}, \
  {"RetroPad"   #DISPLAY_IDX " L3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_L3,     JOYCODE_##DISPLAY_IDX##_BUTTON9}, \
  {"RetroPad"   #DISPLAY_IDX " R3",          ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_R3,     JOYCODE_##DISPLAY_IDX##_BUTTON10}, \
  {"RetroPad"   #DISPLAY_IDX " Start",       ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_START,  JOYCODE_##DISPLAY_IDX##_START}, \
  {"RetroPad"   #DISPLAY_IDX " Select",      ((DISPLAY_IDX - 1) * 18) + RETRO_DEVICE_ID_JOYPAD_SELECT, JOYCODE_##DISPLAY_IDX##_SELECT}, \
  {"RetroMouse" #DISPLAY_IDX " Left Click",  ((DISPLAY_IDX - 1) * 18) + 16,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON1}, \
  {"RetroMouse" #DISPLAY_IDX " Right Click", ((DISPLAY_IDX - 1) * 18) + 17,                            JOYCODE_MOUSE_##DISPLAY_IDX##_BUTTON2},

struct JoystickInfo alternate_joystick_maps[PLAYER_COUNT][IDX_PAD_end][PER_PLAYER_CTRL_COUNT] =
{
  {{EMIT_RETROPAD_GAMEPAD(1)},{EMIT_RETROPAD_8BUTTON(1)},{EMIT_RETROPAD_6BUTTON(1)},{EMIT_RETROPAD_CLASSIC(1)}},
  {{EMIT_RETROPAD_GAMEPAD(2)},{EMIT_RETROPAD_8BUTTON(2)},{EMIT_RETROPAD_6BUTTON(2)},{EMIT_RETROPAD_CLASSIC(2)}},
  {{EMIT_RETROPAD_GAMEPAD(3)},{EMIT_RETROPAD_8BUTTON(3)},{EMIT_RETROPAD_6BUTTON(3)},{EMIT_RETROPAD_CLASSIC(3)}},
  {{EMIT_RETROPAD_GAMEPAD(4)},{EMIT_RETROPAD_8BUTTON(4)},{EMIT_RETROPAD_6BUTTON(4)},{EMIT_RETROPAD_CLASSIC(4)}},
  {{EMIT_RETROPAD_GAMEPAD(5)},{EMIT_RETROPAD_8BUTTON(5)},{EMIT_RETROPAD_6BUTTON(5)},{EMIT_RETROPAD_CLASSIC(5)}},
  {{EMIT_RETROPAD_GAMEPAD(6)},{EMIT_RETROPAD_8BUTTON(6)},{EMIT_RETROPAD_6BUTTON(6)},{EMIT_RETROPAD_CLASSIC(6)}},
};


/******************************************************************************

	Joystick & Mouse/Trackball

******************************************************************************/

int retroJsState[72];
int16_t mouse_x[4];
int16_t mouse_y[4];
int16_t analogjoy[4][4];

struct JoystickInfo mame_joy_map[(PLAYER_COUNT * PER_PLAYER_CTRL_COUNT) + 1]; /* + 1 for final zeroed struct */

const struct JoystickInfo *osd_get_joy_list(void)
{
  int player_map_idx = 0;
  int overall_idx    = 0;
  int display_idx    = 0;
   
  for(display_idx = DISP_PLAYER1; display_idx <= DISP_PLAYER6; display_idx++)
  {
    for(player_map_idx = 0; player_map_idx < PER_PLAYER_CTRL_COUNT; player_map_idx++)
    {
      int data_idx     = display_idx - 1;
      int coded_layout = options.retropad_layout[data_idx];
      int layout_idx   = 0;
      
      switch(coded_layout)
      {
        case RETRO_DEVICE_JOYPAD: layout_idx = IDX_GAMEPAD; break;
        case PAD_8BUTTON:         layout_idx = IDX_8BUTTON; break;
        case PAD_6BUTTON:         layout_idx = IDX_6BUTTON; break;
        case PAD_CLASSIC:         layout_idx = IDX_CLASSIC; break;
      }
 
      mame_joy_map[overall_idx++] = alternate_joystick_maps[data_idx][layout_idx][player_map_idx];
    }
  }

  /* the extra final record remains zeroed to indicate the end of the description to the frontend */ 
  mame_joy_map[overall_idx].name         = NULL;
  mame_joy_map[overall_idx].code         = 0;
  mame_joy_map[overall_idx].standardcode = 0;
  
  return mame_joy_map;
}

int osd_is_joy_pressed(int joycode)
{
  if(options.input_interface == RETRO_DEVICE_KEYBOARD)
    return 0;
  return (joycode >= 0) ? retroJsState[joycode] : 0;
}

int osd_is_joystick_axis_code(int joycode)
{
    return 0;
}

void osd_lightgun_read(int player, int *deltax, int *deltay)
{

}

void osd_trak_read(int player, int *deltax, int *deltay)
{
    *deltax = mouse_x[player];
    *deltay = mouse_y[player];
}

int convert_analog_scale(int input)
{
    static int libretro_analog_range = LIBRETRO_ANALOG_MAX - LIBRETRO_ANALOG_MIN;
    static int analog_range = ANALOG_MAX - ANALOG_MIN;

    return (input - LIBRETRO_ANALOG_MIN)*analog_range / libretro_analog_range + ANALOG_MIN;
}

void osd_analogjoy_read(int player,int analog_axis[MAX_ANALOG_AXES], InputCode analogjoy_input[MAX_ANALOG_AXES])
{
    int i;
    for (i = 0; i < MAX_ANALOG_AXES; i ++)
    {
        if (analogjoy[player][i])
            analog_axis[i] = convert_analog_scale(analogjoy[player][i]);
    }

    analogjoy_input[0] = IPT_AD_STICK_X;
    analogjoy_input[1] = IPT_AD_STICK_Y;
}

void osd_customize_inputport_defaults(struct ipd *defaults)
{
  unsigned int i = 0;
  default_inputs = defaults;

  for( ; default_inputs[i].type != IPT_END; ++i)
  {
    struct ipd *entry = &default_inputs[i];

    if(options.dual_joysticks)
    {
      switch(entry->type)
      {
         case (IPT_JOYSTICKRIGHT_UP   | IPF_PLAYER1):
            seq_set_1(entry->seq, JOYCODE_2_UP);
            break;
         case (IPT_JOYSTICKRIGHT_DOWN | IPF_PLAYER1):
            seq_set_1(entry->seq, JOYCODE_2_DOWN);
            break;
         case (IPT_JOYSTICKRIGHT_LEFT | IPF_PLAYER1):
            seq_set_1(entry->seq, JOYCODE_2_LEFT);
            break;
         case (IPT_JOYSTICKRIGHT_RIGHT | IPF_PLAYER1):
            seq_set_1(entry->seq, JOYCODE_2_RIGHT);
            break;
         case (IPT_JOYSTICK_UP   | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_UP);
            break;
         case (IPT_JOYSTICK_DOWN | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_DOWN);
            break;
         case (IPT_JOYSTICK_LEFT | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_LEFT);
            break;
         case (IPT_JOYSTICK_RIGHT | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_RIGHT);
            break; 
         case (IPT_JOYSTICKRIGHT_UP   | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_4_UP);
            break;
         case (IPT_JOYSTICKRIGHT_DOWN | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_4_DOWN);
            break;
         case (IPT_JOYSTICKRIGHT_LEFT | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_4_LEFT);
            break;
         case (IPT_JOYSTICKRIGHT_RIGHT | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_4_RIGHT);
            break;
         case (IPT_JOYSTICKLEFT_UP   | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_UP);
            break;
         case (IPT_JOYSTICKLEFT_DOWN | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_DOWN);
            break;
         case (IPT_JOYSTICKLEFT_LEFT | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_LEFT);
            break;
         case (IPT_JOYSTICKLEFT_RIGHT | IPF_PLAYER2):
            seq_set_1(entry->seq, JOYCODE_3_RIGHT);
            break;
     }
    }
   }
}

/* These calibration functions should never actually be used (as long as needs_calibration returns 0 anyway).*/
int osd_joystick_needs_calibration(void) { return 0; }
void osd_joystick_start_calibration(void){ }
const char *osd_joystick_calibrate_next(void) { return 0; }
void osd_joystick_calibrate(void) { }
void osd_joystick_end_calibration(void) { }


/******************************************************************************

	Keyboard
  
******************************************************************************/

extern const struct KeyboardInfo retroKeys[];
int retroKeyState[512];

const struct KeyboardInfo *osd_get_key_list(void)
{
  return retroKeys;
}

int osd_is_key_pressed(int keycode)
{
  if(options.input_interface == RETRO_DEVICE_JOYPAD)
    return 0;
  return (keycode < 512 && keycode >= 0) ? retroKeyState[keycode] : 0;
}

int osd_readkey_unicode(int flush)
{
  /* TODO*/
  return 0;
}

/* Unassigned keycodes*/
/*	KEYCODE_OPENBRACE, KEYCODE_CLOSEBRACE, KEYCODE_BACKSLASH2, KEYCODE_STOP, KEYCODE_LWIN, KEYCODE_RWIN, KEYCODE_DEL_PAD, KEYCODE_PAUSE,*/

/* The format for each systems key constants is RETROK_$(TAG) and KEYCODE_$(TAG)*/
/* EMIT1(TAG): The tag value is the same between libretro and MAME*/
/* EMIT2(RTAG, MTAG): The tag value is different between the two*/
/* EXITX(TAG): MAME has no equivalent key.*/

#define EMIT2(RETRO, KEY) {(char*)#RETRO, RETROK_##RETRO, KEYCODE_##KEY}
#define EMIT1(KEY) {(char*)#KEY, RETROK_##KEY, KEYCODE_##KEY}
#define EMITX(KEY) {(char*)#KEY, RETROK_##KEY, KEYCODE_OTHER}

const struct KeyboardInfo retroKeys[] =
{
    EMIT1(BACKSPACE),
    EMIT1(TAB),
    EMITX(CLEAR),
    
    EMIT1(BACKSPACE),
    EMIT1(TAB),
    EMITX(CLEAR),
    EMIT2(RETURN, ENTER),
    EMITX(PAUSE),
    EMIT2(ESCAPE, ESC),
    EMIT1(SPACE),
    EMITX(EXCLAIM),
    EMIT2(QUOTEDBL, TILDE),
    EMITX(HASH),
    EMITX(DOLLAR),
    EMITX(AMPERSAND),
    EMIT1(QUOTE),
    EMITX(LEFTPAREN),
    EMITX(RIGHTPAREN),
    EMIT1(ASTERISK),
    EMIT2(PLUS, EQUALS),
    EMIT1(COMMA),
    EMIT1(MINUS),
    EMITX(PERIOD),
    EMIT1(SLASH),
    
    EMIT1(0), EMIT1(1), EMIT1(2), EMIT1(3), EMIT1(4), EMIT1(5), EMIT1(6), EMIT1(7), EMIT1(8), EMIT1(9),
    
    EMIT1(COLON),
    EMITX(SEMICOLON),
    EMITX(LESS),
    EMITX(EQUALS),
    EMITX(GREATER),
    EMITX(QUESTION),
    EMITX(AT),
    EMITX(LEFTBRACKET),
    EMIT1(BACKSLASH),
    EMITX(RIGHTBRACKET),
    EMITX(CARET),
    EMITX(UNDERSCORE),
    EMITX(BACKQUOTE),
    
    EMIT2(a, A), EMIT2(b, B), EMIT2(c, C), EMIT2(d, D), EMIT2(e, E), EMIT2(f, F),
    EMIT2(g, G), EMIT2(h, H), EMIT2(i, I), EMIT2(j, J), EMIT2(k, K), EMIT2(l, L),
    EMIT2(m, M), EMIT2(n, N), EMIT2(o, O), EMIT2(p, P), EMIT2(q, Q), EMIT2(r, R),
    EMIT2(s, S), EMIT2(t, T), EMIT2(u, U), EMIT2(v, V), EMIT2(w, W), EMIT2(x, X),
    EMIT2(y, Y), EMIT2(z, Z),
    
    EMIT2(DELETE, DEL),

    EMIT2(KP0, 0_PAD), EMIT2(KP1, 1_PAD), EMIT2(KP2, 2_PAD), EMIT2(KP3, 3_PAD),
    EMIT2(KP4, 4_PAD), EMIT2(KP5, 5_PAD), EMIT2(KP6, 6_PAD), EMIT2(KP7, 7_PAD),
    EMIT2(KP8, 8_PAD), EMIT2(KP9, 9_PAD),
    
    EMITX(KP_PERIOD),
    EMIT2(KP_DIVIDE, SLASH_PAD),
    EMITX(KP_MULTIPLY),
    EMIT2(KP_MINUS, MINUS_PAD),
    EMIT2(KP_PLUS, PLUS_PAD),
    EMIT2(KP_ENTER, ENTER_PAD),
    EMITX(KP_EQUALS),

    EMIT1(UP), EMIT1(DOWN), EMIT1(RIGHT), EMIT1(LEFT),
    EMIT1(INSERT), EMIT1(HOME), EMIT1(END), EMIT2(PAGEUP, PGUP), EMIT2(PAGEDOWN, PGDN),

    EMIT1(F1), EMIT1(F2), EMIT1(F3), EMIT1(F4), EMIT1(F5), EMIT1(F6),
    EMIT1(F7), EMIT1(F8), EMIT1(F9), EMIT1(F10), EMIT1(F11), EMIT1(F12),
    EMITX(F13), EMITX(F14), EMITX(F15),

    EMIT1(NUMLOCK),
    EMIT1(CAPSLOCK),
    EMIT2(SCROLLOCK, SCRLOCK),
    EMIT1(RSHIFT), EMIT1(LSHIFT), EMIT2(RCTRL, RCONTROL), EMIT2(LCTRL, LCONTROL), EMIT1(RALT), EMIT1(LALT),
    EMITX(RMETA), EMITX(LMETA), EMITX(LSUPER), EMITX(RSUPER),
    
    EMITX(MODE),
    EMITX(COMPOSE),

    EMITX(HELP),
    EMIT2(PRINT, PRTSCR),
    EMITX(SYSREQ),
    EMITX(BREAK),
    EMIT1(MENU),
    EMITX(POWER),
    EMITX(EURO),
    EMITX(UNDO),

    {0, 0, 0}
};

