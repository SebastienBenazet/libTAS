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

#ifndef LIBTAS_INPUTS_H_INCL
#define LIBTAS_INPUTS_H_INCL

#include "../../external/SDL.h"
#include "../global.h"

/* Inputs that are sent from linTAS */
extern struct AllInputs ai;

/* Last state of the inputs, used to generate events */
extern struct AllInputs old_ai;

/* Fake state of the inputs that is seen by the game.
 * This struct is used when the game want to set inputs, such as
 * Warping the cursor position.
 */
extern struct AllInputs game_ai;

/* Generate events of type SDL_KEYUP, store them in our emulated event queue */
void generateKeyUpEvents(void);

/* Same as above but with events SDL_KEYDOWN */
void generateKeyDownEvents(void);

/* Generate events indicating that a controller was plugged in */
void generateControllerAdded(void);

/* Same as KeyUp/KeyDown functions but with controller events */
void generateControllerEvents(void);

/* Same as above with MouseMotion event */
void generateMouseMotionEvents(void);

/* Same as above with the MouseButton event */
void generateMouseButtonEvents(void);

#endif

