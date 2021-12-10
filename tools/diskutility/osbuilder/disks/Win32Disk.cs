using Microsoft.Win32.SafeHandles;
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace OSBuilder
{
    public class Win32Disk : IDisk
    {
        private static readonly uint KILOBYTE = 1024;
        private static readonly uint MEGABYTE = (KILOBYTE * 1024);
        private static readonly ulong GIGABYTE = (MEGABYTE * 1024);

        public ulong SectorCount { get { return _totalSectors; }  }
        public DiskGeometry Geometry { get { return _diskGeometry; } }

        private ulong _totalSectors = 0;

        private String _deviceId;
        private SafeFileHandle _nativeHandle = null;
        private DiskGeometry _diskGeometry = null;
        
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

        public Win32Disk(string deviceId, uint bytesPerSector, ulong sectorCount)
        {
            _deviceId = deviceId;
            _totalSectors = sectorCount;
            _diskGeometry = DiskGeometry.CalculateGeometryLbaAssisted(bytesPerSector * sectorCount, bytesPerSector);
        }
        
        public bool Create()
        {
            // not supported on live disks
            return false;
        }

        public bool Open()
        {
            // make sure we do not reopen the disk handle
            if (_nativeHandle != null)
                return true;

            uint lpBytesReturned = 0;

            // Assume direct-disk
            _nativeHandle = CreateFile(_deviceId,
                GENERIC_READ | GENERIC_WRITE, 0, IntPtr.Zero,
                OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, new SafeFileHandle(IntPtr.Zero, true));
            if (_nativeHandle.IsInvalid)
            {
                Console.WriteLine("Failed to open disk with id " + _deviceId);
                return false;
            }

            // Lock disk and unmount
            if (!DeviceIoControl(_nativeHandle.DangerousGetHandle(), FSCTL_LOCK_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out lpBytesReturned, IntPtr.Zero))
            {
                Console.WriteLine("Failed to lock disk with id " + _deviceId);
                return false;
            }
                
            if (!DeviceIoControl(_nativeHandle.DangerousGetHandle(), FSCTL_DISMOUNT_VOLUME, IntPtr.Zero, 0, IntPtr.Zero, 0, out lpBytesReturned, IntPtr.Zero))
            {
                Console.WriteLine("Failed to unmount disk with id " + _deviceId);
                return false;
            }
            return true;
        }

        public Stream GetStream()
        {
            if (!IsOpen())
                return null;
            return null;
        }

        public bool IsOpen()
        {
            return _nativeHandle != null;
        }

        public void Close()
        {
            if (_nativeHandle == null)
                throw new Exception("Disk not open");
            
            _nativeHandle.Close();
            _nativeHandle = null;
        }

        public void Seek(long offset)
        {
            if (_nativeHandle == null)
                throw new Exception("Disk not open");

            int posHigh = (int)((offset >> 32) & 0xFFFFFFFF);
            int posLow = (int)(offset & 0xFFFFFFFF);
            SetFilePointer(_nativeHandle, posLow, ref posHigh, EMoveMethod.Begin);
        }

        public void Write(byte[] buffer, ulong atSector = 0, bool seekFirst = false)
        {
            if (_nativeHandle == null)
                throw new Exception("Disk not open");

            // If we asked to seek, then handle the case
            if (seekFirst) {
                // Calculate the absolute offset
                ulong seekOffset = atSector * _diskGeometry.BytesPerSector;
                Seek((long)seekOffset);
            }

            uint BytesWritten = 0;
            WriteFile(_nativeHandle.DangerousGetHandle(), buffer, (uint)buffer.Length, out BytesWritten, IntPtr.Zero);
        }

        public byte[] Read(ulong sector, ulong sectorCount)
        {
            if (_nativeHandle == null)
                throw new Exception("Disk not open");

            byte[] buffer = new Byte[sectorCount * _diskGeometry.BytesPerSector];
            ulong seekOffset = sector * _diskGeometry.BytesPerSector;

            // Prepare disk access by seeking to position
            Seek((long)seekOffset);
            
            uint bRead = 0;
            ReadFile(_nativeHandle.DangerousGetHandle(), buffer, (uint)buffer.Length, out bRead, IntPtr.Zero);
            return buffer;
        }
    }
}
