/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Pipe Interface
 *  - Builds on principle of a ringbuffer with added features
 *    as queues and locks
 */

/* Includes 
 * - System */
#include <scheduler.h>
#include <pipe.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stddef.h>

/* PipeIncreaseWrite
 * Helper for properly doing a wrap-around
 * when buffer boundary is reached */
void
PipeIncreaseWrite(
    _In_ MCorePipe_t *Pipe)
{
    // Increase with wrap-around
    Pipe->IndexWrite++;
    if (Pipe->IndexWrite == Pipe->Length) {
        Pipe->IndexWrite = 0;
    }
}

/* PipeIncreaseRead
 * Helper for properly doing a wrap-around
 * when buffer boundary is reached */
void
PipeIncreaseRead(
    _In_ MCorePipe_t *Pipe)
{
    // Increase with wrap-around
    Pipe->IndexRead++;
    if (Pipe->IndexRead == Pipe->Length) {
        Pipe->IndexRead = 0;
    }
}

/* PipeCreate
 * Initialise a new pipe of the given size 
 * and with the given flags */
MCorePipe_t*
PipeCreate(
    _In_ size_t Size,
    _In_ Flags_t Flags)
{
    // Allocate both a pipe and a 
    // buffer in kernel memory for the pipe data
    MCorePipe_t *Pipe = (MCorePipe_t*)kmalloc(sizeof(MCorePipe_t));
    Pipe->Buffer = (uint8_t*)kmalloc(Size);
    PipeConstruct(Pipe, Pipe->Buffer, Size, Flags);
    return Pipe;
}

/* PipeConstruct
 * Construct an already existing pipe by resetting the
 * pipe with the given parameters */
void
PipeConstruct(
    _InOut_ MCorePipe_t *Pipe,
    _In_ uint8_t *Buffer,
    _In_ size_t BufferLength,
    _In_ Flags_t Flags)
{
    // Update members
    Pipe->Buffer = Buffer;
    Pipe->ReadQueueCount = 0;
    Pipe->WriteQueueCount = 0;
    Pipe->IndexWrite = 0;
    Pipe->IndexRead = 0;
    Pipe->Length = BufferLength;
    Pipe->Flags = Flags;

    // Construct synchronizations
    CriticalSectionConstruct(&Pipe->Lock, CRITICALSECTION_PLAIN);
    SemaphoreConstruct(&Pipe->ReadQueue, 0);
    SemaphoreConstruct(&Pipe->WriteQueue, 0);
}

/* PipeDestroy
 * Destroys a pipe and wakes up all sleeping threads, then
 * frees all resources allocated */
void
PipeDestroy(
    _In_ MCorePipe_t *Pipe)
{
    // Wake all up so no-one is left behind
    SchedulerWakeupAllThreads((uintptr_t*)&Pipe->ReadQueue);
    SchedulerWakeupAllThreads((uintptr_t*)&Pipe->WriteQueue);
    kfree(Pipe->Buffer);
    kfree(Pipe);
}

/* PipeWrite
 * Writes the given data to the pipe-buffer, unless PIPE_NOBLOCK_WRITE
 * has been specified, it will block untill there is room in the pipe */
int
PipeWrite(
    _In_ MCorePipe_t *Pipe,
    _In_ uint8_t *Data,
    _In_ size_t Length)
{
    // Variables
    size_t BytesWritten = 0;

    // Sanitize parameters
    if (Pipe == NULL || Length == 0 || Data == NULL) {
        return -1;
    }

    // Write in loop
    while (BytesWritten < Length) {
        // Write while there are bytes left, and space in pipe
        // Locked operation
        CriticalSectionEnter(&Pipe->Lock);
        while (PipeBytesLeft(Pipe) > 0 && BytesWritten < Length) {
            Pipe->Buffer[Pipe->IndexWrite] = Data[BytesWritten++];
            PipeIncreaseWrite(Pipe);
        }
        CriticalSectionLeave(&Pipe->Lock);

        // Wakeup one of the readers 
        // Always do this nvm what
        if (Pipe->ReadQueueCount > 0) {
            SemaphoreV(&Pipe->ReadQueue, 1);
            Pipe->ReadQueueCount--;
        }

        // Do we stil need to write more? 
        // Only do this if NOBLOCK is not specified
        if (BytesWritten < Length
            && !(Pipe->Flags & PIPE_NOBLOCK_WRITE)) {
            Pipe->WriteQueueCount++;
            SemaphoreP(&Pipe->WriteQueue, 0);
        }
    }
    return (int)BytesWritten;
}

