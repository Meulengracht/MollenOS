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
        // Variabes
        private CDisk m_pDisk;
        private UInt64 m_iSector;
        private UInt64 m_iSectorCount;
        private UInt16 m_iBucketSize;

        /* Allocate a bucket */
        UInt32 AllocateBucket(UInt32 FreeBucketIndex, UInt64 NumBuckets, out UInt32 InitialSize)
        {
            /* Calculate Bucket Map */
            UInt64 Buckets = mDisk.TotalSectors / mDisk.BucketSize;
            UInt64 BucketMapSize = Buckets * 8; /* One bucket descriptor is 8 bytes */
            UInt64 BucketMapSector = (mDisk.TotalSectors - ((BucketMapSize / mDisk.BytesPerSector) + 1));
            UInt32 BucketsPerSector = mDisk.BytesPerSector / 8;

            UInt64 Counter = NumBuckets;
            UInt32 BucketPtr = FreeBucketIndex;
            UInt32 BucketPrevPtr = 0;
            UInt32 FirstFreeSize = 0;
            while (Counter > 0)
            {
                /* Which sector is the next bucket in? */
                UInt32 FreeCount = 0;
                UInt32 SectorOffset = BucketPtr / BucketsPerSector;
                UInt32 SectorIndex = BucketPtr % BucketsPerSector;

                /* Read sector */
                Byte[] Sector = ReadDisk(mDisk, BucketMapSector + SectorOffset, 1);

                /* Done */
                BucketPrevPtr = BucketPtr;
                BucketPtr = BitConverter.ToUInt32(Sector, (int)(SectorIndex * 8));
                FreeCount = BitConverter.ToUInt32(Sector, (int)((SectorIndex * 8) + 4));

                /* How many buckets? */
                if (FreeCount > Counter)
                {
                    /* Calculate next free */
                    UInt32 NextFreeBucket = BucketPrevPtr + (UInt32)Counter;
                    UInt32 NextFreeCount = FreeCount - (UInt32)Counter;

                    if (FirstFreeSize == 0)
                        FirstFreeSize = (UInt32)Counter;

                    /* We have to adjust now, 
                     * since we are taking only a chunk
                     * of the available length */
                    Sector[SectorIndex * 8] = 0xFF;
                    Sector[SectorIndex * 8 + 1] = 0xFF;
                    Sector[SectorIndex * 8 + 2] = 0xFF;
                    Sector[SectorIndex * 8 + 3] = 0xFF;
                    Sector[SectorIndex * 8 + 4] = (Byte)(Counter & 0xFF);
                    Sector[SectorIndex * 8 + 5] = (Byte)((Counter >> 8) & 0xFF);
                    Sector[SectorIndex * 8 + 6] = (Byte)((Counter >> 16) & 0xFF);
                    Sector[SectorIndex * 8 + 7] = (Byte)((Counter >> 24) & 0xFF);

                    /* Write it back */
                    WriteDisk(mDisk, BucketMapSector + SectorOffset, Sector, true);

                    /* Setup new block */
                    SectorOffset = NextFreeBucket / BucketsPerSector;
                    SectorIndex = NextFreeBucket % BucketsPerSector;

                    /* Read */
                    Sector = ReadDisk(mDisk, BucketMapSector + SectorOffset, 1);

                    /* Modify */
                    Sector[SectorIndex * 8] = (Byte)(BucketPtr & 0xFF);
                    Sector[SectorIndex * 8 + 1] = (Byte)((BucketPtr >> 8) & 0xFF);
                    Sector[SectorIndex * 8 + 2] = (Byte)((BucketPtr >> 16) & 0xFF);
                    Sector[SectorIndex * 8 + 3] = (Byte)((BucketPtr >> 24) & 0xFF);
                    Sector[SectorIndex * 8 + 4] = (Byte)(NextFreeCount & 0xFF);
                    Sector[SectorIndex * 8 + 5] = (Byte)((NextFreeCount >> 8) & 0xFF);
                    Sector[SectorIndex * 8 + 6] = (Byte)((NextFreeCount >> 16) & 0xFF);
                    Sector[SectorIndex * 8 + 7] = (Byte)((NextFreeCount >> 24) & 0xFF);

                    /* Write it back */
                    WriteDisk(mDisk, BucketMapSector + SectorOffset, Sector, true);

                    /* Done */
                    InitialSize = FirstFreeSize;
                    return NextFreeBucket;
                }
                else
                {
                    /* We can just take the whole cake
                     * no need to modify it's length */
                    if (FirstFreeSize == 0)
                        FirstFreeSize = FreeCount;

                    /* Next */
                    Counter -= FreeCount;
                }
            }

            /* Update BucketPrevPtr to 0xFFFFFFFF */
            UInt32 _SecOff = BucketPrevPtr / BucketsPerSector;
            UInt32 _SecInd = BucketPrevPtr % BucketsPerSector;

            /* Read sector */
            Byte[] _Sec = ReadDisk(mDisk, BucketMapSector + _SecOff, 1);

            /* Modify Sector */
            _Sec[_SecInd * 8] = 0xFF;
            _Sec[_SecInd * 8 + 1] = 0xFF;
            _Sec[_SecInd * 8 + 2] = 0xFF;
            _Sec[_SecInd * 8 + 3] = 0xFF;

            /* Write it back */
            WriteDisk(mDisk, BucketMapSector + _SecOff, _Sec, true);

            /* Done */
            InitialSize = FirstFreeSize;
            return BucketPtr;
        }

        /* GetNextBucket
         * The next bucket is returned as result, and the length of the bucket is
         * given in the _out_ parameter */
        UInt32 GetNextBucket(UInt32 Bucket, out UInt32 BucketLength)
        {
            // Calculates the position of the bucket-map
            UInt64 Buckets = m_pDisk.TotalSectors / m_iBucketSize;
            UInt64 BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            UInt64 BucketMapSector = (m_pDisk.TotalSectors - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));

            // Calculate index into bucket map
            UInt32 BucketsPerSector = m_pDisk.BytesPerSector / 8;
            UInt32 SectorOffset = Bucket / BucketsPerSector;
            UInt32 SectorIndex = Bucket % BucketsPerSector;

            // Read the calculated sector
            Byte[] Sector = m_pDisk.Read(BucketMapSector + SectorOffset, 1);

            // Update length and return link
            BucketLength = BitConverter.ToUInt32(Sector, (int)((SectorIndex * 8) + 4));
            return BitConverter.ToUInt32(Sector, (int)(SectorIndex * 8));
        }

        /* SetNextBucket
         * Updates the link to the next bucket for the given bucket */
        void SetNextBucket(UInt32 Bucket, UInt32 NextBucket)
        {
            // Calculates the position of the bucket-map
            UInt64 Buckets = m_pDisk.TotalSectors / m_iBucketSize;
            UInt64 BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            UInt64 BucketMapSector = (m_pDisk.TotalSectors - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));

            // Calculate index into bucket map
            UInt32 BucketsPerSector = m_pDisk.BytesPerSector / 8;
            UInt32 SectorOffset = Bucket / BucketsPerSector;
            UInt32 SectorIndex = Bucket % BucketsPerSector;

            // Read the calculated sector
            Byte[] Sector = m_pDisk.Read(BucketMapSector + SectorOffset, 1);

            // Update link
            Sector[SectorIndex * 8] = (Byte)(NextBucket & 0xFF);
            Sector[SectorIndex * 8 + 1] = (Byte)((NextBucket >> 8) & 0xFF);
            Sector[SectorIndex * 8 + 2] = (Byte)((NextBucket >> 16) & 0xFF);
            Sector[SectorIndex * 8 + 3] = (Byte)((NextBucket >> 24) & 0xFF);

            // Flush buffer to disk
            m_pDisk.Write(Sector, BucketMapSector + SectorOffset, true);
        }

        /* Constructor
         * Zeroes out and initializes local members */
        public CMollenOSFileSystem()
        {
            // Initialize
            m_pDisk = null;
            m_iBucketSize = 0;
        }

        /* Initialize
         * Initializes the filesystem instance at the given position on the given disk */
        public bool Initialize(CDisk Disk, UInt64 StartSector, UInt64 SectorCount)
        {
            // Store variables
            m_pDisk = Disk;
            m_iSectorCount = SectorCount;
            m_iSector = StartSector;

            // Done
            return true;
        }

        /* Format
         * Formats the partition with the filesystem - wipes all data from the partition */
        public bool Format()
        {
            // Sanitize variables
            if (m_pDisk == null)
                return false;

            // Debug
            Console.WriteLine("Format - Calculating metrics");

            // Variables
            UInt64 DriveSizeBytes = m_iSectorCount * m_pDisk.BytesPerSector;
            UInt64 GigaByte = (1024 * 1024 * 1024);
            UInt32 BucketSize = 0;
            UInt64 Buckets = 0;
            UInt64 BucketMapSize = 0;
            UInt64 BucketMapSector = 0;
            UInt64 ReservedBuckets = 0;
            UInt64 MirrorMasterBucketSector = 0;

            // Determine bucket size 
            // if <1gb = 2 Kb (4 sectors) 
            // If <64gb = 4 Kb (8 sectors)
            // If >64gb = 8 Kb (16 sectors)
            // If >256gb = 16 Kb (32 sectors)
            if (DriveSizeBytes >= (256 * GigaByte))
                BucketSize = 32;
            else if (DriveSizeBytes >= (64 * GigaByte))
                BucketSize = 16;
            else if (DriveSizeBytes <= GigaByte)
                BucketSize = 4;
            else
                BucketSize = 8;

            // Save bucket size and debug
            m_iBucketSize = (UInt16)BucketSize;
            Console.WriteLine("Format - Bucket Size: " + BucketSize.ToString());

            // Sanitize that bootloaders are present
            if (!File.Exists("ssbl.stm") || !File.Exists("stage1.bin"))
            {
                Console.WriteLine("Format - Bootloaders are missing");
                return false;
            }

            // We need to calculate size of bootloader
            FileStream nLoader = new FileStream("ssbl.stm", FileMode.Open);
            Byte[] Stage2Data = new Byte[nLoader.Length];
            nLoader.Read(Stage2Data, 0, (int)nLoader.Length);
            nLoader.Close();

            // Calculate reserved sector count
            Int64 ReservedSectors = 1 + ((Stage2Data.Length / m_pDisk.BytesPerSector) + 1);

            // Debug
            Console.WriteLine("Format - Reserved Sectors: " + ReservedSectors.ToString());

            // Setup Bucket-list
            // SectorCount / BucketSize
            // Each bucket must point to the next, untill we reach the end of buckets
            // Position at end of drive
            Buckets = m_pDisk.TotalSectors / BucketSize;
            BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            BucketMapSector = (m_pDisk.TotalSectors - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));
            MirrorMasterBucketSector = BucketMapSector - 1;

            // Reserve an additional for the mirror master-record
            ReservedBuckets = (((BucketMapSize / m_pDisk.BytesPerSector) + 1) / BucketSize) + 1;

            // Debug
            Console.WriteLine("Format - Bucket Count: " + Buckets.ToString());
            Console.WriteLine("Format - Bucket Map Size: " + BucketMapSize.ToString());
            Console.WriteLine("Format - Bucket Map Sector: " + BucketMapSector.ToString());
            Console.WriteLine("Format - Reserved Buckets: " + ReservedBuckets.ToString());
            Console.WriteLine("Format - Free BucketCount: " + ((Buckets - ReservedBuckets) - 1).ToString());

            // Debug
            Console.WriteLine("Format - Building map");

            // Seek to start of map
            Int64 ValToMove = (Int64)BucketMapSector * m_pDisk.BytesPerSector;
            m_pDisk.Seek(ValToMove);
            Byte[] SectorBuffer = new Byte[m_pDisk.BytesPerSector];
            UInt64 Iterator = 0;
            UInt64 Max = Buckets;
            UInt64 FreeCount = (Buckets - ReservedBuckets) - 1;

            // A map entry consists of the length of the bucket, and it's link
            // To get the length of the link, you must lookup it's length by accessing Map[Link]
            // Length of bucket 0 is HIDWORD(Map[0]), Link of bucket 0 is LODWORD(Map[0])
            // If the link equals 0xFFFFFFFF there is no link

            // Ok, iterate through all sectors in map and initialize them
            // The first X map-records are system-reserved and mapped as (0x1 | END)
            // The rest of the map-records are free and mapped as ((FreeCount-N) | END)
            while (Iterator < Max) {
                // Each record is 8 bytes
                for (UInt64 i = 0; i < (m_pDisk.BytesPerSector / 8); i++) {
                    if (Iterator >= ((Max - ReservedBuckets) - 1)) {
                        SectorBuffer[(i * 8)] = 0xFF;
                        SectorBuffer[(i * 8) + 1] = 0xFF;
                        SectorBuffer[(i * 8) + 2] = 0xFF;
                        SectorBuffer[(i * 8) + 3] = 0xFF;
                        SectorBuffer[(i * 8) + 4] = 0x01;
                        SectorBuffer[(i * 8) + 5] = 0x00;
                        SectorBuffer[(i * 8) + 6] = 0x00;
                        SectorBuffer[(i * 8) + 7] = 0x00;
                    }
                    else {
                        SectorBuffer[(i * 8)] = 0xFF;
                        SectorBuffer[(i * 8) + 1] = 0xFF;
                        SectorBuffer[(i * 8) + 2] = 0xFF;
                        SectorBuffer[(i * 8) + 3] = 0xFF;
                        SectorBuffer[(i * 8) + 4] = (Byte)((FreeCount) & 0xFF);
                        SectorBuffer[(i * 8) + 5] = (Byte)(((FreeCount) >> 8) & 0xFF);
                        SectorBuffer[(i * 8) + 6] = (Byte)(((FreeCount) >> 16) & 0xFF);
                        SectorBuffer[(i * 8) + 7] = (Byte)(((FreeCount) >> 24) & 0xFF);

                        // Decrease the number of free-count
                        FreeCount--;
                    }

                    // Go to next entry
                    Iterator++;
                }

                // Flush the sector to disk
                m_pDisk.Write(SectorBuffer);
            }

            // Calculate how many start-buckets we shall skip because of 
            // reserved sectors
            Int64 SkipBuckets = (ReservedSectors / BucketSize) + 2;
            Int64 MasterBucketSector = ReservedSectors + 1;

            // Debug
            Console.WriteLine("Format - Creating master-records");
            Console.WriteLine("Format - Original: " + MasterBucketSector.ToString());
            Console.WriteLine("Format - Mirror: " + MirrorMasterBucketSector.ToString());

            /* Setup the MasterBucket structure */
            UInt32 BucketStartFree = (UInt32)((MasterBucketSector / BucketSize) + 1);

            Console.WriteLine("Format - First Free Bucket: " + BucketStartFree.ToString());
            UInt32 InitialBucketSize = 0;
            UInt32 RootIndex = BucketStartFree;
            BucketStartFree = AllocateBucket(mDisk, BucketStartFree, 1, out InitialBucketSize);
            UInt32 BadBucketIndex = BucketStartFree;
            BucketStartFree = AllocateBucket(mDisk, BucketStartFree, 1, out InitialBucketSize);
            UInt32 JournalIndex = BucketStartFree;
            BucketStartFree = AllocateBucket(mDisk, BucketStartFree, 8, out InitialBucketSize);
            Console.WriteLine("First Free Bucket after initial: " + BucketStartFree.ToString());

            /* Write it to the two sectors */
            Byte[] MasterBucket = new Byte[512];

            /* Zero It */
            for (int i = 0; i < 512; i++)
                MasterBucket[i] = 0;

            /* Setup Magic */
            MasterBucket[0] = 0x4D;
            MasterBucket[1] = 0x46;
            MasterBucket[2] = 0x53;
            MasterBucket[3] = 0x31;

            /* Setup flags */
            MasterBucket[4] = 0;
            MasterBucket[5] = 0;
            MasterBucket[6] = 0;
            MasterBucket[7] = 0;

            /* Setup free bucket pointer */
            MasterBucket[8] = (Byte)(BucketStartFree & 0xFF);
            MasterBucket[9] = (Byte)((BucketStartFree >> 8) & 0xFF);
            MasterBucket[10] = (Byte)((BucketStartFree >> 16) & 0xFF);
            MasterBucket[11] = (Byte)((BucketStartFree >> 24) & 0xFF);

            /* Setup Root Index */
            MasterBucket[12] = (Byte)(RootIndex & 0xFF);
            MasterBucket[13] = (Byte)((RootIndex >> 8) & 0xFF);
            MasterBucket[14] = (Byte)((RootIndex >> 16) & 0xFF);
            MasterBucket[15] = (Byte)((RootIndex >> 24) & 0xFF);

            /* Setup Bad Bucket Index */
            MasterBucket[16] = (Byte)(BadBucketIndex & 0xFF);
            MasterBucket[17] = (Byte)((BadBucketIndex >> 8) & 0xFF);
            MasterBucket[18] = (Byte)((BadBucketIndex >> 16) & 0xFF);
            MasterBucket[19] = (Byte)((BadBucketIndex >> 24) & 0xFF);

            /* Setup Journal File Index */
            MasterBucket[20] = (Byte)(JournalIndex & 0xFF);
            MasterBucket[21] = (Byte)((JournalIndex >> 8) & 0xFF);
            MasterBucket[22] = (Byte)((JournalIndex >> 16) & 0xFF);
            MasterBucket[23] = (Byte)((JournalIndex >> 24) & 0xFF);

            /* Setup bucketmap sector */
            MasterBucket[24] = (Byte)(BucketMapSector & 0xFF);
            MasterBucket[25] = (Byte)((BucketMapSector >> 8) & 0xFF);
            MasterBucket[26] = (Byte)((BucketMapSector >> 16) & 0xFF);
            MasterBucket[27] = (Byte)((BucketMapSector >> 24) & 0xFF);
            MasterBucket[28] = (Byte)((BucketMapSector >> 32) & 0xFF);
            MasterBucket[29] = (Byte)((BucketMapSector >> 40) & 0xFF);
            MasterBucket[30] = (Byte)((BucketMapSector >> 48) & 0xFF);
            MasterBucket[31] = (Byte)((BucketMapSector >> 56) & 0xFF);

            /* Setup bucketmap size */
            MasterBucket[32] = (Byte)(BucketMapSize & 0xFF);
            MasterBucket[33] = (Byte)((BucketMapSize >> 8) & 0xFF);
            MasterBucket[34] = (Byte)((BucketMapSize >> 16) & 0xFF);
            MasterBucket[35] = (Byte)((BucketMapSize >> 24) & 0xFF);
            MasterBucket[36] = (Byte)((BucketMapSize >> 32) & 0xFF);
            MasterBucket[37] = (Byte)((BucketMapSize >> 40) & 0xFF);
            MasterBucket[38] = (Byte)((BucketMapSize >> 48) & 0xFF);
            MasterBucket[39] = (Byte)((BucketMapSize >> 56) & 0xFF);

            /* Seek */
            WriteDisk(mDisk, MirrorMasterBucketSector, MasterBucket, true);
            WriteDisk(mDisk, (UInt64)MasterBucketSector, MasterBucket, true);

            /* Wipe new sectors */
            Byte[] Wipe = new Byte[BucketSize * mDisk.BytesPerSector];
            for (int i = 0; i < Wipe.Length; i++)
                Wipe[i] = 0;

            Console.WriteLine("Wiping Root");
            WriteDisk(mDisk, (RootIndex * BucketSize), Wipe, true);

            Console.WriteLine("Wiping Bad Bucket List");
            WriteDisk(mDisk, (BadBucketIndex * BucketSize), Wipe, true);

            Wipe = new Byte[(BucketSize * mDisk.BytesPerSector) * 8];
            for (int i = 0; i < Wipe.Length; i++)
                Wipe[i] = 0;

            Console.WriteLine("Wiping Journal");
            WriteDisk(mDisk, (JournalIndex * BucketSize), Wipe, true);

            /* Step 3 */
            Console.WriteLine("Writing Mbr");

            /* Load the mbr, modify it */
            Byte[] Mbr = new Byte[mDisk.BytesPerSector];
            for (int c = 0; c < mDisk.BytesPerSector; c++)
                Mbr[c] = 0;

            /* Set magic */
            Mbr[3] = 0x4D;
            Mbr[4] = 0x46;
            Mbr[5] = 0x53;
            Mbr[6] = 0x31;

            /* Set version */
            Mbr[7] = 0x1;

            /* Set flags */
            Mbr[8] = 0;

            /* Disk metrics */
            Mbr[9] = 0x80;

            Mbr[10] = (Byte)(mDisk.BytesPerSector & 0xFF);
            Mbr[11] = (Byte)((mDisk.BytesPerSector >> 8) & 0xFF);

            /* Sectors Per Track */
            Mbr[12] = (Byte)(mDisk.SectorsPerTrack & 0xFF);
            Mbr[13] = (Byte)((mDisk.SectorsPerTrack >> 8) & 0xFF);

            /* Heads Per Cylinder */
            Mbr[14] = (Byte)(mDisk.TracksPerCylinder & 0xFF);
            Mbr[15] = (Byte)((mDisk.TracksPerCylinder >> 8) & 0xFF);

            Mbr[16] = (Byte)(mDisk.TotalSectors & 0xFF);
            Mbr[17] = (Byte)((mDisk.TotalSectors >> 8) & 0xFF);
            Mbr[18] = (Byte)((mDisk.TotalSectors >> 16) & 0xFF);
            Mbr[19] = (Byte)((mDisk.TotalSectors >> 24) & 0xFF);
            Mbr[20] = (Byte)((mDisk.TotalSectors >> 32) & 0xFF);
            Mbr[21] = (Byte)((mDisk.TotalSectors >> 40) & 0xFF);
            Mbr[22] = (Byte)((mDisk.TotalSectors >> 48) & 0xFF);
            Mbr[23] = (Byte)((mDisk.TotalSectors >> 56) & 0xFF);

            Mbr[24] = (Byte)(ReservedSectors & 0xFF);
            Mbr[25] = (Byte)((ReservedSectors >> 8) & 0xFF);

            Mbr[26] = (Byte)(BucketSize & 0xFF);
            Mbr[27] = (Byte)((BucketSize >> 8) & 0xFF);

            Mbr[28] = (Byte)(MasterBucketSector & 0xFF);
            Mbr[29] = (Byte)((MasterBucketSector >> 8) & 0xFF);
            Mbr[30] = (Byte)((MasterBucketSector >> 16) & 0xFF);
            Mbr[31] = (Byte)((MasterBucketSector >> 24) & 0xFF);
            Mbr[32] = (Byte)((MasterBucketSector >> 32) & 0xFF);
            Mbr[33] = (Byte)((MasterBucketSector >> 40) & 0xFF);
            Mbr[34] = (Byte)((MasterBucketSector >> 48) & 0xFF);
            Mbr[35] = (Byte)((MasterBucketSector >> 56) & 0xFF);

            Mbr[36] = (Byte)(MirrorMasterBucketSector & 0xFF);
            Mbr[37] = (Byte)((MirrorMasterBucketSector >> 8) & 0xFF);
            Mbr[38] = (Byte)((MirrorMasterBucketSector >> 16) & 0xFF);
            Mbr[39] = (Byte)((MirrorMasterBucketSector >> 24) & 0xFF);
            Mbr[40] = (Byte)((MirrorMasterBucketSector >> 32) & 0xFF);
            Mbr[41] = (Byte)((MirrorMasterBucketSector >> 40) & 0xFF);
            Mbr[42] = (Byte)((MirrorMasterBucketSector >> 48) & 0xFF);
            Mbr[43] = (Byte)((MirrorMasterBucketSector >> 56) & 0xFF);

            /* Set volume name */
            Mbr[44] = (Byte)'M';
            Mbr[45] = (Byte)'o';
            Mbr[46] = (Byte)'l';
            Mbr[47] = (Byte)'l';
            Mbr[48] = (Byte)'e';
            Mbr[49] = (Byte)'n';
            Mbr[50] = (Byte)'O';
            Mbr[51] = (Byte)'S';

            /* Write it */
            m_pDisk.Write(Mbr, 0, true);
        }

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        public bool WriteFile(String Path, FileFlags Flags, Byte[] Data)
        {

        }
    }
}
