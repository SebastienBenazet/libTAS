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

#include "inputs.h"
#include "keyboard_helper.h"
#include "../logging.h"
#include "../../shared/AllInputs.h"
#include "../../shared/tasflags.h"
#include <X11/keysym.h>
#include <stdlib.h>
#include "../DeterministicTimer.h"
#include "../windows.h" // for SDL_GetWindowId_real
#include "sdlgamecontroller.h" // sdl_controller_event

struct AllInputs ai;
struct AllInputs old_ai;

int generateKeyUpEvent(void *events, void* gw, int num, int update)
{
    int evi = 0;
    int i, j;
	struct timespec time;
    for (i=0; i<16; i++) { // TODO: Another magic number
        if (old_ai.keyboard[i] == XK_VoidSymbol) {
            continue;
        }
        for (j=0; j<16; j++) {
            if (old_ai.keyboard[i] == ai.keyboard[j]) {
                /* Key was not released */
                break;
            }
        }
        if (j == 16) {
            /* Key was released. Generate event */
            if (SDLver == 2) {
                SDL_Event* events2 = (SDL_Event*)events;
                events2[evi].type = SDL_KEYUP;
                events2[evi].key.state = SDL_RELEASED;
                events2[evi].key.windowID = SDL_GetWindowID_real(gw);
				time = detTimer.getTicks(TIMETYPE_UNTRACKED);
                events2[evi].key.timestamp = time.tv_sec * 1000 + time.tv_nsec / 1000000;

                SDL_Keysym keysym;
                xkeysymToSDL(&keysym, old_ai.keyboard[i]);
                events2[evi].key.keysym = keysym;

                debuglog(LCF_SDL | LCF_EVENTS | LCF_KEYBOARD, "Generate SDL event KEYUP with key ", events2[evi].key.keysym.sym);
            }

            if (SDLver == 1) {
                SDL1::SDL_Event* events1 = (SDL1::SDL_Event*)events;
                events1[evi].type = SDL1::SDL_KEYUP;
                events1[evi].key.which = 0; // FIXME: I don't know what is going here
                events1[evi].key.state = SDL_RELEASED;

                SDL1::SDL_keysym keysym;
                xkeysymToSDL1(&keysym, old_ai.keyboard[i]);
                events1[evi].key.keysym = keysym;

                debuglog(LCF_SDL | LCF_EVENTS | LCF_KEYBOARD, "Generate SDL1 event KEYUP with key ", events1[evi].key.keysym.sym);
            }

            if (update) {
                /* Update old keyboard state so that this event won't trigger inifinitely */
                old_ai.keyboard[i] = XK_VoidSymbol;
            }
            evi++;

            /* If we reached the asked number of events, returning */
            if (evi == num)
                return evi;

        }
    }
    /* We did not reached the asked number of events, returning the number */
    return evi;
}


/* Generate pressed keyboard input events */
int generateKeyDownEvent(void *events, void* gw, int num, int update)
{
    int evi = 0;
    int i,j,k;
	struct timespec time;
    for (i=0; i<16; i++) { // TODO: Another magic number
        if (ai.keyboard[i] == XK_VoidSymbol) {
            continue;
        }
        for (j=0; j<16; j++) {
            if (ai.keyboard[i] == old_ai.keyboard[j]) {
                /* Key was not pressed */
                break;
            }
        }
        if (j == 16) {
            /* Key was pressed. Generate event */
            if (SDLver == 2) {
                SDL_Event* events2 = (SDL_Event*)events;
                events2[evi].type = SDL_KEYDOWN;
                events2[evi].key.state = SDL_PRESSED;
                events2[evi].key.windowID = SDL_GetWindowID_real(gw);
				time = detTimer.getTicks(TIMETYPE_UNTRACKED);
                events2[evi].key.timestamp = time.tv_sec * 1000 + time.tv_nsec / 1000000;

                SDL_Keysym keysym;
                xkeysymToSDL(&keysym, ai.keyboard[i]);
                events2[evi].key.keysym = keysym;

                debuglog(LCF_SDL | LCF_EVENTS | LCF_KEYBOARD, "Generate SDL event KEYDOWN with key ", events2[evi].key.keysym.sym);
            }

            if (SDLver == 1) {
                SDL1::SDL_Event* events1 = (SDL1::SDL_Event*)events;
                events1[evi].type = SDL1::SDL_KEYDOWN;
                events1[evi].key.which = 0; // FIXME: I don't know what is going here
                events1[evi].key.state = SDL_PRESSED;

                SDL1::SDL_keysym keysym;
                xkeysymToSDL1(&keysym, ai.keyboard[i]);
                events1[evi].key.keysym = keysym;

                debuglog(LCF_SDL | LCF_EVENTS | LCF_KEYBOARD, "Generate SDL event KEYDOWN with key ", events1[evi].key.keysym.sym);
            }

            if (update) {
                /* Update old keyboard state so that this event won't trigger inifinitely */
                for (k=0; k<16; k++)
                    if (old_ai.keyboard[k] == XK_VoidSymbol) {
                        /* We found an empty space to put our key*/
                        old_ai.keyboard[k] = ai.keyboard[i];
                        break;
                    }
            }

            evi++;

            /* Returning if we reached the asked number of events */
            if (evi == num)
                return evi;

        }
    }

    /* Returning the number of generated events */
    return evi;
}


