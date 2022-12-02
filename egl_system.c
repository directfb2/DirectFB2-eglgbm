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

#include <core/core.h>
#include <core/core_system.h>
#include <core/layers.h>
#include <core/screens.h>
#include <core/surface_pool.h>
#include <fusion/shmalloc.h>

#include "egl_system.h"

D_DEBUG_DOMAIN( EGL_System, "EGL/System", "EGL System Module" );

DFB_CORE_SYSTEM( eglgbm )

/**********************************************************************************************************************/

extern const ScreenFuncs       eglScreenFuncs;
extern const DisplayLayerFuncs eglPrimaryLayerFuncs;
extern const SurfacePoolFuncs  eglSurfacePoolFuncs;

static DFBResult
local_init( const char *device_name,
            EGLData    *egl )
{
     CoreScreen   *screen;
     EGLConfig     config;
     EGLint        num_config;
     const EGLint  config_attr[]  = { EGL_RED_SIZE,   8,
                                      EGL_GREEN_SIZE, 8,
                                      EGL_BLUE_SIZE,  8,
                                      EGL_ALPHA_SIZE, 8,
                                      EGL_NONE };
     const EGLint  context_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2,
                                      EGL_NONE };
     int           i;

     /* Open EGL display. */
     egl->fd = open( device_name, O_RDWR );
     if (egl->fd < 0) {
          D_PERROR( "EGL/System: Failed to open '%s'!\n", device_name );
          return DFB_INIT;
     }

     egl->gbm = gbm_create_device( egl->fd );
     if (!egl->gbm) {
          D_ERROR( "EGL/System: gbm_create_device() failed!\n" );
          return DFB_INIT;
     }

     egl->eglDisplay = eglGetDisplay( egl->gbm );
     if (!egl->eglDisplay) {
          D_ERROR( "EGL/System: eglGetDisplay() failed: 0x%x!\n", (unsigned int) eglGetError() );
          return DFB_INIT;
     }

     if (!eglInitialize( egl->eglDisplay, NULL, NULL )) {
          D_ERROR( "EGL/System: eglInitialize() failed: 0x%x!\n", (unsigned int) eglGetError() );
          return DFB_INIT;
     }

     if (!eglChooseConfig( egl->eglDisplay, config_attr, &config, 1, &num_config ) || (num_config != 1)) {
          D_ERROR("DirectFB/EGL: eglChooseConfig() failed: 0x%x!\n", (unsigned int) eglGetError() );
          return DFB_INIT;
     }

     /* Retrieve display information. */
     egl->resources = drmModeGetResources( egl->fd );
     if (!egl->resources) {
          D_PERROR( "EGL/System: Could not retrieve resources!\n" );
          return DFB_INIT;
     }

     for (i = 0; i < egl->resources->count_connectors; i++) {
          egl->connector = drmModeGetConnector( egl->fd, egl->resources->connectors[i] );
          if (!egl->connector)
               continue;

          if (egl->connector->connection == DRM_MODE_CONNECTED)
               break;

          drmModeFreeConnector( egl->connector );
          egl->connector = NULL;
     }

     if (!egl->connector) {
          D_ERROR( "EGL/System: Cannot find connector!\n" );
          return DFB_INIT;
     }

     if (egl->connector->encoder_id) {
          egl->encoder = drmModeGetEncoder( egl->fd, egl->connector->encoder_id );
     }
     else {
          for (i = 0; i < egl->resources->count_encoders; i++) {
               egl->encoder = drmModeGetEncoder( egl->fd, egl->connector->encoders[i] );
               if (!egl->encoder)
                    continue;

               break;
          }
     }

     if (!egl->encoder) {
          D_ERROR( "EGL/System: Cannot find encoder!\n" );
          return DFB_INIT;
     }

     if (egl->encoder->crtc_id) {
          egl->crtc = drmModeGetCrtc( egl->fd, egl->encoder->crtc_id );
     }
     else {
          for (i = 0; i < egl->resources->count_crtcs; i++) {
               if (egl->encoder->possible_crtcs & (1 << i))
                    break;
          }

          egl->crtc = drmModeGetCrtc( egl->fd, egl->resources->crtcs[i] );
     }

     if (!egl->crtc) {
          D_ERROR( "EGL/System: Cannot find crtc!\n" );
          return DFB_INIT;
     }

     egl->size.w = egl->connector->modes[0].hdisplay;
     egl->size.h = egl->connector->modes[0].vdisplay;

     D_INFO( "EGL/System: Found display configuration\n" );

     /* Create EGL window surface. */
     egl->gbm_surface = gbm_surface_create( egl->gbm, egl->size.w, egl->size.h, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT );
     if (!egl->gbm_surface) {
          D_ERROR( "EGL/System: gbm_surface_create() failed!\n" );
          return DFB_INIT;
     }

     egl->eglSurface = eglCreateWindowSurface( egl->eglDisplay, config, egl->gbm_surface, NULL );
     if (!egl->eglSurface) {
          D_ERROR( "EGL/System: eglCreateWindowSurface() failed: 0x%x!\n", (unsigned int) eglGetError() );
          return DFB_INIT;
     }

     /* Create EGL context and attach it to the EGL window surface. */
     egl->eglContext = eglCreateContext( egl->eglDisplay, config, EGL_NO_CONTEXT, context_attr );
     if (!egl->eglContext) {
          D_ERROR( "EGL/System: eglCreateContext() failed: 0x%x!\n", (unsigned int) eglGetError() );
          return DFB_INIT;
     }

     if (!eglMakeCurrent( egl->eglDisplay, egl->eglSurface, egl->eglSurface, egl->eglContext )) {
          D_ERROR( "EGL/System: eglMakeCurrent() failed: 0x%x!\n", (unsigned int) eglGetError() );
          return DFB_INIT;
     }

     screen = dfb_screens_register( egl, &eglScreenFuncs );

     dfb_layers_register( screen, egl, &eglPrimaryLayerFuncs );

     return DFB_OK;
}

