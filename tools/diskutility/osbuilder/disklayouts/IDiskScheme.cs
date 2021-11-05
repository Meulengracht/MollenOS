using System;

namespace OSBuilder.DiskLayouts
{
    public interface IDiskScheme
    {
        /**
         * Initializes a disk with the existing disk-scheme and gathers
         * information about the on-board filesystems
         */
        bool Open(Disk disk);

        /**
         * Initializes a disk with the disk-scheme and formats the disk
         * for usage with <AddPartition>, this action wipes the disk
         */
        bool Create(Disk disk);

        /**
         * Adds a new partition of the filesystem given, with the given size
         */
        bool AddPartition(FileSystems.IFileSystem fileSystem, ulong sectorCount);

        /**
         * Finalizes a disk opened or created by Open/Create
         */
        bool Finalize();

        /** 
         * Retrieves the number of free sectors available for the next partition
         */
        ulong GetFreeSectorCount();
    }
}