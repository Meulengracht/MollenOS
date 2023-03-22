/**
 * Copyright 2023, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CONSOLE_SCREEN_H__
#define __CONSOLE_SCREEN_H__

#include <ddk/video.h>
#include <os/types/syslog.h>

struct Output;

struct SplashOptions {
    const void* Bitmap;
    int         X;
    int         Y;
    int         Width;
    int         Height;
};

struct OutputOperations {
    void (*Print)(struct Output*, const char*);
    void (*Clear)(struct Output*, uint8_t, uint8_t, uint8_t);
    void (*Flush)(struct Output*);
    void (*SetSplashMode)(struct Output*, bool, struct SplashOptions*);
    void (*Inhibit)(struct Output*);
    void (*Uninhibit)(struct Output*);
};

struct Output {
    struct OutputOperations* Ops;
};

typedef struct Output*(*OutputCreateFn)(OSBootVideoDescriptor_t*);

/**
 * Available outputs
 */
extern struct Output* ScreenCreateFB(OSBootVideoDescriptor_t* video);

/**
 * @brief Initializes the output system.
 */
extern void LogInitialize(void);

extern void LogReportStage(const char* string, int steps);
extern void LogReportProgress(const char* string);
extern void LogPrint(enum OSSysLogLevel level, const char* string);
extern void LogInhibite(void);
extern void LogUninhibite(void);

#endif //!__CONSOLE_SCREEN_H__