static DFBResult
local_deinit( EGLData *egl )
{
     if (egl->eglContext) {
          eglMakeCurrent( egl->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
          eglDestroyContext( egl->eglDisplay, egl->eglContext );
     }

     if (egl->eglSurface)
          eglDestroySurface( egl->eglDisplay, egl->eglSurface );

     if (egl->gbm_surface)
          gbm_surface_destroy( egl->gbm_surface );

     if (egl->crtc) {
          drmModeSetCrtc( egl->fd, egl->crtc->crtc_id, egl->crtc->buffer_id, egl->crtc->x, egl->crtc->y,
                          &egl->connector->connector_id, 1, &egl->crtc->mode );
          drmModeFreeCrtc( egl->crtc );
     }

     if (egl->encoder)
          drmModeFreeEncoder( egl->encoder );

     if (egl->connector)
          drmModeFreeConnector( egl->connector );

     if (egl->resources)
         drmModeFreeResources( egl->resources );

     if (egl->eglDisplay)
          eglTerminate( egl->eglDisplay );

     if (egl->gbm)
          gbm_device_destroy( egl->gbm );

     if (egl->fd != -1)
          close( egl->fd );

     return DFB_OK;
}

/**********************************************************************************************************************/

static void
system_get_info( CoreSystemInfo *info )
{
     info->version.major = 0;
     info->version.minor = 1;

     info->caps = CSCAPS_ACCELERATION | CSCAPS_ALWAYS_INDIRECT;

     snprintf( info->name,   DFB_CORE_SYSTEM_INFO_NAME_LENGTH,   "EGL" );
     snprintf( info->vendor, DFB_CORE_SYSTEM_INFO_VENDOR_LENGTH, "DirectFB" );
}

static DFBResult
system_initialize( CoreDFB  *core,
                   void    **ret_data )
{
     DFBResult            ret;
     EGLData             *egl;
     EGLDataShared       *shared;
     FusionSHMPoolShared *pool;
     const char          *value;

     D_DEBUG_AT( EGL_System, "%s()\n", __FUNCTION__ );

     egl = D_CALLOC( 1, sizeof(EGLData) );
     if (!egl)
          return D_OOM();

     egl->core = core;

     pool = dfb_core_shmpool( core );

     shared = SHCALLOC( pool, 1, sizeof(EGLDataShared) );
     if (!shared) {
          D_FREE( egl );
          return D_OOSHM();
     }

     shared->shmpool = pool;

     egl->shared = shared;

     if ((value = direct_config_get_value( "eglgbm" ))) {
          direct_snputs( shared->device_name, value, 255 );
          D_INFO( "EGL/System: Using device %s as specified in DirectFB configuration\n", shared->device_name );
     }
     else if (getenv( "DRICARD" ) && *getenv( "DRICARD" ) != '\0') {
          direct_snputs( shared->device_name, getenv( "DRICARD" ), 255 );
          D_INFO( "EGL/System: Using device %s as set in DRICARD environment variable\n", shared->device_name );
     }
     else {
          snprintf( shared->device_name, 255, "/dev/dri/card0" );
          D_INFO( "EGL/System: Using device %s (default)\n", shared->device_name );
     }

     ret = local_init( shared->device_name, egl );
     if (ret)
          goto error;

     *ret_data = egl;

     ret = dfb_surface_pool_initialize( core, &eglSurfacePoolFuncs, &shared->pool );
     if (ret)
          goto error;

     ret = core_arena_add_shared_field( core, "egl", shared );
     if (ret)
          goto error;

     return DFB_OK;

error:
     local_deinit( egl );

     SHFREE( pool, shared );

     D_FREE( egl );

     return ret;
}

static DFBResult
system_join( CoreDFB  *core,
             void    **ret_data )
{
     DFBResult      ret;
     EGLData       *egl;
     EGLDataShared *shared;

     D_DEBUG_AT( EGL_System, "%s()\n", __FUNCTION__ );

     egl = D_CALLOC( 1, sizeof(EGLData) );
     if (!egl)
          return D_OOM();

     egl->core = core;

     ret = core_arena_get_shared_field( core, "egl", (void**) &shared );
     if (ret) {
          D_FREE( egl );
          return ret;
     }

     egl->shared = shared;

     ret = local_init( shared->device_name, egl );
     if (ret)
          goto error;

     *ret_data = egl;

     ret = dfb_surface_pool_join( core, shared->pool, &eglSurfacePoolFuncs );
     if (ret)
          goto error;

     return DFB_OK;

error:
     local_deinit( egl );

     D_FREE( egl );

     return ret;
}

static DFBResult
system_shutdown( bool emergency )
{
     EGLData       *egl = dfb_system_data();
     EGLDataShared *shared;

     D_DEBUG_AT( EGL_System, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );

     shared = egl->shared;

     dfb_surface_pool_destroy( shared->pool );

     local_deinit( egl );

     SHFREE( shared->shmpool, shared );

     D_FREE( egl );

     return DFB_OK;
}

static DFBResult
system_leave( bool emergency )
{
     EGLData       *egl = dfb_system_data();
     EGLDataShared *shared;

     D_DEBUG_AT( EGL_System, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );

     shared = egl->shared;

     dfb_surface_pool_leave( shared->pool );

     local_deinit( egl );

     D_FREE( egl );

     return DFB_OK;
}

static DFBResult
system_suspend()
{
     return DFB_OK;
}

static DFBResult
system_resume()
{
     return DFB_OK;
}

static VideoMode *
system_get_modes()
{
     return NULL;
}

static VideoMode *
system_get_current_mode()
{
     return NULL;
}

static DFBResult
system_thread_init()
{
     return DFB_OK;
}

static bool
system_input_filter( CoreInputDevice *device,
                     DFBInputEvent   *event )
{
     return false;
}

static volatile void *
system_map_mmio( unsigned int offset,
                 int          length )
{
     return NULL;
}

static void
system_unmap_mmio( volatile void *addr,
                   int            length )
{
}

static int
system_get_accelerator()
{
     return direct_config_get_int_value( "accelerator" );
}

static unsigned long
system_video_memory_physical( unsigned int offset )
{
     return 0;
}

static void *
system_video_memory_virtual( unsigned int offset )
{
     return NULL;
}

static unsigned int
system_videoram_length()
{
     return 0;
}

static void
system_get_busid( int *ret_bus,
                  int *ret_dev,
                  int *ret_func )
{
     return;
}

static void
system_get_deviceid( unsigned int *ret_vendor_id,
                     unsigned int *ret_device_id )
{
     return;
}
