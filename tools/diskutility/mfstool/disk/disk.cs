using Microsoft.Win32.SafeHandles;
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace DiskUtility
{
    public class CDisk
    {
        // Variables
        public UInt32 BytesPerSector;
        public UInt32 SectorsPerTrack;
        public UInt32 TracksPerCylinder;
        public UInt64 TotalSectors;

        private String DeviceId;
        private Object vDiskHandle;
        private SafeFileHandle sfHandle;
        private Stream fStream;
        
        // Seek offsets
        public enum EMoveMethod : uint
        {
            Begin = 0,
            Current = 1,
            End = 2
        }

        // Import direct disk control functinos
        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        internal static extern SafeFileHandle CreateFile(string lpFileName, uint
        dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint
        dwFlagsAndAttributes,
        SafeFileHandle hTemplateFile);
        const uint GENERIC_READ = 0x80000000;
        const uint GENERIC_WRITE = 0x050000000;
        public const int FILE_FLAG_NO_BUFFERING = 0x20000000;
        internal const int OPEN_EXISTING = 3;

        [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true, CharSet = CharSet.Auto)]
        private static extern bool DeviceIoControl(
            IntPtr hDevice,
            uint dwIoControlCode,
            IntPtr lpInBuffer,
            uint nInBufferSize,
            IntPtr lpOutBuffer,
            uint nOutBufferSize,
            out uint lpBytesReturned,
            IntPtr lpOverlapped
        );
        const int FSCTL_LOCK_VOLUME = 0x00090018;
        const int FSCTL_DISMOUNT_VOLUME = 0x00090020;
        const int FSCTL_UNLOCK_VOLUME = 0x00090022;

        [DllImport("kernel32", SetLastError = true)]
        static extern bool ReadFile(
            IntPtr hFile,
            byte[] lpBuffer,
            uint numBytesToRead,
            out uint numBytesRead,
            IntPtr lpOverlapped
        );

        [DllImport("kernel32.dll")]
        static extern bool WriteFile(
            IntPtr hFile,
            byte[] lpBuffer,
            uint nNumberOfBytesToWrite,
            out uint lpNumberOfBytesWritten,
            IntPtr lpOverlapped
        );

        [DllImport("kernel32.dll", EntryPoint = "SetFilePointer")]
        static extern uint SetFilePointer(
              [In] SafeFileHandle hFile,
              [In] int lDistanceToMove,
              [In, Out] ref int lpDistanceToMoveHigh,
              [In] EMoveMethod dwMoveMethod);

        /* Constructor
         * Initializes disk class and disk geometry */
        public CDisk(String pDeviceId, UInt32 pBytesPerSector, UInt32 pSectorsPerTrack, 
            UInt32 pTracksPerCylinder, UInt64 pTotalSectors)
        {
            // Store values
            DeviceId = pDeviceId;
            BytesPerSector = pBytesPerSector;
            SectorsPerTrack = pSectorsPerTrack;
            TracksPerCylinder = pTracksPerCylinder;
            TotalSectors = pTotalSectors;

            // Initialize rest to 0
            vDiskHandle = null;
            sfHandle = null;
        }

        /* Open
         * Opens and initializes disk access with the given type
         * and and path to device (if live) */
        public bool Open()
        {
            // Write
            Console.WriteLine("Initializing disk image on target " + DeviceId);

            // Check for disk identifiers
            if (DeviceId == "VMDK" || DeviceId == "IMG")
            {
                // Calculate size of harddisk in bytes
                UInt64 SizeOfHdd = BytesPerSector * TotalSectors;

                // Always create the image with this name
                if (File.Exists("mollenos." + DeviceId.ToLower()))
                    File.Delete("mollenos." + DeviceId.ToLower());

                // Initialize disk-handle
                if (DeviceId == "VMDK")
                    vDiskHandle =  DiscUtils.Vmdk.Disk.Initialize("mollenos." + DeviceId.ToLower(), (long)SizeOfHdd, 
                        DiscUtils.Vmdk.DiskCreateType.MonolithicSparse);
                else if (DeviceId == "IMG") {
                    fStream = File.Create("mollenos." + DeviceId.ToLower());
                    vDiskHandle = DiscUtils.Raw.Disk.Initialize(fStream, DiscUtils.Ownership.None, (long)SizeOfHdd);
                }

                // Hardcode these variables
                SectorsPerTrack = 63;
                TracksPerCylinder = 255;
            }
            else
            {
                // Variables
                uint lpBytesReturned = 0;

                // Assume direct-disk
                sfHandle = CreateFile(DeviceId,
                    GENERIC_READ | GENERIC_WRITE, 0, IntPtr.Zero,
                    OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, new SafeFileHandle(IntPtr.Zero, true));

                // Sanitize handle
                if (sfHandle.IsInvalid)
                {
                    Console.WriteLine("Failed to open disk with id " + DeviceId);
                    return false;
                }

                // Lock disk and unmount
                if (!DeviceIoControl(sfHandle.DangerousGetHandle(), FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out lpBytesReturned, IntPtr.Zero))
                {
                    Console.WriteLine("Failed to lock disk with id " + DeviceId);
                    return false;
                }
                    
                if (!DeviceIoControl(sfHandle.DangerousGetHandle(), FSCTL_DISMOUNT_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out lpBytesReturned, IntPtr.Zero))
                {
                    Console.WriteLine("Failed to unmount disk with id " + DeviceId);
                    return false;
                }
            }

            // Asssume success
            return true;
        }

        /* Close
         * Closes the access to the disk and cleans up any resources */
        public void Close()
        {
            // Write
            Console.WriteLine("Finishing disk image on target " + DeviceId);

            // Check for disk identifiers
            if (DeviceId == "VMDK" || DeviceId == "IMG") {
                if (DeviceId == "VMDK") {
                    ((DiscUtils.Vmdk.Disk)vDiskHandle).Dispose();
                }
                else if (DeviceId == "IMG") {
                    ((DiscUtils.Raw.Disk)vDiskHandle).Dispose();
                    fStream.Close();
                }
            }
            else
                sfHandle.Close();

            // Null members
            vDiskHandle = null;
            sfHandle = null;
            fStream = null;
        }

        /* Seek
         * Seeks the disk-position to the given offset, offset provided must
         * be both absolute and within bounds */
        public void Seek(Int64 pOffset)
        {
            // We have to handle each case differently
            if (DeviceId == "VMDK") {
                ((DiscUtils.Vmdk.Disk)vDiskHandle).Content.Seek((long)pOffset, SeekOrigin.Begin);
            }
            else if (DeviceId == "IMG") {
                ((DiscUtils.Raw.Disk)vDiskHandle).Content.Seek((long)pOffset, SeekOrigin.Begin);
            }
            else {
                int DistHigh = (int)((pOffset >> 32) & 0xFFFFFFFF);
                int DistLow = (int)(pOffset & 0xFFFFFFFF);
                SetFilePointer(sfHandle, DistLow, ref DistHigh, EMoveMethod.Begin);
            }
        }

        /* Write 
         * Writes sector-aligned buffers to the disk, if it needs to be
         * written at a given sector, specifiy pSeek = true */
        public void Write(Byte[] pBuffer, UInt64 pSector = 0, Boolean pSeek = false)
        {
            // If we asked to seek, then handle the case
            if (pSeek) {
                // Calculate the absolute offset
                Int64 ValToMove = ((Int64)pSector * (Int64)BytesPerSector);
                Seek(ValToMove);
            }

            // We must handle all these cases differently again
            if (DeviceId == "VMDK") {
                ((DiscUtils.Vmdk.Disk)vDiskHandle).Content.Write(pBuffer, 0, pBuffer.Length);
                ((DiscUtils.Vmdk.Disk)vDiskHandle).Content.Flush();
            }
            else if (DeviceId == "IMG") {
                ((DiscUtils.Raw.Disk)vDiskHandle).Content.Write(pBuffer, 0, pBuffer.Length);
                ((DiscUtils.Raw.Disk)vDiskHandle).Content.Flush();
            }
            else {
                uint BytesWritten = 0;
                WriteFile(sfHandle.DangerousGetHandle(), pBuffer, (uint)pBuffer.Length, out BytesWritten, IntPtr.Zero);
            }
        }

        /* Read 
         * Reads the specified number of sectors into a byte buffer at 
         * the given absolute sector */
        public Byte[] Read(UInt64 pSector, UInt64 pSectorCount)
        {
            // Allocate a return buffer
            Byte[] RetBuf = new Byte[pSectorCount * BytesPerSector];

            // Prepare disk access by seeking to position
            Seek((Int64)pSector * (Int64)BytesPerSector);

            // Handle each output case differently
            if (DeviceId == "VMDK") {
                ((DiscUtils.Vmdk.Disk)vDiskHandle).Content.Read(RetBuf, 0, RetBuf.Length);
            }
            else if (DeviceId == "IMG") {
                ((DiscUtils.Raw.Disk)vDiskHandle).Content.Read(RetBuf, 0, RetBuf.Length);
            }
            else {
                uint bRead = 0;
                ReadFile(sfHandle.DangerousGetHandle(), RetBuf, (uint)RetBuf.Length, out bRead, IntPtr.Zero);
            }

            // Return the buffer created
            return RetBuf;
        }
    }
}
