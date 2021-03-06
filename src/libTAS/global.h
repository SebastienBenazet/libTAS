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

#ifndef LIBTAS_GLOBAL_H_INCL
#define LIBTAS_GLOBAL_H_INCL

/* Include this file in every source code that override functions of the game */

#if __GNUC__ >= 4
    #define OVERRIDE extern "C" __attribute__ ((visibility ("default")))
#else
    #define OVERRIDE extern "C"
#endif

/* Some code may be executed before we ever have time to call our constructor,
 * so we keep track of this
 * Definition is in libTAS.cpp
 */
extern bool libTAS_init;

#endif