/* Generate SDL2 GameController events */

int generateControllerAdded(SDL_Event* events, int num, int update)
{
    struct timespec time;

    /* Number of controllers added in total */
    static int controllersAdded = 0;

    /* Number of controllers added during this function call */
    int curAdded = 0;

    while ((curAdded < num) && ((curAdded + controllersAdded) < tasflags.numControllers)) {
        events[curAdded].type = SDL_CONTROLLERDEVICEADDED;
        time = detTimer.getTicks(TIMETYPE_UNTRACKED);
        events[curAdded].cdevice.timestamp = time.tv_sec * 1000 + time.tv_nsec / 1000000;
        events[curAdded].cdevice.which = curAdded + controllersAdded;
        curAdded++;
    }
    if (update)
        controllersAdded += curAdded;
    return curAdded;
}

int generateControllerEvent(SDL_Event* events, int num, int update)
{
    int evi = 0;
	struct timespec time;
    if (!sdl_controller_events)
        return 0;
    int ji = 0;
    for (ji=0; ji<tasflags.numControllers; ji++) {
        /* Check for axes change */
        int axis;
        for (axis=0; axis<6; axis++) {
            if (ai.controller_axes[ji][axis] != old_ai.controller_axes[ji][axis]) {
                /* We got a change in a controller axis value */

                /* Fill the event structure */
                events[evi].type = SDL_CONTROLLERAXISMOTION;
				time = detTimer.getTicks(TIMETYPE_UNTRACKED);
                events[evi].caxis.timestamp = time.tv_sec * 1000 + time.tv_nsec / 1000000;
                events[evi].caxis.which = ji;
                events[evi].caxis.axis = axis;
                events[evi].caxis.value = ai.controller_axes[ji][axis];

                if (update) {
                    /* Upload the old AllInput struct */
                    old_ai.controller_axes[ji][axis] = ai.controller_axes[ji][axis];
                }

                evi++;

                if (evi == num) {
                    /* Return the number of events */
                    return evi;
                }
            }
        }

        /* Check for button change */
        int bi;
        unsigned short buttons = ai.controller_buttons[ji];
        unsigned short old_buttons = old_ai.controller_buttons[ji];

        for (bi=0; bi<16; bi++) {
            if (((buttons >> bi) & 0x1) != ((old_buttons >> bi) & 0x1)) {
                /* We got a change in a button state */

                /* Fill the event structure */
                if ((buttons >> bi) & 0x1) {
                    events[evi].type = SDL_CONTROLLERBUTTONDOWN;
                    events[evi].cbutton.state = SDL_PRESSED;
                }
                else {
                    events[evi].type = SDL_CONTROLLERBUTTONUP;
                    events[evi].cbutton.state = SDL_RELEASED;
                }
				time = detTimer.getTicks(TIMETYPE_UNTRACKED);
                events[evi].cbutton.timestamp = time.tv_sec * 1000 + time.tv_nsec / 1000000;
                events[evi].cbutton.which = ji;
                events[evi].cbutton.button = bi;

                if (update) {
                    /* Upload the old AllInput struct */
                    old_ai.controller_buttons[ji] ^= (1 << bi);
                }

                if (evi == num) {
                    /* Return the number of events */
                    return evi;
                }
            }
        }
    }

    /* We did not reached the asked number of events, returning the number */
    return evi;
}
