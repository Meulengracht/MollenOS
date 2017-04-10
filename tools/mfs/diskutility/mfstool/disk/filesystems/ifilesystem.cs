using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace DiskUtility
{
    [Flags]
    public enum FileFlags
    {
        Directory = 0x1,
        System = 0x2
    };

    public interface IFileSystem
    {
        /* Initialize
         * Initializes the filesystem instance at the given position on the given disk */
        bool Initialize(CDisk Disk, UInt64 StartSector, UInt64 SectorCount);

        /* Format
         * Formats the partition with the filesystem - wipes all data from the partition */
        bool Format();

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        bool WriteFile(String Path, FileFlags Flags, Byte[] Data);
    }
}
