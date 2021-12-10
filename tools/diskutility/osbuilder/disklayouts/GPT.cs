using System.Collections.Generic;
using System.IO;
using System.Linq;
using System;

namespace OSBuilder.DiskLayouts
{
    public class GPT : IDiskScheme
    {
        private GPTHeader _header;

        private IDisk _disk = null;
        private List<FileSystems.IFileSystem> _fileSystems = new List<FileSystems.IFileSystem>();
        private ulong _firstUsableSector = 2;
        private ulong _lastUsableSector = 2;
        private ulong _allocatedSectors = 0;

        private void ParseGPTEntries(ulong sector, uint entries, uint entrySize)
        {
            ulong sectorCount = ((entries * entrySize) / _disk.Geometry.BytesPerSector) + 1;
            byte[] data = _disk.Read(sector, sectorCount); 

            for (uint i = 0; i < entries; i++)
            {
                var entry = GPTPartitionEntry.Parse(data, (int)(i * entrySize));
                var partitionSize = (entry.LastSector - entry.FirstSector) + 1;
                if (entry.TypeGuid == GPTGuids.ValiSystemPartition ||
                    entry.TypeGuid == GPTGuids.ValiDataPartition ||
                    entry.TypeGuid == GPTGuids.ValiUserPartition ||
                    entry.TypeGuid == GPTGuids.ValiDataUserPartition)
                {
                    _fileSystems.Add(new FileSystems.MFS.FileSystem(_disk, entry.FirstSector, partitionSize));
                }
                _allocatedSectors += partitionSize;
            }
        }
        
        private bool ParseGPT()
        {
            // GPT header located at LBA 1
            byte[] gptData = _disk.Read(1, 1);
            _header = GPTHeader.Parse(gptData);
            if (_header == null)
                return false;

            _firstUsableSector = _header.FirstUsableLBA;
            _lastUsableSector = _header.LastUsableLBA;

            // parse partition entries
            ParseGPTEntries(
                _header.PartitionEntryLBA, 
                _header.NumberOfPartitionEntries, 
                _header.SizeOfPartitionEntry
            );
            return true;
        }

        public bool Open(IDisk disk)
        {
            // Store disk
            _disk = disk;

            // ensure disk is open for read/write
            if (!_disk.IsOpen())
                return false;

            return ParseGPT();
        }

        private int GetSectorCountForPartitionTable(IDisk disk)
        {
            return (int)(16384 / disk.Geometry.BytesPerSector);
        }

        public bool Create(IDisk disk)
        {
            _disk = disk;
            _header = new GPTHeader();

            if (!File.Exists("deploy/mbr-gpt.sys"))
            {
                Console.WriteLine("Format - GPT scheme requested but mbr-gpt.sys is not found (deploy/mbr-gpt.sys)");
                return false;
            }

            // ensure disk is open for read/write
            if (!_disk.IsOpen())
                return false;

            // set the usuable sectors
            _firstUsableSector = 2 + (ulong)GetSectorCountForPartitionTable(disk);
            _lastUsableSector = _disk.SectorCount - (2 + (ulong)GetSectorCountForPartitionTable(disk));
            return true;
        }

        public void Dispose()
        {
            if (_disk == null)
                return;
            
            // finalize the GPT layout
            FinalizeLayout();

            // dispose of filesystems
            foreach (var fs in _fileSystems)
            {
                fs.Dispose();
            }
            
            // cleanup
            _fileSystems.Clear();
            _disk = null;
        }

        public bool AddPartition(FileSystems.IFileSystem fileSystem, ulong sectorCount)
        {
            ulong partitionSize = sectorCount;
            if (_disk == null || fileSystem == null)
                return false;

            if (GetFreeSectorCount() < partitionSize)
            {
                partitionSize = GetFreeSectorCount();
                Console.WriteLine("AddPartition - not enough space, reducing partition size to " + partitionSize + " sectors");
            }

            // Initialize the file-system
            fileSystem.Initialize(_disk, _firstUsableSector + _allocatedSectors, partitionSize);
            
            // Add sectors allocated
            _fileSystems.Add(fileSystem);
            _allocatedSectors += partitionSize;
            return fileSystem.Format();
        }

