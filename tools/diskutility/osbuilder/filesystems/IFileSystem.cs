using System;

namespace OSBuilder.FileSystems
{
    public interface IFileSystem
    {
        /**
         * Initializes the filesystem instance at the given position on the given disk
         */
        bool Initialize(Disk disk, ulong startSector, ulong sectorCount);

        /**
         * Formats the partition with the filesystem - wipes all data from the partition
         */
        bool Format();

        /**
         * Readies the filesystem for being the primary bootable filesystem by preparing a bootsector
         */
        void MakeBoot();

        /**
         * List's the contents of the given path - that must be a directory path
         */
        bool ListDirectory(string path);

        /** 
         * Creates a new file or directory with the given path, flags and data
         */
        bool WriteFile(string localPath, FileFlags flags, byte[] buffer);

        bool IsBootable();
        byte GetFileSystemType();
        Guid GetFileSystemGuid();
        ulong GetSectorStart();
        ulong GetSectorCount();
    }
}
