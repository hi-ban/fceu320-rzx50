/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/// \file
/// \brief Handles sound emulation using the SDL.

#include <stdio.h>
#include <string.h> 
#include <stdlib.h>

#include "dingoo.h"

#include "../common/configSys.h"

extern Config *g_config;

static int16 *s_Buffer = 0;
static unsigned int s_BufferSize;
static unsigned int s_BufferRead;
static unsigned int s_BufferWrite;
static volatile unsigned int s_BufferIn;

static int s_mute = 0;

SDL_AudioSpec spec;
SDL_mutex *sound_mutex;
SDL_cond *sound_cv;

/**
 * Callback from Slaanesh's minimal library to get and play audio data.
 */
static void fillaudio(void *udata, uint8 *stream, int len) // len == spec.samples * 4
{
    int32 *tmps = (int32 *)stream;
    len >>= 2;

    // debug code
    //printf("s_BufferIn: %i s_BufferWrite = %i s_BufferRead = %i s_BufferSize = %i\n",
    //    s_BufferIn, s_BufferWrite, s_BufferRead, s_BufferSize);

    SDL_LockMutex(sound_mutex);

    while (len) {
        int32 sample = 0;
        if (s_BufferIn) {
            sample = s_Buffer[s_BufferRead];
            s_BufferRead = (s_BufferRead + 1) % s_BufferSize;
            s_BufferIn--;
            sample |= (sample << 16);
        } else {
            sample = 0;
        }

        *tmps = sample;
        tmps++;
        len--; 
    }

    SDL_CondSignal(sound_cv);
    SDL_UnlockMutex(sound_mutex);
}

/**
 * Initialize the audio subsystem.
 */
int InitSound() 
{
    int sound, soundrate, soundbufsize, soundvolume, soundtrianglevolume,
            soundsquare1volume, soundsquare2volume, soundnoisevolume,
            soundpcmvolume, soundq, soundp, lowpass, samples;


    FCEUI_printf("Initializing audio...\n");

    g_config->getOption("SDL.Sound", &sound);
    if (!sound) return 0;

    memset(&spec, 0, sizeof(spec));
    if(SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        puts(SDL_GetError());
        KillSound();
        return(0);
    }
    char driverName[8];
    SDL_AudioDriverName(driverName, 8);
    
    fprintf(stderr, "Loading SDL sound with %s driver...\n", driverName);

    // load configuration variables
    g_config->getOption("SDL.Sound.Rate", &soundrate);
    g_config->getOption("SDL.Sound.BufSize", &soundbufsize);
    g_config->getOption("SDL.Sound.Volume", &soundvolume);
    g_config->getOption("SDL.Sound.Quality", &soundq);
    g_config->getOption("SDL.Sound.TriangleVolume", &soundtrianglevolume);
    g_config->getOption("SDL.Sound.Square1Volume", &soundsquare1volume);
    g_config->getOption("SDL.Sound.Square2Volume", &soundsquare2volume);
    g_config->getOption("SDL.Sound.NoiseVolume", &soundnoisevolume);
    g_config->getOption("SDL.Sound.PCMVolume", &soundpcmvolume);
    g_config->getOption("SDL.Sound.Priority", &soundp);
    g_config->getOption("SDL.Sound.LowPass", &lowpass);

    spec.freq = soundrate;
    spec.format = AUDIO_S16;
    spec.channels = 2;
    spec.samples = 256;
    spec.callback = fillaudio;
    spec.userdata = 0;

    s_BufferSize = 4 * soundrate / 60; // 2 times bigger than sdl buffer //soundbufsize * soundrate / 1000;
    s_BufferSize -= s_BufferSize % spec.samples;

    // For safety, set a bare minimum:
    if (s_BufferSize < spec.samples * 2) s_BufferSize = spec.samples * 2;

    s_Buffer = (int16 *) malloc(sizeof(int16) * s_BufferSize);
    if (!s_Buffer) return 0;

    s_BufferRead = s_BufferWrite = s_BufferIn = 0;

    printf("SDL Size: %d, Internal size: %d\n", spec.samples, s_BufferSize);

    if(SDL_OpenAudio(&spec, 0) < 0) {
        puts(SDL_GetError());
        KillSound();
        return(0);
    }

    sound_mutex = SDL_CreateMutex();
    sound_cv = SDL_CreateCond();
    SDL_PauseAudio(0);

    FCEUI_SetSoundVolume(soundvolume);
    FCEUI_SetSoundQuality(soundq);
    FCEUI_Sound(soundrate);
    FCEUI_SetTriangleVolume(soundtrianglevolume);
    FCEUI_SetSquare1Volume(soundsquare1volume);
    FCEUI_SetSquare2Volume(soundsquare2volume);
    FCEUI_SetNoiseVolume(soundnoisevolume);
    FCEUI_SetPCMVolume(soundpcmvolume);
    FCEUI_SetLowPass(lowpass);

    return (1);
}

