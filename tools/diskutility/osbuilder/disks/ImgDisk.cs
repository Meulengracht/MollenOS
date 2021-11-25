using System;
using System.IO;

namespace OSBuilder
{
    public class ImgDisk : IDisk
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

        private String _imageName = "mollenos.img";
        private DiscUtils.VirtualDisk _diskHandle = null;
        private Stream _fileStream = null;
        
        public ImgDisk(uint bytesPerSector, ulong sectorCount)
        {
            _bytesPerSector = bytesPerSector;
            _totalSectors = sectorCount;

            ulong sizeOfHdd = _bytesPerSector * _totalSectors;
            CalculateGeometry(sizeOfHdd);
        }

        public ImgDisk(string imageName)
        {
            _imageName = imageName;
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
            if (_diskHandle != null)
                return true;
            
            ulong sizeOfHdd = _bytesPerSector * _totalSectors;

            try
            {
                // Always create the image with this name
                if (File.Exists(_imageName))
                    File.Delete(_imageName);

                _fileStream = File.Create(_imageName);
                _diskHandle = DiscUtils.Raw.Disk.Initialize(_fileStream, DiscUtils.Streams.Ownership.None, (long)sizeOfHdd);
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                return false;
            }
            CalculateGeometry(sizeOfHdd);
            return true;
        }

        public bool Open()
        {
            try
            {
                _diskHandle = DiscUtils.Raw.Disk.OpenDisk(_imageName, FileAccess.ReadWrite);
                _bytesPerSector = (uint)_diskHandle.Geometry.BytesPerSector;
                _sectorsPerTrack = (uint)_diskHandle.Geometry.SectorsPerTrack;
                _heads = (uint)_diskHandle.Geometry.HeadsPerCylinder;
                _totalSectors = (ulong)_diskHandle.Geometry.TotalSectorsLong;
                return true;
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
            }
            return false;
        }

        public Stream GetStream()
        {
            if (!IsOpen())
                return null;
            return _diskHandle.Content;
        }

        public bool IsOpen()
        {
            return _diskHandle != null;
        }

        public void Close()
        {
            if (_diskHandle == null)
                throw new Exception("Disk not open");
            
            _diskHandle.Dispose();
            if (_fileStream != null)
                _fileStream.Close();

            // Reset members
            _diskHandle = null;
            _fileStream = null;
        }

        public void Seek(long offset)
        {
            if (_diskHandle == null)
                throw new Exception("Disk not open");
            _diskHandle.Content.Seek(offset, SeekOrigin.Begin);
        }

        public void Write(byte[] buffer, ulong atSector = 0, bool seekFirst = false)
        {
            if (_diskHandle == null)
                throw new Exception("Disk not open");

            // If we asked to seek, then handle the case
            if (seekFirst) {
                // Calculate the absolute offset
                ulong seekOffset = atSector * _bytesPerSector;
                Seek((long)seekOffset);
            }
            _diskHandle.Content.Write(buffer, 0, buffer.Length);
            _diskHandle.Content.Flush();
        }

        public byte[] Read(ulong sector, ulong sectorCount)
        {
            if (_diskHandle == null)
                throw new Exception("Disk not open");

            byte[] buffer = new Byte[sectorCount * _bytesPerSector];
            ulong seekOffset = sector * _bytesPerSector;

            // Prepare disk access by seeking to position
            Seek((long)seekOffset);

            _diskHandle.Content.Read(buffer, 0, buffer.Length);
            return buffer;
        }
    }
}
