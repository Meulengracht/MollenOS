using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OSBuilder.DiskLayouts
{
    public class MBR : IDiskScheme
    {
        static readonly int MAX_PARTITONS = 4;

        private IDisk _disk = null;
        private List<FileSystems.IFileSystem> _fileSystems = new List<FileSystems.IFileSystem>();
        private ulong _sectorsAllocated = 1; // MBR

        public bool Open(IDisk disk)
        {
            // Store disk
            _disk = disk;

            // ensure disk is open for read/write
            if (!_disk.IsOpen())
                return false;
            
            ParseMBR();
            return true;
        }

        /**
         * Initializes a disk with the disk-scheme and formats the disk
         * for usage with <AddPartition>, this action wipes the disk
         */
        public bool Create(IDisk disk)
        {
            _disk = disk;

            if (!File.Exists("deploy/mbr.sys"))
            {
                Console.WriteLine("Format - MBR scheme requested but mbr.sys is not found (deploy/mbr.sys)");
                return false;
            }

            // ensure disk is open for read/write
            if (!_disk.IsOpen())
                return false;

            return true;
        }

        public void Dispose()
        {
            if (_disk == null)
                return;
            
            // finalize the MBR layout
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

        private void SetPartitionInMBR(byte[] mbr, int partition, FileSystems.IFileSystem fileSystem)
        {
            var byteOffset = 446 + (partition * 16);
            ulong partitionStart = fileSystem.GetSectorStart();
            ulong partitionEnd = fileSystem.GetSectorStart() + fileSystem.GetSectorCount();
            Console.WriteLine("partition " + partition.ToString() + 
                " - start: " + partitionStart.ToString() +
                ", end: " + partitionEnd.ToString());

            // determine data to write
            byte status = (byte)(fileSystem.IsBootable() ? 0x80 : 0x00);
            var sectorsPerTrack = _disk.Geometry.SectorsPerTrack;

            ulong headOfStart = (partitionStart / sectorsPerTrack) % 16;
            ulong headOfEnd = (partitionEnd / sectorsPerTrack) % 16;

            ushort cylinderOfStart = Math.Min((ushort)(partitionStart / (sectorsPerTrack * 16)), (ushort)1023);
            ushort cylinderOfEnd = Math.Min((ushort)(partitionEnd / (sectorsPerTrack * 16)), (ushort)1023);

            ulong sectorInCylinderStart = (fileSystem.GetSectorStart() % sectorsPerTrack) + 1;
            ulong sectorInCylinderEnd = (fileSystem.GetSectorStart() % sectorsPerTrack) + 1;
            Console.WriteLine("partiton CHS start - " + cylinderOfStart.ToString() +
                "/" + headOfStart.ToString() + "/" + sectorInCylinderStart.ToString());
            Console.WriteLine("partiton CHS end   - " + cylinderOfEnd.ToString() +
                "/" + headOfEnd.ToString() + "/" + sectorInCylinderEnd.ToString());

            uint sectorOfStart = fileSystem.GetSectorStart() > uint.MaxValue ? uint.MaxValue : (uint)fileSystem.GetSectorStart();
            uint sectorCount = fileSystem.GetSectorCount() > uint.MaxValue ? uint.MaxValue : (uint)fileSystem.GetSectorCount();

            // Set partition status
            mbr[byteOffset] = status;

            // Set partiton start (CHS), high byte is low byte of cylinder
            mbr[byteOffset + 1] = (byte)headOfStart;
            mbr[byteOffset + 2] = (byte)((byte)((cylinderOfStart >> 2) & 0xC0) | (byte)(sectorInCylinderStart & 0x3F));
            mbr[byteOffset + 3] = (byte)(cylinderOfStart & 0xFF);

            // Set partition type
            mbr[byteOffset + 4] = fileSystem.GetFileSystemType();

            // Set partition end (CHS), high byte is low byte of cylinder
            mbr[byteOffset + 5] = (byte)headOfEnd;
            mbr[byteOffset + 6] = (byte)((byte)((cylinderOfEnd >> 2) & 0xC0) | (byte)(sectorInCylinderEnd & 0x3F));
            mbr[byteOffset + 7] = (byte)(cylinderOfEnd & 0xFF);

            // Set partition start (LBA)
            mbr[byteOffset + 8] = (byte)(sectorOfStart & 0xFF);
            mbr[byteOffset + 9] = (byte)((sectorOfStart >> 8) & 0xFF);
            mbr[byteOffset + 10] = (byte)((sectorOfStart >> 16) & 0xFF);
            mbr[byteOffset + 11] = (byte)((sectorOfStart >> 24) & 0xFF);

            // Set partition size (LBA)
            mbr[byteOffset + 12] = (byte)(sectorCount & 0xFF);
            mbr[byteOffset + 13] = (byte)((sectorCount >> 8) & 0xFF);
            mbr[byteOffset + 14] = (byte)((sectorCount >> 16) & 0xFF);
            mbr[byteOffset + 15] = (byte)((sectorCount >> 24) & 0xFF);
        }

        private void SetPartitonEntryEmptyInMBR(byte[] mbr, int partition)
        {
            mbr[446 + (partition * 16)] = 0x00;
            mbr[446 + (partition * 16) + 4] = 0x00;
        }

        private void FinalizeLayout()
        {
            // Load up mbr and build the partition table
            Console.WriteLine("Finalize - loading mbr (mbr.sys)");
            byte[] mbr = File.ReadAllBytes("deploy/mbr.sys");
            
            // Install partitions in the MBR
            for (int i = 0; i < 4; i++)
            {
                if (i < _fileSystems.Count)
                {
                    SetPartitionInMBR(mbr, i, _fileSystems[i]);
                }
                else
                {
                    SetPartitonEntryEmptyInMBR(mbr, i);
                }
            }

            _disk.Write(mbr, 0, true);
        }

        /**
         * Adds a new partition of the filesystem given, with the given size
         */
        public bool AddPartition(FileSystems.IFileSystem fileSystem, ulong sectorCount)
        {
            ulong partitionSize = sectorCount;
            if (_disk == null || fileSystem == null || _fileSystems.Count == MAX_PARTITONS)
                return false;

            if (GetFreeSectorCount() < partitionSize)
            {
                partitionSize = GetFreeSectorCount();
                Console.WriteLine("AddPartition - not enough space, reducing partition size to " + partitionSize + " sectors");
            }

            // Initialize the file-system
            fileSystem.Initialize(_disk, _sectorsAllocated, partitionSize);
            
            // Add sectors allocated
            _fileSystems.Add(fileSystem);
            _sectorsAllocated += partitionSize;
            return fileSystem.Format();
        }
        
        public ulong GetFreeSectorCount()
        {
            if (_disk == null)
                return 0;
            return _disk.SectorCount - _sectorsAllocated;
        }

        public IEnumerable<FileSystems.IFileSystem> GetFileSystems()
        {
            return _fileSystems;
        }

        public void ParseMBR()
        {
            var initialSet = false;
            byte[] mbr = _disk.Read(0, 1);
            for (int i = 0; i < MAX_PARTITONS; i++)
            {
                byte status = mbr[446 + (i * 16)];
                byte type = mbr[446 + (i * 16) + 4];
                if (status == 0x00 && type == 0x00)
                    break; // reached end of table
                
                uint start = (uint)(mbr[446 + (i * 16) + 8] |
                    (mbr[446 + (i * 16) + 9] << 8) |
                    (mbr[446 + (i * 16) + 10] << 16) |
                    (mbr[446 + (i * 16) + 11] << 24));
                uint size = (uint)(mbr[446 + (i * 16) + 12] |
                    (mbr[446 + (i * 16) + 13] << 8) |
                    (mbr[446 + (i * 16) + 14] << 16) |
                    (mbr[446 + (i * 16) + 15] << 24));

                // Create supported filesystems
                if (type == FileSystems.MFS.FileSystem.TYPE)
                    _fileSystems.Add(new FileSystems.MFS.FileSystem(_disk, (ulong)start, (ulong)size));
                
                // Reserve the sectors
                if (!initialSet)
                {
                    _sectorsAllocated = start;
                    initialSet = true;
                }
                _sectorsAllocated += size;
            }
        }
    }
}
