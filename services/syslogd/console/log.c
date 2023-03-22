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

#include "screen.h"

static struct {
    OutputCreateFn make;
    struct Output* output;
} g_outputs[] = {
        { ScreenCreateFB, NULL },
        { NULL, NULL }
};
static bool g_splashMode = false;

void LogInitialize(void)
{
    OSBootVideoDescriptor_t bootVideo;
    int                     i = 0;

    // Assume this call always succeed
    QueryBootVideoInformation(&bootVideo);

    // Create all system outputs
    while (g_outputs[i].make) {
        g_outputs[i].output = g_outputs[i].make(&bootVideo);
        i++;
    }

    // TODO: Read all kernel logs and print them
    // TODO: Detect from boot environment whether to do splash mode or not
}

void LogReportStage(const char* string, int steps)
{

}

void LogReportProgress(const char* string)
{

}

void LogPrint(enum OSSysLogLevel level, const char* string)
{
    int i = 0;
    while (g_outputs[i].output) {
        g_outputs[i].output->Ops->Print(g_outputs[i].output, string);
        i++;
    }
}

void LogInhibite(void)
{
    int i = 0;
    while (g_outputs[i].output) {
        g_outputs[i].output->Ops->Inhibit(g_outputs[i].output);
        i++;
    }
}

void LogUninhibite(void)
{
    int i = 0;
    while (g_outputs[i].output) {
        g_outputs[i].output->Ops->Inhibit(g_outputs[i].output);
        i++;
    }
}