/**
 * Returns the size of the audio buffer.
 */
uint32 GetMaxSound(void) {
    return (s_BufferSize);
}

/**
 * Returns the amount of free space in the audio buffer.
 */
uint32 GetWriteSound(void) {
    return (s_BufferSize - s_BufferIn);
}

/**
 * Send a sound clip to the audio subsystem.
 */
void WriteSound(int32 *buf, int Count) 
{
    //extern int EmulationPaused;

    SDL_LockMutex(sound_mutex);

    /*if (EmulationPaused == 0)*/ { // for some reason EmulationPaused is always 1, ignore it
        while(Count) {
            while(s_BufferIn == s_BufferSize)
                SDL_CondWait(sound_cv, sound_mutex); //SDL_Delay(1);  // not done playing

            s_Buffer[s_BufferWrite] = *buf;
            Count--;
            s_BufferWrite = (s_BufferWrite + 1) % s_BufferSize;
            
            s_BufferIn++;
            
            buf++;
        }
    }

    SDL_CondSignal(sound_cv);
    SDL_UnlockMutex(sound_mutex);
}

/**
 * Pause (1) or unpause (0) the audio output.
 */
void SilenceSound(int n)
{
    SDL_PauseAudio(n);  
}

/**
 * Shut down the audio subsystem.
 */
int KillSound(void) {
    SDL_CondSignal(sound_cv);
    FCEUI_Sound(0);
    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if (s_Buffer) {
        free((void *) s_Buffer);
        s_Buffer = 0;
    }
    return (0);
}

/**
 * Adjust the volume either down (-1), up (1), or to the default (0).
 * Unmutes if mute was active before.
 */
void FCEUD_SoundVolumeAdjust(int n) {
    int soundvolume;
    g_config->getOption("SDL.SoundVolume", &soundvolume);

    switch (n) {
    case -1:
        soundvolume -= 10;
        if (soundvolume < 0) {
            soundvolume = 0;
        }
        break;
    case 0:
        soundvolume = 100;
        break;
    case 1:
        soundvolume += 10;
        if (soundvolume > 150) {
            soundvolume = 150;
        }
        break;
    }

    s_mute = 0;
    FCEUI_SetSoundVolume(soundvolume);
    g_config->setOption("SDL.SoundVolume", soundvolume);

    FCEU_DispMessage("Sound volume %d.", soundvolume);
}

/**
 * Toggles the sound on or off.
 */
void FCEUD_SoundToggle(void) {
    if (s_mute) {
        int soundvolume;
        g_config->getOption("SDL.SoundVolume", &soundvolume);

        s_mute = 0;
        FCEUI_SetSoundVolume(soundvolume);
        FCEU_DispMessage("Sound mute off.");
    } else {
        s_mute = 1;
        FCEUI_SetSoundVolume(0);
        FCEU_DispMessage("Sound mute on.");
    }
}