using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace DiskUtility
{
    public class SchemeMBR : IDiskScheme
    {
        // Variables
        private UInt64 m_iSectorsAllocated;
        private Boolean m_bSinglePartition;
        private CDisk m_pDisk;

        /* Constructor 
         * Initializes local members */
        public SchemeMBR()
        {
            m_iSectorsAllocated = 0;
            m_pDisk = null;
        }

        /* Open
         * Initializes a disk with the existing disk-scheme and gathers
         * information about the on-board filesystems */
        public bool Open(CDisk Disk)
        {
            // Store disk
            m_pDisk = Disk;

            return false;
        }

        /* Create
         * Initializes a disk with the disk-scheme and formats the disk
         * for usage with <AddPartition>, this action wipes the disk */
        public bool Create(CDisk Disk, Boolean SinglePartition = true)
        {
            // Store variables
            m_bSinglePartition = SinglePartition;
            m_pDisk = Disk;

            // Open disn
            m_pDisk.Open();

            // Prepare boot-sector if it's a multi-partition layout
            if (!m_bSinglePartition)
            {
                // no support
                return false;
            }
            else
                return true;
        }

        /* Finalize
         * Finalizes a disk opened or created by Open/Create */
        public bool Finalize()
        {
            // Finalize all filesystems and dispose them

            // Close disk
            m_pDisk.Close();

            // Done
            return true;
        }

        /* AddPartition 
         * Adds a new partition of the filesystem given, with the given size */
        public bool AddPartition(IFileSystem FileSystem, UInt64 SectorCount)
        {
            // Sanitize
            if (m_pDisk == null)
                return false;

            // Make sure there is enough space
            if (GetFreeSectorCount() < SectorCount)
                return false;

            // Write to the partition table
            if (!m_bSinglePartition)
            {
                // Not supported
                return false;
            }

            // Initialize the file-system
            FileSystem.Initialize(m_pDisk, m_iSectorsAllocated, SectorCount);

            // Add sectors allocated
            m_iSectorsAllocated += SectorCount;

            // Format the disk partition
            return FileSystem.Format();
        }

        /* GetFreeSectorCount 
         * Retrieves the number of free sectors available for the next partition */
        public UInt64 GetFreeSectorCount()
        {
            // Sanitize
            if (m_pDisk == null)
                return 0;

            // Calculate
            return m_pDisk.TotalSectors - m_iSectorsAllocated;
        }
    }
}