        private bool WriteMBR()
        {
            if (_disk == null)
                return false;

            Console.WriteLine("Finalize - loading mbr (mbr-gpt.sys)");
            byte[] mbr = File.ReadAllBytes("deploy/mbr-gpt.sys");

            _disk.Write(mbr, 0, true);
            return true;
        }
        
        private GPTPartitionAttributes GetAttributes(FileSystems.IFileSystem fileSystem)
        {
            GPTPartitionAttributes attributes = GPTPartitionAttributes.None;
            if (fileSystem.IsBootable())
                attributes |= GPTPartitionAttributes.LegacyBiosBootable;
            return attributes;
        }

        private void WriteGPTEntry(FileSystems.IFileSystem fileSystem, byte[] data, int offset)
        {
            var entry = new GPTPartitionEntry();
            entry.Name = fileSystem.GetName();
            entry.TypeGuid = fileSystem.GetFileSystemTypeGuid();
            entry.UniqueId = Guid.NewGuid();
            entry.FirstSector = fileSystem.GetSectorStart();
            entry.LastSector = fileSystem.GetSectorStart() + fileSystem.GetSectorCount() - 1;
            entry.Attributes = GetAttributes(fileSystem);

            entry.Write(data, offset);
        }

        private bool WriteGPT(bool isBackup)
        {
            byte[] headerData = new byte[_disk.Geometry.BytesPerSector];
            byte[] entriesData = new byte[_disk.Geometry.BytesPerSector * GetSectorCountForPartitionTable(_disk)];

            // write the entries
            for (int i = 0; i < _fileSystems.Count; i++)
            {
                var fileSystem = _fileSystems[i];
                WriteGPTEntry(fileSystem, entriesData, (int)(i * _header.SizeOfPartitionEntry));
            }

            // calculate CRC data
            Crc32 crc = new Crc32();
            if (_header.PartitionEntryArrayCRC32 == 0)
            {
                var cutData = entriesData.Take((int)(_header.NumberOfPartitionEntries * _header.SizeOfPartitionEntry));
                _header.PartitionEntryArrayCRC32 = crc.Get<byte>(cutData);
            }

            // write the header first to calculate CRC
            _header.HeaderCRC32 = 0;
            _header.Write(headerData);
            _header.HeaderCRC32 =  crc.Get<byte>(headerData.Take((int)_header.HeaderSize));
            _header.Write(headerData);

            // write to disk
            if (isBackup)
            {
                _disk.Write(entriesData, _header.PartitionEntryLBA, true);
                _disk.Write(headerData);
            }
            else
            {
                _disk.Write(headerData, _header.CurrentLBA, true);
                _disk.Write(entriesData);
            }
            return true;
        }

        private void FinalizeLayout()
        {
            // write the protective MBR
            if (!WriteMBR())
                throw new Exception("Failed to write protective MBR");

            // updated shared values
            _header.NumberOfPartitionEntries = (uint)_fileSystems.Count;
            _header.FirstUsableLBA = _firstUsableSector;
            _header.LastUsableLBA = _lastUsableSector;

            // update the gpt header with some values for the primary copy
            _header.CurrentLBA = 1;
            _header.BackupLBA = _disk.SectorCount - 1;
            _header.PartitionEntryLBA = 2;
            if (!WriteGPT(false))
                throw new Exception("Failed to write primary GPT");

            // update the gpt header with some values for the secondary copy
            _header.CurrentLBA = _disk.SectorCount - 1;
            _header.BackupLBA = 1;
            _header.PartitionEntryLBA = _header.LastUsableLBA + 1;
            if (!WriteGPT(true))
                throw new Exception("Failed to write backup GPT");
        }

        public ulong GetFreeSectorCount()
        {
            if (_disk == null)
                return 0;
            return (_lastUsableSector - _firstUsableSector) - _allocatedSectors;
        }

        public IEnumerable<FileSystems.IFileSystem> GetFileSystems()
        {
            return _fileSystems;
        }
    }
}
