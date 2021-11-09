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
    public class Disk
    {
        private static readonly uint KILOBYTE = 1024;
        private static readonly uint MEGABYTE = (KILOBYTE * 1024);
        private static readonly ulong GIGABYTE = (MEGABYTE * 1024);

        public uint BytesPerSector { get { return _bytesPerSector; }  }
        public ulong SectorCount { get { return _totalSectors; }  }
        public uint SectorsPerTrack { get { return _sectorsPerTrack; }  }
        public uint Heads { get { return _heads; }  }
        public uint Cylinders { get { return _cylinders; }  }

        private uint _bytesPerSector = 0;
        private uint _sectorsPerTrack = 0;
        private uint _heads = 0;
        private uint _cylinders = 0;
        private ulong _totalSectors = 0;

        private String _deviceId;
        private Object _diskHandle = null;
        private SafeFileHandle _nativeHandle = null;
        private Stream _fileStream = null;
        
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

        public Disk(string deviceId, uint bytesPerSector, ulong sectorCount)
        {
            _deviceId = deviceId;
            _bytesPerSector = bytesPerSector;
            _totalSectors = sectorCount;

            ulong sizeOfHdd = _bytesPerSector * _totalSectors;
            CalculateGeometry(sizeOfHdd);
        }

        public Disk(string deviceId)
        {
            _deviceId = deviceId;
        }

        internal void CalculateGeometry(ulong diskSize)
        {
            if (diskSize < GIGABYTE)
            {
                _heads = 64;
                _sectorsPerTrack = 32;
            }
            else if (diskSize < (2UL * GIGABYTE))
            {
                _heads = 128;
                _sectorsPerTrack = 32;
            }
            else
            {
                _heads = 255;
                _sectorsPerTrack = 63;
            }

            _cylinders = (uint)(diskSize / (_heads * _sectorsPerTrack * _bytesPerSector));
        }

        public bool Create()
        {
            // make sure we do not reopen the disk handle
            if (_diskHandle != null || _nativeHandle != null)
                return true;
            
            ulong sizeOfHdd = _bytesPerSector * _totalSectors;

            try
            {
                var imageName = "mollenos." + _deviceId.ToLower();

                // Always create the image with this name
                if (File.Exists(imageName))
                    File.Delete(imageName);

                if (_deviceId.ToUpper() == "VMDK")
                    _diskHandle =  DiscUtils.Vmdk.Disk.Initialize(imageName, (long)sizeOfHdd, 
                        DiscUtils.Vmdk.DiskCreateType.MonolithicSparse);
                else if (_deviceId.ToUpper() == "IMG") {
                    _fileStream = File.Create(imageName);
                    _diskHandle = DiscUtils.Raw.Disk.Initialize(_fileStream, DiscUtils.Streams.Ownership.None, (long)sizeOfHdd);
                }
                else {
                    throw new Exception("Unsupported image type");
                }
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                return false;
            }
            CalculateGeometry(sizeOfHdd);
            return true;
        }

        public bool OpenImage()
        {
            Console.WriteLine("Initializing disk image on target " + _deviceId);
            try
            {
                if (_deviceId.ToLower().EndsWith("vmdk")) {
                    _diskHandle = DiscUtils.Vmdk.Disk.OpenDisk(_deviceId, FileAccess.ReadWrite);
                    _bytesPerSector = (uint)((DiscUtils.Vmdk.Disk)_diskHandle).Geometry.BytesPerSector;
                    _sectorsPerTrack = (uint)((DiscUtils.Vmdk.Disk)_diskHandle).Geometry.SectorsPerTrack;
                    _heads = (uint)((DiscUtils.Vmdk.Disk)_diskHandle).Geometry.HeadsPerCylinder;
                    _totalSectors = (ulong)((DiscUtils.Vmdk.Disk)_diskHandle).Geometry.TotalSectorsLong;
                    return true;
                }
                else if (_deviceId.ToLower().EndsWith("img")) {
                    _diskHandle = DiscUtils.Raw.Disk.OpenDisk(_deviceId, FileAccess.ReadWrite);
                    _bytesPerSector = (uint)((DiscUtils.Raw.Disk)_diskHandle).Geometry.BytesPerSector;
                    _sectorsPerTrack = (uint)((DiscUtils.Raw.Disk)_diskHandle).Geometry.SectorsPerTrack;
                    _heads = (uint)((DiscUtils.Raw.Disk)_diskHandle).Geometry.HeadsPerCylinder;
                    _totalSectors = (ulong)((DiscUtils.Raw.Disk)_diskHandle).Geometry.TotalSectorsLong;
                    return true;
                }
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
            }
            return false;
        }

        public bool OpenDevice()
        {
            // make sure we do not reopen the disk handle
            if (_diskHandle != null || _nativeHandle != null)
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

        public bool IsOpen()
        {
            return _diskHandle != null || _nativeHandle != null;
        }

        public void Close()
        {
            if (_diskHandle == null && _nativeHandle == null)
                throw new Exception("Disk not open");
            
            Console.WriteLine("Finishing disk image on target " + _deviceId);

            // Check for disk identifiers
            if (_deviceId == "VMDK" || _deviceId == "IMG") {
                if (_deviceId == "VMDK") {
                    ((DiscUtils.Vmdk.Disk)_diskHandle).Dispose();
                }
                else if (_deviceId == "IMG") {
                    ((DiscUtils.Raw.Disk)_diskHandle).Dispose();
                    if (_fileStream != null)
                        _fileStream.Close();
                }
            }
            else
                _nativeHandle.Close();

            // Reset members
            _diskHandle = null;
            _nativeHandle = null;
            _fileStream = null;
        }

        public void Seek(long offset)
        {
            if (_diskHandle == null && _nativeHandle == null)
                throw new Exception("Disk not open");

            // We have to handle each case differently
            if (_deviceId == "VMDK") {
                ((DiscUtils.Vmdk.Disk)_diskHandle).Content.Seek(offset, SeekOrigin.Begin);
            }
            else if (_deviceId == "IMG") {
                ((DiscUtils.Raw.Disk)_diskHandle).Content.Seek(offset, SeekOrigin.Begin);
            }
            else {
                int posHigh = (int)((offset >> 32) & 0xFFFFFFFF);
                int posLow = (int)(offset & 0xFFFFFFFF);
                SetFilePointer(_nativeHandle, posLow, ref posHigh, EMoveMethod.Begin);
            }
        }

        public void Write(byte[] buffer, ulong atSector = 0, bool seekFirst = false)
        {
            if (_diskHandle == null && _nativeHandle == null)
                throw new Exception("Disk not open");

            // If we asked to seek, then handle the case
            if (seekFirst) {
                // Calculate the absolute offset
                ulong seekOffset = atSector * _bytesPerSector;
                Seek((long)seekOffset);
            }

            // We must handle all these cases differently again
            if (_deviceId == "VMDK") {
                ((DiscUtils.Vmdk.Disk)_diskHandle).Content.Write(buffer, 0, buffer.Length);
                ((DiscUtils.Vmdk.Disk)_diskHandle).Content.Flush();
            }
            else if (_deviceId == "IMG") {
                ((DiscUtils.Raw.Disk)_diskHandle).Content.Write(buffer, 0, buffer.Length);
                ((DiscUtils.Raw.Disk)_diskHandle).Content.Flush();
            }
            else {
                uint BytesWritten = 0;
                WriteFile(_nativeHandle.DangerousGetHandle(), buffer, (uint)buffer.Length, out BytesWritten, IntPtr.Zero);
            }
        }

        public byte[] Read(ulong sector, ulong sectorCount)
        {
            if (_diskHandle == null && _nativeHandle == null)
                throw new Exception("Disk not open");

            byte[] buffer = new Byte[sectorCount * _bytesPerSector];
            ulong seekOffset = sector * _bytesPerSector;

            // Prepare disk access by seeking to position
            Seek((long)seekOffset);

            // Handle each output case differently
            try
            {
                if (_deviceId == "VMDK") {
                    ((DiscUtils.Vmdk.Disk)_diskHandle).Content.Read(buffer, 0, buffer.Length);
                }
                else if (_deviceId == "IMG") {
                    ((DiscUtils.Raw.Disk)_diskHandle).Content.Read(buffer, 0, buffer.Length);
                }
                else {
                    uint bRead = 0;
                    ReadFile(_nativeHandle.DangerousGetHandle(), buffer, (uint)buffer.Length, out bRead, IntPtr.Zero);
                }
            }
            catch (Exception e)
            {
                Console.WriteLine($"Read(ulong sector={sector}, ulong sectorCount={sectorCount})");
                Console.WriteLine("Failed to read disk with id " + _deviceId);
                Console.WriteLine(e.Message);
                throw e;
            }
            return buffer;
        }
    }
}
