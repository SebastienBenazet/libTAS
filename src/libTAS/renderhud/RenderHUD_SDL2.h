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

#ifdef LIBTAS_ENABLE_HUD

#ifndef LIBTAS_RENDERHUD_SDL2_H_INCL
#define LIBTAS_RENDERHUD_SDL2_H_INCL

#include "RenderHUD.h"
#include "../../external/SDL.h"
#include "../windows.h"

class RenderHUD_SDL2 : public RenderHUD
{
    public:
        ~RenderHUD_SDL2();
        void init(void);
        void setRenderer(SDL_Renderer* r);
        void renderText(const char* text, SDL_Color fg_color, SDL_Color bg_color, int x, int y);

    private:
        SDL_Renderer* renderer;
};

#endif
#endif

