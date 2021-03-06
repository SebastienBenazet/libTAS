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

#include "DeterministicTimer.h"
#include "NonDeterministicTimer.h"
#include "logging.h"
#include "../shared/tasflags.h"
#include "threads.h"
#include "frame.h"
#include "time.h" // clock_gettime_real
#include "audio/AudioContext.h"
#include "ThreadState.h"

#define MAX_NONFRAME_GETTIMES 4000

struct timespec DeterministicTimer::getTicks(TimeCallType type=TIMETYPE_UNTRACKED)
{
    //std::lock_guard<std::mutex> lock(mutex);
    DEBUGLOGCALL(LCF_TIMEGET | LCF_FREQUENT);

    /* If we are in the native thread state, just return the real time */
    if (threadState.isNative()) {
        struct timespec realtime;
        clock_gettime_real(CLOCK_REALTIME, &realtime);
        return realtime;
    }

    if(tasflags.framerate == 0) {
        return nonDetTimer.getTicks(); // 0 framerate means disable deterministic timer
    }

    bool isFrameThread = isMainThread();

    /* If it is our own code calling this, we don't need to track the call */
    if (threadState.isOwnCode())
        type = TIMETYPE_UNTRACKED;

    /* Only the main thread can modify the timer */
    if(!isFrameThread) {
        if(type != TIMETYPE_UNTRACKED) {

            /* Well, actually, if another thread get the time too many times,
             * we temporarily consider it as the main thread.
             * This can lead to desyncs, but it avoids freeze in games that
             * expect the time to advance in other threads.
             */
            if(getTimes >= MAX_NONFRAME_GETTIMES)
            {
                if(getTimes == MAX_NONFRAME_GETTIMES)
                    debuglog(LCF_TIMEGET | LCF_DESYNC, "Temporarily assuming main thread");
                isFrameThread = true;
            }
            getTimes++;
        }
    }

    if (isFrameThread)
    {
        /* Only do this in the main thread so as to not dirty the timer with nondeterministic values
         * (not to mention it would be extremely multithreading-unsafe without using sync primitives otherwise)
         */

        int ticksExtra = 0;

        if (type != TIMETYPE_UNTRACKED)
        {
            debuglog(LCF_TIMESET | LCF_FREQUENT, "subticks ", type, " increased");
            altGetTimes[type]++;

            if(altGetTimes[type] > altGetTimeLimits[type]) {
                /* 
                 * We reached the limit of the number of calls.
                 * We advance the deterministic timer by some value
                 */
                int tickDelta = 1;

                debuglog(LCF_TIMESET | LCF_FREQUENT, "WARNING! force-advancing time of type ", type);

                ticksExtra += tickDelta;

                /* Reseting the number of calls from all functions */
                for (int i = 0; i < TIMETYPE_NUMTRACKEDTYPES; i++)
                    altGetTimes[i] = 0;

            }
        }

        if(ticksExtra) {
            /* Delay by ticksExtra ms. Arbitrary */
            struct timespec delay = {0, ticksExtra * 1000000};
            addDelay(delay);
        }
    }

    TimeHolder fakeTicks = ticks + fakeExtraTicks;
    return *(struct timespec*)&fakeTicks;
}


void DeterministicTimer::addDelay(struct timespec delayTicks)
{
    //std::lock_guard<std::mutex> lock(mutex);
    debuglog(LCF_TIMESET | LCF_SLEEP, __func__, " call with delay ", delayTicks.tv_sec * 1000000000 + delayTicks.tv_nsec, " nsec");

    if(tasflags.framerate == 0) // 0 framerate means disable deterministic timer
        return nonDetTimer.addDelay(delayTicks);

    /* We don't handle wait if it is our own code calling this. */
    if (threadState.isOwnCode())
        return;

    /* Deferring as much of the delay as possible
     * until the place where the regular per-frame delay is applied
     * gives the smoothest results.
     * However, there must be a limit,
     * otherwise it could easily build up and make us freeze (in some games)
     */

    TimeHolder maxDeferredDelay = timeIncrement * 6;

    addedDelay += *(TimeHolder*)&delayTicks;
    ticks += *(TimeHolder*)&delayTicks;
    forceAdvancedTicks += *(TimeHolder*)&delayTicks;

    if(!tasflags.fastforward)
    {
        /* Sleep, because the caller would have yielded at least a little */
        struct timespec nosleep = {0, 0};
        nanosleep_real(&nosleep, NULL);
    }

    while(addedDelay > maxDeferredDelay)
    {
        /* Indicating that the following frame boundary is not
         * a normal (draw) frame boundary.
         */
        drawFB = false;

        /* We have built up too much delay. We must enter a frame boundary,
         * to advance the time.
         * This decrements addedDelay by (basically) how much it advances ticks
         */
        frameBoundary(false);

    }
}

