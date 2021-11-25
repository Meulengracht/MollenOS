using System;

namespace OSBuilder.FileSystems.FAT
{
    public class FileSystem : IFileSystem
    {
        public static readonly byte TYPE = 0xC;

        private String _partitionName;
        private Guid _partitionGuid = Guid.NewGuid();
        private bool _efiSystemPartition = false;
        private bool _bootable = false;
        private IDisk _disk = null;
        private ulong _sector = 0;
        private ulong _sectorCount = 0;
        private ushort _reservedSectorCount = 0;
        private DiscUtils.Fat.FatFileSystem _fileSystem = null;

        public FileSystem(string partitionName, FileSystemAttributes attributes)
        {
            _partitionName = partitionName;
            if (attributes.HasFlag(FileSystemAttributes.Boot)) {
                _bootable = true;
            }
            if (attributes.HasFlag(FileSystemAttributes.EFI))
            {
                _efiSystemPartition = true;
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
                (int)_disk.Heads,
                (int)_disk.SectorsPerTrack,
                (int)_disk.BytesPerSector
            );
        }

        public bool Format()
        {
            _fileSystem = DiscUtils.Fat.FatFileSystem.FormatPartition(
                _disk.GetStream(),
                _partitionName,
                GetGeometry(),
                (int)_sector,
                (int)_sectorCount,
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

            var files = _fileSystem.GetFiles(path);
            foreach (var file in files)
            {
                Console.WriteLine(file);
            }
            return true;
        }

        public bool WriteFile(string localPath, FileFlags flags, byte[] buffer)
        {
            if (_fileSystem == null)
            {
                return false;
            }

            var file = _fileSystem.OpenFile(localPath, System.IO.FileMode.CreateNew, System.IO.FileAccess.ReadWrite);
            file.Write(buffer, 0, buffer.Length);
            file.Close();
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
            return _efiSystemPartition ? DiskLayouts.GPTGuids.EfiSystemPartition : DiskLayouts.GPTGuids.Fat32;
        }

        public Guid GetFileSystemGuid()
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
