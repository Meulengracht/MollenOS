using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace DiskUtility
{
    public interface IDiskScheme
    {
        /* Open
         * Initializes a disk with the existing disk-scheme and gathers
         * information about the on-board filesystems */
        bool Open(CDisk Disk);

        /* Create
         * Initializes a disk with the disk-scheme and formats the disk
         * for usage with <AddPartition>, this action wipes the disk */
        bool Create(CDisk Disk, Boolean SinglePartition = true);

        /* AddPartition 
         * Adds a new partition of the filesystem given, with the given size */
        bool AddPartition(IFileSystem FileSystem, UInt64 SectorCount);

        /* GetFreeSectorCount 
         * Retrieves the number of free sectors available for the next partition */
        UInt64 GetFreeSectorCount();
    }
}