/* PipeRead
 * Reads the requested data-length from the pipe buffer, unless PIPE_NOBLOCK_READ
 * has been specified, it will block untill data becomes available. If NULL is
 * given as the buffer it will just consume data instead */
int
PipeRead(
    _In_ MCorePipe_t *Pipe,
    _In_ uint8_t *Buffer,
    _In_ size_t Length,
    _In_ int Peek)
{
    // Variables
    ssize_t SavedIndex = 0;
    size_t BytesRead = 0;

    // Sanitize the parameters
    if (Pipe == NULL || Length == 0) {
        return -1;
    }

    // Read data in loop to get all
    SavedIndex = Pipe->IndexRead;
    while (BytesRead == 0) {
        // Only read while there is data available
        CriticalSectionEnter(&Pipe->Lock);
        while (PipeBytesAvailable(Pipe) > 0 && BytesRead < Length) {
            if (Buffer != NULL) {
                Buffer[BytesRead++] = Pipe->Buffer[Pipe->IndexRead];
            }
            else BytesRead++;
            PipeIncreaseRead(Pipe);
        }
        if (Peek) {
            // Restore index if we peaked
            Pipe->IndexRead = SavedIndex;
        }
        CriticalSectionLeave(&Pipe->Lock);

        // Only go to queue if not a peek
        if (!Peek) {
            if (Pipe->WriteQueueCount > 0) {
                SemaphoreP(&Pipe->WriteQueue, 0);
                Pipe->WriteQueueCount--;
            }
            if (BytesRead == 0
                && !(Pipe->Flags & PIPE_NOBLOCK_READ)) {
                Pipe->ReadQueueCount++;
                SemaphoreV(&Pipe->ReadQueue, 1);
            }
        }
    }
    return (int)BytesRead;
}

/* PipeWait
 * Waits for next data to enter pipe before continuing
 * this sleeps/blocks the calling thread */
OsStatus_t
PipeWait(
    _In_ MCorePipe_t *Pipe,
    _In_ size_t Timeout)
{
    // Sanitize parameters
    if (Pipe == NULL) {
        return OsError;
    }

    // Increase wait count
    Pipe->ReadQueueCount++;
    SemaphoreP(&Pipe->ReadQueue, Timeout);
    return OsSuccess;
}

/* PipeBytesAvailable
 * Returns how many bytes are available in buffer to be read */
int
PipeBytesAvailable(
    _In_ MCorePipe_t *Pipe)
{
    // If they are in matching positions, no data 
    if (Pipe->IndexRead == Pipe->IndexWrite) {
        return 0;
    }

    // If the read index is larger than add write
    if (Pipe->IndexRead > Pipe->IndexWrite) {
        return (int)(Pipe->Length - Pipe->IndexRead) + Pipe->IndexWrite;
    }
    else {
        return (int)(Pipe->IndexWrite - Pipe->IndexRead);
    }
}

/* PipeBytesLeft
 * Returns how many bytes are ready for usage/able to be written */
int
PipeBytesLeft(
    _In_ MCorePipe_t *Pipe)
{
    // If read_index == write_index then we have no of data ready
    if (Pipe->IndexRead == Pipe->IndexWrite) {
        return (int)(Pipe->Length - 1);
    }

    // If read index is higher than write, we have wrapped around
    // Otherwise we haven't wrapped, just return difference
    if (Pipe->IndexRead > Pipe->IndexWrite) {
        return (int)(Pipe->IndexRead - Pipe->IndexWrite - 1);
    }    
    return (int)((Pipe->Length - Pipe->IndexWrite) + Pipe->IndexRead - 1);
}
