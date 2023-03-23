/*
   This file is part of DirectFB.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
*/

#include <core/screens.h>
#include <misc/conf.h>

#include "egl_system.h"

D_DEBUG_DOMAIN( EGL_Screen, "EGL/Screen", "EGL Screen" );

/**********************************************************************************************************************/

typedef struct {
     int rotation;
} EGLScreenData;

static const char *panel_orientation_table[] = {
     "Normal", "Upside Down", "Left Side Up", "Right Side Up"
};

static int hor[] = {
      640,  720,  720,  800, 1024, 1152, 1280, 1280, 1280, 1280, 1400, 1600, 1920, 960, 1440, 800, 1024, 1366, 1920,
     2560, 2560, 3840, 4096
};

static int ver[] = {
      480,  480,  576,  600,  768,  864,  720,  768,  960, 1024, 1050, 1200, 1080, 540,  540, 480,  600,  768, 1200,
     1440, 1600, 2160, 2160
};

/**********************************************************************************************************************/

static int
eglScreenDataSize()
{
     return sizeof(EGLScreenData);
}

static DFBResult
eglInitScreen( CoreScreen           *screen,
               void                 *driver_data,
               void                 *screen_data,
               DFBScreenDescription *description )
{
     EGLData       *egl  = driver_data;
     EGLDataShared *shared;
     EGLScreenData *data = screen_data;
     int            width, height;

     D_DEBUG_AT( EGL_Screen, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );
     D_ASSERT( data != NULL );

     shared = egl->shared;

     /* Set capabilities. */
     description->caps    = DSCCAPS_OUTPUTS;
     description->outputs = 1;

     /* Set name. */
     snprintf( description->name, DFB_SCREEN_DESC_NAME_LENGTH, "EGL Screen" );

     width  = dfb_config->mode.width  ?: egl->size.w;
     height = dfb_config->mode.height ?: egl->size.h;

     D_INFO( "EGL/Screen: Default mode is %dx%d\n", width, height );

     /* Get the layer rotation. */
     if (dfb_config->layers[dfb_config->primary_layer].rotate_set) {
          data->rotation = dfb_config->layers[dfb_config->primary_layer].rotate;
     }
     else {
          drmModeObjectProperties *props;
          drmModePropertyRes      *prop;
          int                      i;

          data->rotation = 0;

          props = drmModeObjectGetProperties( egl->fd, egl->connector->connector_id, DRM_MODE_OBJECT_CONNECTOR );
          if (!props)
               return DFB_OK;

          for (i = 0; i < props->count_props; i++) {
               prop = drmModeGetProperty( egl->fd, props->props[i] );

               if (!strcmp( prop->name, "panel orientation" )) {
                    D_ASSUME( props->prop_values[i] >= 0 && props->prop_values[i] <= 3 );

                    for (i = 0; i < prop->count_enums; i++) {
                         if (!strcmp( panel_orientation_table[props->prop_values[i]], "Upside Down" ))
                              data->rotation = 180;
                         else if (!strcmp( panel_orientation_table[props->prop_values[i]], "Left Side Up" ))
                              data->rotation = 270;
                         else if (!strcmp( panel_orientation_table[props->prop_values[i]], "Right Side Up" ))
                              data->rotation = 90;
                    }

                    D_INFO( "EGL/Screen: Using %s panel orientation (rotation = %d)\n",
                            panel_orientation_table[props->prop_values[i]], data->rotation );
                    break;
               }

               drmModeFreeProperty( prop );
          }

          drmModeFreeObjectProperties( props );
     }

     if (data->rotation == 90 || data->rotation == 270) {
          shared->mode.w = height <= width ? width : (float) height * height / width;
          shared->mode.h = height <= width ? (float) width * width / height : height;
     }
     else {
          shared->mode.w = width;
          shared->mode.h = height;
     }

     return DFB_OK;
}

static DFBResult
eglInitOutput( CoreScreen                 *screen,
               void                       *driver_data,
               void                       *screen_data,
               int                         output,
               DFBScreenOutputDescription *description,
               DFBScreenOutputConfig      *config )
{
     EGLData       *egl = driver_data;
     EGLDataShared *shared;
     int            j;

     D_DEBUG_AT( EGL_Screen, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );

     shared = egl->shared;

     /* Set capabilities. */
     description->caps = DSOCAPS_RESOLUTION;

     /* Set name. */
     snprintf( description->name, DFB_SCREEN_OUTPUT_DESC_NAME_LENGTH, "EGL Output" );

     config->flags = DSOCONF_RESOLUTION;

     config->resolution = DSOR_UNKNOWN;

     for (j = 0; j < D_ARRAY_SIZE(hor); j++) {
          if (shared->mode.w == hor[j] && shared->mode.h == ver[j]) {
               config->resolution = 1 << j;
               break;
          }
     }

     return DFB_OK;
}

static DFBResult
eglSetOutputConfig( CoreScreen                  *screen,
                    void                        *driver_data,
                    void                        *screen_data,
                    int                          output,
                    const DFBScreenOutputConfig *config )
{
     EGLData       *egl    = driver_data;
     EGLDataShared *shared = egl->shared;
     int            res;

     D_DEBUG_AT( EGL_Screen, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );

     if (config->flags != DSOCONF_RESOLUTION)
          return DFB_INVARG;

     res = D_BITn32( config->resolution );
     if (res == -1 || res >= D_ARRAY_SIZE(hor))
          return DFB_INVARG;

     shared->mode.w = hor[res];
     shared->mode.h = ver[res];

     return DFB_OK;
}

static DFBResult
eglGetScreenSize( CoreScreen *screen,
                  void       *driver_data,
                  void       *screen_data,
                  int        *ret_width,
                  int        *ret_height )
{
     EGLData       *egl = driver_data;
     EGLDataShared *shared;

     D_DEBUG_AT( EGL_Screen, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );

     shared = egl->shared;

     *ret_width  = shared->mode.w;
     *ret_height = shared->mode.h;

     return DFB_OK;
}

static DFBResult
eglGetScreenRotation( CoreScreen *screen,
                      void       *driver_data,
                      void       *screen_data,
                      int        *ret_rotation )
{
     EGLScreenData *data = screen_data;

     D_DEBUG_AT( EGL_Screen, "%s()\n", __FUNCTION__ );

     D_ASSERT( data != NULL );

     *ret_rotation = data->rotation;

     return DFB_OK;
}

const ScreenFuncs eglScreenFuncs = {
     .ScreenDataSize    = eglScreenDataSize,
     .InitScreen        = eglInitScreen,
     .InitOutput        = eglInitOutput,
     .SetOutputConfig   = eglSetOutputConfig,
     .GetScreenSize     = eglGetScreenSize,
     .GetScreenRotation = eglGetScreenRotation
};
