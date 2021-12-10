using System;
using System.IO;

namespace OSBuilder
{
    public class ImgDisk : IDisk
    {
        private static readonly uint KILOBYTE = 1024;
        private static readonly uint MEGABYTE = (KILOBYTE * 1024);
        private static readonly ulong GIGABYTE = (MEGABYTE * 1024);

        public ulong SectorCount { get { return _totalSectors; } }
        public DiskGeometry Geometry { get { return _diskGeometry; } }

        private ulong _totalSectors = 0;

        private String _imageName = "mollenos.img";
        private DiscUtils.VirtualDisk _diskHandle = null;
        private Stream _fileStream = null;
        private DiskGeometry _diskGeometry = null;
        
        public ImgDisk(uint bytesPerSector, ulong sectorCount)
        {
            _totalSectors = sectorCount;
            _diskGeometry = DiskGeometry.CalculateGeometryLbaAssisted(bytesPerSector * sectorCount, bytesPerSector);
        }

        public ImgDisk(string imageName)
        {
            _imageName = imageName;
        }

        public bool Create()
        {
            // make sure we do not reopen the disk handle
            if (_diskHandle != null)
                return true;
            
            try
            {
                // Always create the image with this name
                if (File.Exists(_imageName))
                    File.Delete(_imageName);

                _fileStream = File.Create(_imageName);
                _diskHandle = DiscUtils.Raw.Disk.Initialize(
                    _fileStream,
                    DiscUtils.Streams.Ownership.None,
                    (long)(_totalSectors * _diskGeometry.BytesPerSector));
            }
            catch (Exception e)
            {
                Console.WriteLine(e.Message);
                return false;
            }
            return true;
        }

        public bool Open()
        {
            try
            {
                _diskHandle = DiscUtils.Raw.Disk.OpenDisk(_imageName, FileAccess.ReadWrite);
                _diskGeometry = new DiskGeometry(
                    (uint)_diskHandle.Geometry.Cylinders,
                    (uint)_diskHandle.Geometry.HeadsPerCylinder,
                    (uint)_diskHandle.Geometry.SectorsPerTrack,
                    (uint)_diskHandle.Geometry.BytesPerSector);
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
                ulong seekOffset = atSector * _diskGeometry.BytesPerSector;
                Seek((long)seekOffset);
            }
            _diskHandle.Content.Write(buffer, 0, buffer.Length);
            _diskHandle.Content.Flush();
        }

        public byte[] Read(ulong sector, ulong sectorCount)
        {
            if (_diskHandle == null)
                throw new Exception("Disk not open");

            byte[] buffer = new Byte[sectorCount * _diskGeometry.BytesPerSector];
            ulong seekOffset = sector * _diskGeometry.BytesPerSector;

            // Prepare disk access by seeking to position
            Seek((long)seekOffset);

            _diskHandle.Content.Read(buffer, 0, buffer.Length);
            return buffer;
        }
    }
}
