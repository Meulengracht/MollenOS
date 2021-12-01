
using System;
using System.Collections.Generic;

namespace OSBuilder.DiskLayouts
{
    public interface IDiskScheme : IDisposable
    {
        /**
         * Initializes a disk with the existing disk-scheme and gathers
         * information about the on-board filesystems
         */
        bool Open(IDisk disk);

        /**
         * Initializes a disk with the disk-scheme and formats the disk
         * for usage with <AddPartition>, this action wipes the disk
         */
        bool Create(IDisk disk);

        /**
         * Adds a new partition of the filesystem given, with the given size
         */
        bool AddPartition(FileSystems.IFileSystem fileSystem, ulong sectorCount);

        /** 
         * Retrieves the number of free sectors available for the next partition
         */
        ulong GetFreeSectorCount();

        /**
         * Retrieves a list of the partitions on the disk
         */
        IEnumerable<FileSystems.IFileSystem> GetFileSystems();
    }
}
