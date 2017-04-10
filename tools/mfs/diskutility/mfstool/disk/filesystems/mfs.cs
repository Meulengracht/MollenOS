using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace DiskUtility
{
    public class CMollenOSFileSystem : IFileSystem
    {
        /* Initialize
         * Initializes the filesystem instance at the given position on the given disk */
        public bool Initialize(CDisk Disk, UInt64 StartSector, UInt64 SectorCount)
        {

        }

        /* Format
         * Formats the partition with the filesystem - wipes all data from the partition */
        public bool Format()
        {

        }

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        public bool WriteFile(String Path, FileFlags Flags, Byte[] Data)
        {

        }
    }
}
