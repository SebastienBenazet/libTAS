/*
    Copyright 2015-2016 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "videocapture.h"

#ifdef LIBTAS_ENABLE_AVDUMPING

#include "hook.h"
#include "logging.h"
#include "../external/gl.h" // glReadPixels enum arguments
#include "../external/SDL.h" // SDL_Surface
#include <vector>
#include <string.h> // memcpy

bool useGL;

/* Temporary pixel arrays */
std::vector<uint8_t> glpixels;
std::vector<uint8_t> winpixels;

/* Original function pointers */
void (*SDL_GL_GetDrawableSize_real)(void* window, int* w, int* h);
SDL_Surface* (*SDL_GetWindowSurface_real)(void* window);
SDL1::SDL_Surface* (*SDL_GetVideoSurface_real)(void);
int (*SDL_LockSurface_real)(SDL1::SDL_Surface* surface);
void (*SDL_UnlockSurface_real)(SDL1::SDL_Surface* surface);
int (*SDL_RenderReadPixels_real)(void*, const SDL_Rect*, Uint32, void*, int);
Uint32 (*SDL_GetWindowPixelFormat_real)(void* window);
void* (*SDL_GetRenderer_real)(void* window);
int (*SDL_GetRendererOutputSize_real)(void* renderer, int* w, int* h);

void (*glReadPixels_real)(int x, int y, int width, int height, unsigned int format, unsigned int type, void* data);

/* Video dimensions */
int width, height;
int size;

void* renderer;
int pixelSize = 0;
int glPixFmt;

AVPixelFormat initVideoCapture(void* window, bool video_opengl, int *pwidth, int *pheight)
{
    /* If the game uses openGL, the method to capture the screen will be different */
    useGL = video_opengl;
    AVPixelFormat pixfmt = AV_PIX_FMT_NONE;

    /* Link the required functions, and get the window dimensions */
    if (SDLver == 1) {
        LINK_SUFFIX_SDL1(SDL_GetVideoSurface);
        if (useGL)
            LINK_SUFFIX(glReadPixels, "libGL");
        else {
            LINK_SUFFIX_SDL1(SDL_LockSurface);
            LINK_SUFFIX_SDL1(SDL_UnlockSurface);
        }

        /* Get dimensions from the window surface */
        if (!SDL_GetVideoSurface_real) {
            debuglog(LCF_DUMP | LCF_SDL | LCF_ERROR, "Need function SDL_GetVideoSurface.");
            return AV_PIX_FMT_NONE;
        }

        SDL1::SDL_Surface *surf = SDL_GetVideoSurface_real();
        
        pixelSize = surf->format->BytesPerPixel;
        pixfmt = AV_PIX_FMT_RGBA; // TODO
        
        *pwidth = surf->w;
        *pheight = surf->h;
    }
    else if (SDLver == 2) {

        if (useGL) {
            LINK_SUFFIX_SDL2(SDL_GL_GetDrawableSize);
            LINK_SUFFIX(glReadPixels, "libGL");

            /* Get information about the current screen */
            if (!SDL_GL_GetDrawableSize_real) {
                debuglog(LCF_DUMP | LCF_SDL | LCF_ERROR, "Need function SDL_GL_GetDrawableSize.");
                return AV_PIX_FMT_NONE;
            }

            SDL_GL_GetDrawableSize_real(window, pwidth, pheight);
            pixfmt = AV_PIX_FMT_BGRA;
            pixelSize = 4;
        }
        else {
            LINK_SUFFIX_SDL2(SDL_RenderReadPixels);
            LINK_SUFFIX_SDL2(SDL_GetRenderer);
            LINK_SUFFIX_SDL2(SDL_GetRendererOutputSize);
            LINK_SUFFIX_SDL2(SDL_GetWindowPixelFormat);

            Uint32 sdlpixfmt = SDL_GetWindowPixelFormat_real(window);
            /* TODO: There are probably helper functions to build pixel
             * format constants from channel masks
             */
            switch (sdlpixfmt) {
                case SDL_PIXELFORMAT_RGBA8888:
                    pixfmt = AV_PIX_FMT_BGRA;
                    debuglog(LCF_DUMP | LCF_SDL, "  RGBA");
                    break;
                case SDL_PIXELFORMAT_BGRA8888:
                    pixfmt = AV_PIX_FMT_RGBA;
                    debuglog(LCF_DUMP | LCF_SDL, "  BGRA");
                    break;
                case SDL_PIXELFORMAT_ARGB8888:
                    pixfmt = AV_PIX_FMT_ABGR;
                    debuglog(LCF_DUMP | LCF_SDL, "  ARGB");
                    break;
                case SDL_PIXELFORMAT_ABGR8888:
                    pixfmt = AV_PIX_FMT_ARGB;
                    debuglog(LCF_DUMP | LCF_SDL, "  ABGR");
                    break;
                case SDL_PIXELFORMAT_RGB24:
                    pixfmt = AV_PIX_FMT_BGR24;
                    debuglog(LCF_DUMP | LCF_SDL, "  RGB24");
                    break;
                case SDL_PIXELFORMAT_BGR24:
                    pixfmt = AV_PIX_FMT_RGB24;
                    debuglog(LCF_DUMP | LCF_SDL, "  BGR24");
                    break;
                case SDL_PIXELFORMAT_RGB888:
                    pixfmt = AV_PIX_FMT_BGR0;
                    debuglog(LCF_DUMP | LCF_SDL, "  RGB888");
                    break;
                case SDL_PIXELFORMAT_RGBX8888:
                    pixfmt = AV_PIX_FMT_RGB0;
                    debuglog(LCF_DUMP | LCF_SDL, "  RGBX888");
                    break;
                case SDL_PIXELFORMAT_BGR888:
                    pixfmt = AV_PIX_FMT_0BGR;
                    debuglog(LCF_DUMP | LCF_SDL, "  RGBX888");
                    break;
                case SDL_PIXELFORMAT_BGRX8888:
                    pixfmt = AV_PIX_FMT_BGR0;
                    break;
                default:
                    debuglog(LCF_DUMP | LCF_SDL | LCF_ERROR, "  Unsupported pixel format");
            }
            pixelSize = sdlpixfmt & 0xFF;

            /* Get surface from window */
            if (!SDL_GetRendererOutputSize_real) {
                debuglog(LCF_DUMP | LCF_SDL | LCF_ERROR, "Need function SDL_GetWindowSize.");
                return AV_PIX_FMT_NONE;
            }

            renderer = SDL_GetRenderer_real(window);
            SDL_GetRendererOutputSize_real(renderer, pwidth, pheight);
        }
    }
    else {
        debuglog(LCF_DUMP | LCF_SDL | LCF_ERROR, "Unknown SDL version.");
        return AV_PIX_FMT_NONE;
    }

    /* Save dimensions for later */
    width = *pwidth;
    height = *pheight;

    /* Allocate an array of pixels */
    size = width * height * pixelSize;
    winpixels.resize(size);

    /* Dimensions must be a multiple of 2 */
    if ((width % 1) || (height % 1)) {
        debuglog(LCF_DUMP | LCF_ERROR, "Screen dimensions must be a multiple of 2");
        return AV_PIX_FMT_NONE;
    }

    if (useGL) {
        /* Do we already have access to the glReadPixels function? */
        if (!glReadPixels_real) {
            debuglog(LCF_DUMP | LCF_OGL | LCF_ERROR, "Could not load function glReadPixels.");
            return AV_PIX_FMT_NONE;
        }

        /* Allocate another pixels array,
         * because the image will need to be flipped.
         */
        glpixels.resize(size);
    }

    return pixfmt;
}

