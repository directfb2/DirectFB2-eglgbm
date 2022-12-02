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

#include <core/layers.h>

#include "egl_system.h"

D_DEBUG_DOMAIN( EGL_Layer, "EGL/Layer", "EGL Layer" );

/**********************************************************************************************************************/

static void
egl_destroy_user_data( struct gbm_bo *bo,
                       void          *data )
{
     drmModeRmFB( gbm_device_get_fd( gbm_bo_get_device( bo ) ), (uintptr_t) data );
}

/**********************************************************************************************************************/

static DFBResult
eglPrimaryInitLayer( CoreLayer                  *layer,
                     void                       *driver_data,
                     void                       *layer_data,
                     DFBDisplayLayerDescription *description,
                     DFBDisplayLayerConfig      *config,
                     DFBColorAdjustment         *adjustment )
{
     EGLData       *egl = driver_data;
     EGLDataShared *shared;

     D_DEBUG_AT( EGL_Layer, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );
     D_ASSERT( egl->shared != NULL );

     shared = egl->shared;

     /* Set type and capabilities. */
     description->caps = DLCAPS_SURFACE;
     description->type = DLTF_GRAPHICS;

     /* Set name. */
     snprintf( description->name, DFB_DISPLAY_LAYER_DESC_NAME_LENGTH, "EGL Primary Layer" );

     /* Fill out the default configuration. */
     config->flags       = DLCONF_WIDTH | DLCONF_HEIGHT | DLCONF_PIXELFORMAT | DLCONF_BUFFERMODE;
     config->width       = dfb_config->mode.width  ?: shared->mode.w;
     config->height      = dfb_config->mode.height ?: shared->mode.h;
     config->pixelformat = dfb_config->mode.format ?: DSPF_ARGB;
     config->buffermode  = DLBM_FRONTONLY;

     return DFB_OK;
}

static DFBResult
eglPrimaryTestRegion( CoreLayer                  *layer,
                      void                       *driver_data,
                      void                       *layer_data,
                      CoreLayerRegionConfig      *config,
                      CoreLayerRegionConfigFlags *ret_failed )
{
     CoreLayerRegionConfigFlags failed = CLRCF_NONE;

     D_DEBUG_AT( EGL_Layer, "%s( %dx%d, %s )\n", __FUNCTION__,
                 config->source.w, config->source.h, dfb_pixelformat_name( config->format ) );

     switch (config->buffermode) {
          case DLBM_FRONTONLY:
          case DLBM_BACKVIDEO:
          case DLBM_BACKSYSTEM:
          case DLBM_TRIPLE:
               break;

          default:
               failed |= CLRCF_BUFFERMODE;
               break;
     }

     switch (config->format) {
          case DSPF_ARGB:
          case DSPF_RGB16:
               break;

          default:
               failed |= CLRCF_FORMAT;
               break;
     }

     if (config->options)
          failed |= CLRCF_OPTIONS;

     if (ret_failed)
          *ret_failed = failed;

     if (failed)
          return DFB_UNSUPPORTED;

     return DFB_OK;
}

static DFBResult
eglPrimarySetRegion( CoreLayer                  *layer,
                     void                       *driver_data,
                     void                       *layer_data,
                     void                       *region_data,
                     CoreLayerRegionConfig      *config,
                     CoreLayerRegionConfigFlags  updated,
                     CoreSurface                *surface,
                     CorePalette                *palette,
                     CoreSurfaceBufferLock      *left_lock,
                     CoreSurfaceBufferLock      *right_lock )
{
     D_DEBUG_AT( EGL_Layer, "%s()\n", __FUNCTION__ );

     return DFB_OK;
}

static DFBResult
eglPrimaryUpdateRegion( CoreLayer             *layer,
                        void                  *driver_data,
                        void                  *layer_data,
                        void                  *region_data,
                        CoreSurface           *surface,
                        const DFBRegion       *left_update,
                        CoreSurfaceBufferLock *left_lock,
                        const DFBRegion       *right_update,
                        CoreSurfaceBufferLock *right_lock )
{
     EGLData         *egl           = driver_data;
     DFBRegion        region        = DFB_REGION_INIT_FROM_DIMENSION( &surface->config.size );
     drmEventContext  event_context = { DRM_EVENT_CONTEXT_VERSION, NULL, NULL };
     struct gbm_bo   *bo;
     uint32_t         fb_id;

     D_DEBUG_AT( EGL_Layer, "%s()\n", __FUNCTION__ );

     D_ASSERT( egl != NULL );

     if (left_update && !dfb_region_region_intersect( &region, left_update ))
          return DFB_OK;

     eglSwapBuffers( egl->eglDisplay, egl->eglSurface );

     bo = gbm_surface_lock_front_buffer( egl->gbm_surface );

     if (bo) {
          fb_id = (uintptr_t) gbm_bo_get_user_data( bo );

          if(!fb_id) {
               drmModeAddFB( egl->fd, gbm_bo_get_width( bo ), gbm_bo_get_height( bo ), 24, 32, gbm_bo_get_stride( bo ),
                             gbm_bo_get_handle( bo ).u32, &fb_id );

               drmModeSetCrtc( egl->fd, egl->encoder->crtc_id, fb_id, 0, 0, &egl->connector->connector_id, 1,
                               &egl->connector->modes[0] );

               gbm_bo_set_user_data( bo, (void *)(uintptr_t) fb_id, egl_destroy_user_data );
          }

          drmModePageFlip( egl->fd, egl->encoder->crtc_id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL );

          drmHandleEvent( egl->fd, &event_context );

          gbm_surface_release_buffer( egl->gbm_surface, bo );
     }

     return DFB_OK;
}

const DisplayLayerFuncs eglPrimaryLayerFuncs = {
     .InitLayer    = eglPrimaryInitLayer,
     .TestRegion   = eglPrimaryTestRegion,
     .SetRegion    = eglPrimarySetRegion,
     .UpdateRegion = eglPrimaryUpdateRegion
};
