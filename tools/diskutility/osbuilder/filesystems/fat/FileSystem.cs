using System;
using System.IO;

namespace OSBuilder.FileSystems.FAT
{
    public class FileSystem : IFileSystem
    {
        public static readonly byte TYPE = 0xC;

        private String _partitionName;
        private Guid _partitionGuid;
        private bool _bootable = false;
        private IDisk _disk = null;
        private Stream _stream = null;
        private ulong _sector = 0;
        private ulong _sectorCount = 0;
        private ushort _reservedSectorCount = 0;
        private DiscUtils.Fat.FatFileSystem _fileSystem = null;

        public FileSystem(string partitionName, Guid partitionGuid, FileSystemAttributes attributes)
        {
            _partitionName = partitionName;
            _partitionGuid = partitionGuid;
            if (attributes.HasFlag(FileSystemAttributes.Boot)) {
                _bootable = true;
            }
        }

        public void Dispose()
        {
            if (_fileSystem != null) {
                _fileSystem.Dispose();
                _fileSystem = null;
            }

            // write the stream back to the disk
            if (_stream != null) {
                _disk.Seek((long)(_sector * _disk.Geometry.BytesPerSector));
                
                _stream.Flush();
                _stream.Seek(0, SeekOrigin.Begin);
                _stream.CopyTo(_disk.GetStream());
                _stream.Close();
                _stream = null;
            }
        }

        public void Initialize(IDisk disk, ulong startSector, ulong sectorCount)
        {
            _disk = disk;
            _sector = startSector;
            _sectorCount = sectorCount;
        }

        private DiscUtils.Geometry GetGeometry()
        {
            return new DiscUtils.Geometry(
                (long)_disk.SectorCount,
                (int)_disk.Geometry.HeadsPerCylinder,
                (int)_disk.Geometry.SectorsPerTrack,
                (int)_disk.Geometry.BytesPerSector
            );
        }

        public bool Format()
        {
            _stream = new TemporaryFileStream();
            _fileSystem = DiscUtils.Fat.FatFileSystem.FormatPartition(
                _stream,
                _partitionName,
                GetGeometry(),
                0, (int)_sectorCount,
                (short)_reservedSectorCount
            );
            return _fileSystem != null;
        }

        public bool ListDirectory(string path)
        {
            if (_fileSystem == null)
            {
                return false;
            }

            var files = _fileSystem.GetFiles(path.Replace('/', '\\'));
            foreach (var file in files)
            {
                Console.WriteLine(file);
            }
            return true;
        }

        public bool CreateFile(string localPath, FileFlags flags, byte[] buffer)
        {
            if (_fileSystem == null)
            {
                return false;
            }

            var file = _fileSystem.OpenFile(localPath.Replace('/', '\\'), System.IO.FileMode.CreateNew, System.IO.FileAccess.ReadWrite);
            file.Write(buffer, 0, buffer.Length);
            file.Close();
            return true;
        }

        public bool CreateDirectory(string localPath, FileFlags flags)
        {
            if (_fileSystem == null)
            {
                return false;
            }
            
            _fileSystem.CreateDirectory(localPath.Replace('/', '\\'));
            return true;
        }

        public bool IsBootable()
        {
            return _bootable;
        }

        public byte GetFileSystemType()
        {
            return TYPE;
        }

        public Guid GetFileSystemTypeGuid()
        {
            return _partitionGuid;
        }

        public ulong GetSectorStart()
        {
            return _sector;
        }

        public ulong GetSectorCount()
        {
            return _sectorCount;
        }

        public string GetName()
        {
            return _partitionName;
        }
    }
}
