using System;

namespace OSBuilder.FileSystems
{
    public interface IFileSystem
    {
        /**
         * Initializes the filesystem instance at the given position on the given disk
         */
        void Initialize(IDisk disk, ulong startSector, ulong sectorCount);

        /**
         * Formats the partition with the filesystem - wipes all data from the partition
         */
        bool Format();

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
        Guid GetFileSystemTypeGuid();
        Guid GetFileSystemGuid();
        ulong GetSectorStart();
        ulong GetSectorCount();
        string GetName();
    }
}
