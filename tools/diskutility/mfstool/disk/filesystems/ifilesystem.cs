using System;

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

        /* MakeBoot
         * Readies the filesystem for being the primary bootable filesystem by preparing a bootsector */
        bool MakeBoot();

        /* ListDirectory
         * List's the contents of the given path - that must be a directory path */
        bool ListDirectory(String Path);

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        bool WriteFile(String LocalPath, FileFlags Flags, Byte[] Data);
    }
}