void DeterministicTimer::exitFrameBoundary()
{
    //std::lock_guard<std::mutex> lock(mutex);
    DEBUGLOGCALL(LCF_TIMEGET | LCF_FRAME);

    /* Reset the counts of each time get function */
    for(int i = 0; i < TIMETYPE_NUMTRACKEDTYPES; i++)
        altGetTimes[i] = 0;

    if(tasflags.framerate == 0)
        return nonDetTimer.exitFrameBoundary(); // 0 framerate means disable deterministic timer

    getTimes = 0;

    if(addedDelay > timeIncrement)
        addedDelay -= timeIncrement;
    else {
        addedDelay.tv_sec = 0;
        addedDelay.tv_nsec = 0;
    }
}


void DeterministicTimer::enterFrameBoundary()
{
    //std::lock_guard<std::mutex> lock(mutex);
    DEBUGLOGCALL(LCF_TIMEGET | LCF_FRAME);

    if(tasflags.framerate == 0)
        return nonDetTimer.enterFrameBoundary(); // 0 framerate means disable deterministic timer

    /*** First we update the state of the internal timer ***/

    /* We compute by how much we should advance the timer
     * to run exactly as the indicated framerate
     */
    unsigned int integer_increment = 1000000000 / tasflags.framerate;
    unsigned int fractional_increment = 1000000000 % tasflags.framerate;

    timeIncrement.tv_sec = 0;
    timeIncrement.tv_nsec = integer_increment;

    fractional_part += fractional_increment;
    if (fractional_part >= tasflags.framerate)
    {
        timeIncrement.tv_nsec++;
        fractional_part -= tasflags.framerate;
    }

    /* Subtract out ticks that were made when calling GetTicks() */
    /* Warning: adding forceAdvanceTicks or not can yield different results! */
    TimeHolder takenTicks = (ticks - lastEnterTicks) /*+ forceAdvancedTicks*/;

    /* If we enter the frame normally (with a screen draw),
     * then reset the forceAdvancedTicks
     */
    if (drawFB) {
        forceAdvancedTicks.tv_sec = 0;
        forceAdvancedTicks.tv_nsec = 0;
    }

    /* 
     * Did we already have advanced more ticks that the length of a frame?
     * If not, we add the remaining ticks
     */
    if (timeIncrement > takenTicks) {
        TimeHolder deltaTicks = timeIncrement - takenTicks;
        ticks += deltaTicks;
        debuglog(LCF_TIMESET | LCF_FRAME, __func__, " added ", deltaTicks.tv_sec * 1000000000 + deltaTicks.tv_nsec, " nsec");
    }

    /* Doing the audio mixing here */
    audiocontext.mixAllSources(*(struct timespec*)&timeIncrement);

    /*** Then, we sleep the right amount of time so that the game runs at normal speed ***/

    /* Get the current actual time */
    TimeHolder currentTime;
    clock_gettime_real(CLOCK_MONOTONIC, (struct timespec*)&currentTime);

    /* calculate the target time we wanted to be at now */
    /* TODO: This is where we would implement slowdown */
    TimeHolder desiredTime = lastEnterTime + timeIncrement;

    TimeHolder deltaTime = desiredTime - currentTime;

    /* If we are not fast forwarding, and not the first frame,
     * then we wait the delta amount of time.
     */
    if (!tasflags.fastforward && lastEnterValid) {

        /* Check that we wait for a positive time */
        if ((deltaTime.tv_sec > 0) || ((deltaTime.tv_sec == 0) && (deltaTime.tv_nsec >= 0))) {
            nanosleep_real((struct timespec*)&deltaTime, NULL);
        }
    }

    /* 
     * WARNING: This time update is not done in Hourglass,
     * maybe intentionally (the author does not remember).
     */
    clock_gettime_real(CLOCK_MONOTONIC, (struct timespec*)&currentTime);

    lastEnterTime = currentTime;

    lastEnterTicks = ticks;
    lastEnterValid = true;
}

void DeterministicTimer::fakeAdvanceTimer(struct timespec extraTicks) {
    fakeExtraTicks = *(TimeHolder*) &extraTicks;
}

void DeterministicTimer::initialize(void)
{
    getTimes = 0;
    ticks = {0, 0};
    fractional_part = 0;
    clock_gettime_real(CLOCK_MONOTONIC, (struct timespec*)&lastEnterTime);
    lastEnterTicks = ticks;

    for(int i = 0; i < TIMETYPE_NUMTRACKEDTYPES; i++)
        altGetTimes[i] = 0;
    for(int i = 0; i < TIMETYPE_NUMTRACKEDTYPES; i++)
        altGetTimeLimits[i] = 100;

    forceAdvancedTicks = {0, 0};
    addedDelay = {0, 0};
    fakeExtraTicks = {0, 0};
    lastEnterValid = false;

    drawFB = true;
}

DeterministicTimer detTimer;