int captureVideoFrame(const uint8_t* orig_plane[], int orig_stride[])
{
    int pitch = pixelSize * width;

    if (useGL) {
        /* TODO: Check that the openGL dimensions did not change in between */

        /* We access to the image pixels directly using glReadPixels */
        glReadPixels_real(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &glpixels[0]);
        /* TODO: I saw this in some examples before calling glReadPixels: glPixelStorei(GL_PACK_ALIGNMENT, 1); */

        /*
         * Flip image horizontally
         * This is because OpenGL has a different reference point
         * Code taken from http://stackoverflow.com/questions/5862097/sdl-opengl-screenshot-is-black
         * TODO: Could this be done without allocating another array ?
         */

        for (int line = 0; line < height; line++) {
            int pos = line * pitch;
            for (int p=0; p<pitch; p++) {
                winpixels[pos + p] = glpixels[(size-pos)-pitch + p];
            }
        }

    }

    else {
        /* Not tested !! */
        debuglog(LCF_DUMP | LCF_UNTESTED | LCF_FRAME, "Access SDL_Surface pixels for video dump");

        if (SDLver == 1) {
            /* Get surface from window */
            SDL1::SDL_Surface* surf1 = SDL_GetVideoSurface_real();

            /* Checking for a size modification */
            int cw = surf1->w;
            int ch = surf1->h;
            if ((cw != width) || (ch != height)) {
                debuglog(LCF_DUMP | LCF_ERROR, "Window coords have changed (",width,",",height,") -> (",cw,",",ch,")");
                return 1;
            }

            /* We must lock the surface before accessing the raw pixels */
            int ret = SDL_LockSurface_real(surf1);
            if (ret != 0) {
                debuglog(LCF_DUMP | LCF_ERROR, "Could not lock SDL surface");
                return 1;
            }

            /* I know memcpy is not recommended for vectors... */
            memcpy(&winpixels[0], surf1->pixels, size);

            /* Unlock surface */
            SDL_UnlockSurface_real(surf1);
        }

        if (SDLver == 2) {
            /* Checking for a size modification */
            int cw, ch;
            SDL_GetRendererOutputSize_real(renderer, &cw, &ch);
            if ((cw != width) || (ch != height)) {
                debuglog(LCF_DUMP | LCF_ERROR, "Window coords have changed (",width,",",height,") -> (",cw,",",ch,")");
                return 1;
            }

            SDL_RenderReadPixels_real(renderer, NULL, 0, &winpixels[0], pitch);
        }
    }

    orig_plane[0] = (const uint8_t*)(&winpixels[0]);
    orig_stride[0] = pitch;

    return 0;
}

#endif

