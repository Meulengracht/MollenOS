using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace DiskUtility
{
    class Program
    {
        public enum MfsEntryFlags
        {
            MFS_FILE = 0x1,
            MFS_SECURITY = 0x2,
            MFS_DIRECTORY = 0x4,
            MFS_SYSTEM = 0x8,
            MFS_HIDDEN = 0x10,
            MFS_LINK = 0x20
        }
        
        /* Allocate a bucket */
        static UInt32 AllocateBucket(CDisk mDisk, UInt32 FreeBucketIndex, UInt64 NumBuckets, out UInt32 InitialSize)
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

        /* Locate next bucket */
        static UInt32 GetNextBucket(MfsDisk mDisk, UInt32 Bucket, out UInt32 BucketLength)
        {
            /* Calculate Bucket Map */
            UInt64 Buckets = mDisk.TotalSectors / mDisk.BucketSize;
            UInt64 BucketMapSize = Buckets * 8; /* One bucket descriptor is 8 bytes */
            UInt64 BucketMapSector = (mDisk.TotalSectors - ((BucketMapSize / mDisk.BytesPerSector) + 1));

            /* Calculate Index */
            UInt32 BucketsPerSector = mDisk.BytesPerSector / 8;
            UInt32 SectorOffset = Bucket / BucketsPerSector;
            UInt32 SectorIndex = Bucket % BucketsPerSector;

            /* Read sector */
            Byte[] Sector = ReadDisk(mDisk, BucketMapSector + SectorOffset, 1);

            /* Done */
            BucketLength = BitConverter.ToUInt32(Sector, (int)((SectorIndex * 8) + 4));
            return BitConverter.ToUInt32(Sector, (int)(SectorIndex * 8));
        }

        /* Update bucket ptr */
        static void SetNextBucket(MfsDisk mDisk, UInt32 Bucket, UInt32 NextBucket)
        {
            /* Calculate Bucket Map */
            UInt64 Buckets = mDisk.TotalSectors / mDisk.BucketSize;
            UInt64 BucketMapSize = Buckets * 8; /* One bucket descriptor is 8 bytes */
            UInt64 BucketMapSector = (mDisk.TotalSectors - ((BucketMapSize / mDisk.BytesPerSector) + 1));

            /* Calculate Index */
            UInt32 BucketsPerSector = mDisk.BytesPerSector / 8;
            UInt32 SectorOffset = Bucket / BucketsPerSector;
            UInt32 SectorIndex = Bucket % BucketsPerSector;

            /* Read sector */
            Byte[] Sector = ReadDisk(mDisk, BucketMapSector + SectorOffset, 1);

            /* Edit */
            Sector[SectorIndex * 8] = (Byte)(NextBucket & 0xFF);
            Sector[SectorIndex * 8 + 1] = (Byte)((NextBucket >> 8) & 0xFF);
            Sector[SectorIndex * 8 + 2] = (Byte)((NextBucket >> 16) & 0xFF);
            Sector[SectorIndex * 8 + 3] = (Byte)((NextBucket >> 24) & 0xFF);

            /* Write */
            WriteDisk(mDisk, BucketMapSector + SectorOffset, Sector, true);
        }

        /* Format a drive with mfs */
        static void Format(MfsDisk mDisk)
        {
            /* Open Disk */
            if (!OpenDisk(mDisk))
            {
                Console.WriteLine("Failed to open drive");
                return;
            }

            /* Print */
            Console.WriteLine("Calculating metrics");

            /* Calc bytes */
            UInt64 DriveSizeBytes = mDisk.TotalSectors * mDisk.BytesPerSector;
            UInt64 GigaByte = (1024 * 1024 * 1024);
            UInt32 BucketSize = 0;
            UInt64 Buckets = 0;
            UInt64 BucketMapSize = 0;
            UInt64 BucketMapSector = 0;
            UInt64 ReservedBuckets = 0;
            UInt64 MirrorMasterBucketSector = 0;

            /* Determine bucket size 
             * if <1gb = 2 Kb (4 sectors) 
             * If <64gb = 4 Kb (8 sectors)
             * If >64gb = 8 Kb (16 sectors)
             * If >256gb = 16 Kb (32 sectors) */
            if (DriveSizeBytes >= (256 * GigaByte))
                BucketSize = 32;
            else if (DriveSizeBytes >= (64 * GigaByte))
                BucketSize = 16;
            else if (DriveSizeBytes <= GigaByte)
                BucketSize = 4;
            else
                BucketSize = 8;

            /* Print */
            mDisk.BucketSize = (UInt16)BucketSize;
            Console.WriteLine("Bucket Size: " + BucketSize.ToString());

            /* Get size of stage2-loader */
            FileStream nLoader = new FileStream("ssbl.stm", FileMode.Open);
            Byte[] Stage2Data = new Byte[nLoader.Length];
            nLoader.Read(Stage2Data, 0, (int)nLoader.Length);
            nLoader.Close();

            /* Calculate reserved sector count */
            Int64 ReservedSectors = 1 + ((Stage2Data.Length / mDisk.BytesPerSector) + 1);

            /* Print */
            Console.WriteLine("Reserved Sectors: " + ReservedSectors.ToString());

            /* Setup Bucket-list
             * SectorCount / BucketSize
             * Each bucket must point to the next, 
             * untill we reach the end of buckets
             * Position at end of drive */
            Buckets = mDisk.TotalSectors / BucketSize;
            BucketMapSize = Buckets * 8; /* One bucket descriptor is 8 bytes */
            BucketMapSector = (mDisk.TotalSectors - ((BucketMapSize / mDisk.BytesPerSector) + 1));
            MirrorMasterBucketSector = BucketMapSector - 1;

            /* Reserve an additional for the mirror bucket */
            ReservedBuckets = (((BucketMapSize / mDisk.BytesPerSector) + 1) / BucketSize) + 1;

            /* Print */
            Console.WriteLine("Bucket Count: " + Buckets.ToString());
            Console.WriteLine("Bucket Map Size: " + BucketMapSize.ToString());
            Console.WriteLine("Bucket Map Sector: " + BucketMapSector.ToString());
            Console.WriteLine("Reserved Buckets: " + ReservedBuckets.ToString());
            Console.WriteLine("Free BucketCount: " + ((Buckets - ReservedBuckets) - 1).ToString());

            /* Step 1 */
            Console.WriteLine("Writing BucketTable");

            /* Seek to sector BucketMapSector */
            Int64 ValToMove = (Int64)BucketMapSector * mDisk.BytesPerSector;
            SeekDisk(mDisk, ValToMove);
            Byte[] SectorBuffer = new Byte[mDisk.BytesPerSector];
            UInt64 Iterator = 0;
            UInt64 Max = Buckets;
            UInt64 FreeCount = (Buckets - ReservedBuckets) - 1;
            UInt32 FreeBlockItr = 0x7FFFFFFF;

            while (Iterator < Max)
            {
                /* Build sector buffer */
                for (UInt64 i = 0; i < (mDisk.BytesPerSector / 8); i++)
                {
                    /* Sanity */
                    if (Iterator >= ((Max - ReservedBuckets) - 1))
                    {
                        SectorBuffer[(i * 8)] = 0xFF;
                        SectorBuffer[(i * 8) + 1] = 0xFF;
                        SectorBuffer[(i * 8) + 2] = 0xFF;
                        SectorBuffer[(i * 8) + 3] = 0xFF;
                        SectorBuffer[(i * 8) + 4] = 0x01;
                        SectorBuffer[(i * 8) + 5] = 0x00;
                        SectorBuffer[(i * 8) + 6] = 0x00;
                        SectorBuffer[(i * 8) + 7] = 0x00;
                    }
                    else
                    {
                        SectorBuffer[(i * 8)] = 0xFF;
                        SectorBuffer[(i * 8) + 1] = 0xFF;
                        SectorBuffer[(i * 8) + 2] = 0xFF;
                        SectorBuffer[(i * 8) + 3] = 0xFF;
                        SectorBuffer[(i * 8) + 4] = (Byte)((FreeCount) & 0xFF);
                        SectorBuffer[(i * 8) + 5] = (Byte)(((FreeCount) >> 8) & 0xFF);
                        SectorBuffer[(i * 8) + 6] = (Byte)(((FreeCount) >> 16) & 0xFF);
                        SectorBuffer[(i * 8) + 7] = (Byte)(((FreeCount) >> 24) & 0xFF);

                        /* Decrease */
                        FreeCount--;
                        FreeBlockItr--;

                        /* Sanity */
                        if (FreeBlockItr == 0)
                            FreeBlockItr = 0x7FFFFFFF;
                    }

                    /* Go to next */
                    Iterator++;
                }

                /* Write sector */
                WriteDisk(mDisk, 0, SectorBuffer, false);
            }
            
            /* Calculate how many start-buckets we shall skip because of 
             * reserved sectors */
            Int64 SkipBuckets = (ReservedSectors / BucketSize) + 2;
            Int64 MasterBucketSector = ReservedSectors + 1;

            /* Step 2 */
            Console.WriteLine("Writing MasterBucket Original & Mirror");
            Console.WriteLine("Bucket Original Sector: " + MasterBucketSector.ToString());
            Console.WriteLine("Bucket Mirror Sector: " + MirrorMasterBucketSector.ToString());

            /* Setup the MasterBucket structure */
            UInt32 BucketStartFree = 0;

            while (BucketStartFree < ((MasterBucketSector / BucketSize) + 1))
                BucketStartFree++;

            Console.WriteLine("First Free Bucket: " + BucketStartFree.ToString());
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
            WriteDisk(mDisk, 0, Mbr, true);

            /* Done */
            CloseDisk(mDisk);
        }

        /* Recursive List */
        static MfsEntry ListRecursive(MfsDisk mDisk, UInt32 DirBucket, String pPath)
        {
            /* Sanity, if start with "/" skip */
            String mPath = pPath;
            if (mPath.StartsWith("/"))
                mPath = mPath.Substring(1, mPath.Length - 1);

            /* Get token */
            int iDex = mPath.IndexOf("/");
            String LookFor = mPath.Substring(0, iDex == -1 ? mPath.Length : iDex);
            
            /* EoP */
            if (String.IsNullOrEmpty(LookFor)
                || LookFor.Contains("."))
            {
                /* List files */
                UInt32 IteratorBucket = DirBucket;
                int End = 0;

                while (End == 0)
                {
                    UInt64 Sector = IteratorBucket * mDisk.BucketSize;

                    /* Gogo */
                    Byte[] fBuffer = ReadDisk(mDisk, Sector, mDisk.BucketSize);

                    for (int i = 0; i < (mDisk.BucketSize * mDisk.BytesPerSector); i++)
                    {
                        /* EoC? */
                        if (fBuffer[i] == 0)
                        {
                            End = 1;
                            break;
                        }

                        /* Woah, parse */
                        int Len = 0;
                        while (fBuffer[i + 76 + Len] != 0)
                            Len++;
                        String Name = System.Text.Encoding.UTF8.GetString(fBuffer, i + 76, Len);
                        UInt16 Flags = BitConverter.ToUInt16(fBuffer, i + 2);

                        if (LookFor.Contains(".")
                            && Name.ToLower() == LookFor.ToLower())
                        {
                            /* Match */
                            MfsEntry nEntry = new MfsEntry();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(fBuffer, i + 60);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(fBuffer, i + 68);
                            nEntry.Bucket = BitConverter.ToUInt32(fBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(fBuffer, i + 8);

                            nEntry.DirBucket = IteratorBucket;
                            nEntry.DirIndex = (uint)i;

                            /* Done */
                            return nEntry;
                        }
                        else
                        {
                            if (((MfsEntryFlags)Flags).HasFlag(MfsEntryFlags.MFS_DIRECTORY))
                                Console.WriteLine("Dir: " + Name);
                            else
                                Console.WriteLine("File: " + Name + " (" + BitConverter.ToUInt64(fBuffer, i + 60).ToString() + " Bytes)");
                        }

                        /* Next */
                        i += 1023;
                    }

                    /* Get next bucket */
                    UInt32 DirBuckLength = 0;
                    if (End == 0)
                        IteratorBucket = GetNextBucket(mDisk, IteratorBucket, out DirBuckLength);

                    /* End of list? */
                    if (IteratorBucket == 0xFFFFFFFF)
                        End = 1;
                }

                /* Done */
                return null;
            }
            else
            {
                /* Find LookFor in DirBucket */
                UInt32 IteratorBucket = DirBucket;
                int End = 0;

                while (End == 0)
                {
                    UInt64 Sector = IteratorBucket * mDisk.BucketSize;

                    /* Gogo */
                    Byte[] fBuffer = ReadDisk(mDisk, Sector, mDisk.BucketSize);

                    for (int i = 0; i < (mDisk.BucketSize * mDisk.BytesPerSector); i++)
                    {
                        /* EoC? */
                        if (fBuffer[i] == 0)
                        {
                            End = 1;
                            break;
                        }

                        /* Woah, parse */
                        int Len = 0;
                        while (fBuffer[i + 76 + Len] != 0)
                            Len++;
                        String Name = System.Text.Encoding.UTF8.GetString(fBuffer, i + 76, Len);
                        UInt16 Flags = BitConverter.ToUInt16(fBuffer, i + 2);

                        if (Name.ToLower() == LookFor.ToLower())
                        {
                            /* More sanity */
                            if (!((MfsEntryFlags)Flags).HasFlag(MfsEntryFlags.MFS_DIRECTORY))
                            {
                                Console.WriteLine(LookFor + " is not a directory");
                                return null;
                            }

                            /* Match */
                            MfsEntry nEntry = new MfsEntry();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(fBuffer, i + 60);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(fBuffer, i + 68);
                            nEntry.Bucket = BitConverter.ToUInt32(fBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(fBuffer, i + 8);

                            nEntry.DirBucket = IteratorBucket;
                            nEntry.DirIndex = (uint)i;

                            /* Sanity */
                            if (nEntry.Bucket == 0xFFFFFFFF)
                                return null;

                            /* Done */
                            return ListRecursive(mDisk, nEntry.Bucket, mPath.Substring(LookFor.Length));
                        }

                        /* Next */
                        i += 1023;
                    }

                    /* Get next bucket */
                    UInt32 DirBuckLength = 0;
                    if (End == 0)
                        IteratorBucket = GetNextBucket(mDisk, IteratorBucket, out DirBuckLength);

                    /* End of list? */
                    if (IteratorBucket == 0xFFFFFFFF)
                        End = 1;
                }
            }

            return null;
        }

        /* Create Recursive */
        static MfsEntry CreateRecursive(MfsDisk mDisk, UInt32 DirBucket, String pPath)
        {
            /* Sanity, if start with "/" skip */
            String mPath = pPath;
            if (mPath.StartsWith("/"))
                mPath = mPath.Substring(1, mPath.Length - 1);
            if (mPath.EndsWith("/"))
                mPath = mPath.Substring(0, mPath.Length - 1);

            /* Get token */
            int iDex = mPath.IndexOf("/");
            String LookFor = mPath.Substring(0, iDex == -1 ? mPath.Length : iDex);

            /* EoP */
            if (String.IsNullOrEmpty(LookFor)
                || iDex == -1)
            {
                /* List files */
                UInt32 IteratorBucket = DirBucket;
                UInt32 PrevItrBucket = 0;
                int End = 0;

                while (End == 0)
                {
                    UInt64 Sector = IteratorBucket * mDisk.BucketSize;

                    /* Gogo */
                    Byte[] fBuffer = ReadDisk(mDisk, Sector, mDisk.BucketSize);

                    for (int i = 0; i < (mDisk.BucketSize * mDisk.BytesPerSector); i++)
                    {
                        /* EoC? */
                        if (fBuffer[i] == 0)
                        {
                            End = 1;
                            break;
                        }

                        /* Woah, parse */
                        int Len = 0;
                        while (fBuffer[i + 76 + Len] != 0)
                            Len++;
                        String Name = System.Text.Encoding.UTF8.GetString(fBuffer, i + 76, Len);
                        UInt16 Flags = BitConverter.ToUInt16(fBuffer, i + 2);

                        if (Name.ToLower() == LookFor.ToLower())
                        {
                            Console.WriteLine("EOP - Entry did exist already");
                            return null;
                        }

                        /* Next */
                        i += 1023;
                    }

                    /* Sanity */
                    if (End == 1)
                        break;

                    /* Get next bucket */
                    UInt32 DirBucketLength = 0;
                    PrevItrBucket = IteratorBucket;
                    if (End == 0)
                        IteratorBucket = GetNextBucket(mDisk, IteratorBucket, out DirBucketLength);

                    /* End of list? */
                    if (IteratorBucket == 0xFFFFFFFF)
                        End = 1;
                }

                /* We must reach this point */
                MfsEntry nEntry = new MfsEntry();
                nEntry.DirBucket = IteratorBucket == 0xFFFFFFFF ? PrevItrBucket : IteratorBucket;
                nEntry.DirIndex = 0;

                /* Done */
                return nEntry;
            }
            else
            {
                /* Find LookFor in DirBucket */
                UInt32 IteratorBucket = DirBucket;
                int End = 0;

                while (End == 0)
                {
                    UInt64 Sector = IteratorBucket * mDisk.BucketSize;

                    /* Gogo */
                    Byte[] fBuffer = ReadDisk(mDisk, Sector, mDisk.BucketSize);

                    for (int i = 0; i < (mDisk.BucketSize * mDisk.BytesPerSector); i++)
                    {
                        /* EoC? */
                        if (fBuffer[i] == 0)
                        {
                            End = 1;
                            break;
                        }

                        /* Woah, parse */
                        int Len = 0;
                        while (fBuffer[i + 76 + Len] != 0)
                            Len++;
                        String Name = System.Text.Encoding.UTF8.GetString(fBuffer, i + 76, Len);
                        UInt16 Flags = BitConverter.ToUInt16(fBuffer, i + 2);

                        if (Name.ToLower() == LookFor.ToLower())
                        {
                            /* More sanity */
                            if (!((MfsEntryFlags)Flags).HasFlag(MfsEntryFlags.MFS_DIRECTORY))
                            {
                                Console.WriteLine(LookFor + " is not a directory");
                                return null;
                            }

                            /* Match */
                            MfsEntry nEntry = new MfsEntry();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(fBuffer, i + 60);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(fBuffer, i + 68);
                            nEntry.Bucket = BitConverter.ToUInt32(fBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(fBuffer, i + 8);

                            nEntry.DirBucket = IteratorBucket;
                            nEntry.DirIndex = (uint)i;

                            /* Sanity */
                            if (nEntry.Bucket == 0xFFFFFFFF)
                            {
                                Byte[] Mbr = ReadDisk(mDisk, 0, 1);

                                /* Find MB Ptr */
                                UInt64 MbSector = BitConverter.ToUInt64(Mbr, 28);
                                UInt64 MbMirrorSector = BitConverter.ToUInt64(Mbr, 36);

                                /* Find Root Ptr in MB */
                                Byte[] Mb = ReadDisk(mDisk, MbSector, 1);
                                UInt32 FreeBucket = BitConverter.ToUInt32(Mb, 8);

                                /* Allocate */
                                nEntry.Bucket = FreeBucket;
                                UInt32 InitialBucketSize = 0;
                                UInt32 NextFree = AllocateBucket(mDisk, FreeBucket, 1, out InitialBucketSize);

                                /* Update Mb */
                                Mb[8] = (Byte)(NextFree & 0xFF);
                                Mb[9] = (Byte)((NextFree >> 8) & 0xFF);
                                Mb[10] = (Byte)((NextFree >> 16) & 0xFF);
                                Mb[11] = (Byte)((NextFree >> 24) & 0xFF);

                                /* Write Mb */
                                WriteDisk(mDisk, MbSector, Mb, true);
                                WriteDisk(mDisk, MbMirrorSector, Mb, true);

                                /* Update Dir */
                                fBuffer[i + 4] = (Byte)(nEntry.Bucket & 0xFF);
                                fBuffer[i + 5] = (Byte)((nEntry.Bucket >> 8) & 0xFF);
                                fBuffer[i + 6] = (Byte)((nEntry.Bucket >> 16) & 0xFF);
                                fBuffer[i + 7] = (Byte)((nEntry.Bucket >> 24) & 0xFF);

                                fBuffer[i + 8] = 0x01;
                                fBuffer[i + 9] = 0x00;
                                fBuffer[i + 10] = 0x00;
                                fBuffer[i + 11] = 0x00;

                                WriteDisk(mDisk, Sector, fBuffer, true);

                                /* Wipe bucket */
                                Byte[] Wipe = new Byte[mDisk.BucketSize * mDisk.BytesPerSector];
                                for (int g = 0; g < Wipe.Length; g++)
                                    Wipe[g] = 0;
                                WriteDisk(mDisk, (nEntry.Bucket * mDisk.BucketSize), Wipe, true);
                            }

                            /* Done */
                            return CreateRecursive(mDisk, nEntry.Bucket, mPath.Substring(LookFor.Length));
                        }

                        /* Next */
                        i += 1023;
                    }

                    /* Get next bucket */
                    UInt32 DirBucketLength = 0;
                    if (End == 0)
                        IteratorBucket = GetNextBucket(mDisk, IteratorBucket, out DirBucketLength);

                    /* CAN not be end of list? */
                    if (IteratorBucket == 0xFFFFFFFF)
                        End = 1;
                }
            }

            /* We got broken */
            Console.WriteLine("Failed to find " + LookFor + " in directory");
            return null;
        }

        /* Lists root directory */
        static void ListDirectory(MfsDisk mDisk, String Path)
        {
            /* Open Disk */
            if (!OpenDisk(mDisk)) {
                Console.WriteLine("Failed to open drive");
                return;
            }

            /* Read MBR */
            Byte[] Mbr = ReadDisk(mDisk, 0, 1);

            /* Find MB Ptr */
            UInt64 MbSector = BitConverter.ToUInt64(Mbr, 28);
            mDisk.BucketSize = BitConverter.ToUInt16(Mbr, 26);

            /* Find Root Ptr in MB */
            Byte[] Mb = ReadDisk(mDisk, MbSector, 1);
            UInt32 RootBucket = BitConverter.ToUInt32(Mb, 12);

            /* Recurse-Parse Root */
            Console.WriteLine("Files in " + Path + ":");
            ListRecursive(mDisk, RootBucket, Path);
            Console.WriteLine("");

            /* Done */
            CloseDisk(mDisk);
        }

        /* Create entry in Mb */
        static void CreateFileEntry(MfsDisk mDisk, UInt16 Flags, UInt32 Bucket, UInt32 BucketLength, Byte[] Data, String Name, UInt32 DirBucket)
        {
            /* List files */
            UInt32 IteratorBucket = DirBucket;
            int End = 0;

            while (End == 0)
            {
                UInt64 Sector = IteratorBucket * mDisk.BucketSize;

                /* Gogo */
                Byte[] fBuffer = ReadDisk(mDisk, Sector, mDisk.BucketSize);

                for (int i = 0; i < (mDisk.BucketSize * mDisk.BytesPerSector); i++)
                {
                    /* EoC? */
                    if (fBuffer[i] == 0)
                    {
                        UInt64 NumBuckets = 0;
                        UInt64 AllocatedSize = 0;
                        UInt64 DataLen = 0;

                        if (Data != null)
                        {
                            DataLen = (UInt64)Data.LongLength;
                            NumBuckets = (UInt64)(Data.LongLength / mDisk.BytesPerSector) / mDisk.BucketSize;
                            if (((Data.LongLength / mDisk.BytesPerSector) % mDisk.BucketSize) > 0)
                                NumBuckets++;
                            AllocatedSize = NumBuckets * mDisk.BucketSize * mDisk.BytesPerSector;
                        }

                        /* Setup Status */
                        fBuffer[i] = 0x1;
                        fBuffer[i + 1] = 0x0;

                        /* Setup flags */
                        fBuffer[i + 2] = (Byte)(Flags & 0xFF);
                        fBuffer[i + 3] = (Byte)((Flags >> 8) & 0xFF);

                        /* Setup start bucket */
                        fBuffer[i + 4] = (Byte)(Bucket & 0xFF);
                        fBuffer[i + 5] = (Byte)((Bucket >> 8) & 0xFF);
                        fBuffer[i + 6] = (Byte)((Bucket >> 16) & 0xFF);
                        fBuffer[i + 7] = (Byte)((Bucket >> 24) & 0xFF);

                        fBuffer[i + 8] = (Byte)(BucketLength & 0xFF);
                        fBuffer[i + 9] = (Byte)((BucketLength >> 8) & 0xFF);
                        fBuffer[i + 10] = (Byte)((BucketLength >> 16) & 0xFF);
                        fBuffer[i + 11] = (Byte)((BucketLength >> 24) & 0xFF);

                        /* Ignore time and date for now */
                        fBuffer[i + 60] = (Byte)(DataLen & 0xFF);
                        fBuffer[i + 61] = (Byte)((DataLen >> 8) & 0xFF);
                        fBuffer[i + 62] = (Byte)((DataLen >> 16) & 0xFF);
                        fBuffer[i + 63] = (Byte)((DataLen >> 24) & 0xFF);
                        fBuffer[i + 64] = (Byte)((DataLen >> 32) & 0xFF);
                        fBuffer[i + 65] = (Byte)((DataLen >> 40) & 0xFF);
                        fBuffer[i + 66] = (Byte)((DataLen >> 48) & 0xFF);
                        fBuffer[i + 67] = (Byte)((DataLen >> 56) & 0xFF);

                        fBuffer[i + 68] = (Byte)(AllocatedSize & 0xFF);
                        fBuffer[i + 69] = (Byte)((AllocatedSize >> 8) & 0xFF);
                        fBuffer[i + 70] = (Byte)((AllocatedSize >> 16) & 0xFF);
                        fBuffer[i + 71] = (Byte)((AllocatedSize >> 24) & 0xFF);
                        fBuffer[i + 72] = (Byte)((AllocatedSize >> 32) & 0xFF);
                        fBuffer[i + 73] = (Byte)((AllocatedSize >> 40) & 0xFF);
                        fBuffer[i + 74] = (Byte)((AllocatedSize >> 48) & 0xFF);
                        fBuffer[i + 75] = (Byte)((AllocatedSize >> 56) & 0xFF);

                        /* Setup Name */
                        Byte[] NameData = System.Text.Encoding.UTF8.GetBytes(Name);
                        for (int j = 0; j < NameData.Length; j++)
                            fBuffer[i + 76 + j] = NameData[j];

                        /* Don't use rest for now */
                        WriteDisk(mDisk, Sector, fBuffer, true);

                        /* Done */
                        return;
                    }

                    /* Next */
                    i += 1023;
                }

                /* Get next bucket */
                UInt32 DirBucketLength = 0;
                UInt32 PrevDirBucket = IteratorBucket;
                if (End == 0)
                    IteratorBucket = GetNextBucket(mDisk, IteratorBucket, out DirBucketLength);

                /* End of list? */
                if (IteratorBucket == 0xFFFFFFFF)
                {
                    /* Read MBR */
                    Console.WriteLine("Expanding directory");
                    Byte[] Mbr = ReadDisk(mDisk, 0, 1);

                    /* Find MB Ptr */
                    UInt64 MbSector = BitConverter.ToUInt64(Mbr, 28);
                    UInt64 MbMirrorSector = BitConverter.ToUInt64(Mbr, 36);

                    /* Find Root Ptr in MB */
                    Byte[] Mb = ReadDisk(mDisk, MbSector, 1);
                    UInt32 FreeBucket = BitConverter.ToUInt32(Mb, 8);

                    /* Allocate bucket */
                    Console.WriteLine("New bucket: " + FreeBucket.ToString());
                    UInt32 NextFreeBucket = AllocateBucket(mDisk, FreeBucket, 1, out DirBucketLength);
                    Console.WriteLine("Next free bucket: " + NextFreeBucket.ToString());

                    /* Extend directory */
                    SetNextBucket(mDisk, PrevDirBucket, FreeBucket);

                    /* Update Mb(s) */
                    Mb[8] = (Byte)(NextFreeBucket & 0xFF);
                    Mb[9] = (Byte)((NextFreeBucket >> 8) & 0xFF);
                    Mb[10] = (Byte)((NextFreeBucket >> 16) & 0xFF);
                    Mb[11] = (Byte)((NextFreeBucket >> 24) & 0xFF);
                    WriteDisk(mDisk, MbSector, Mb, true);
                    WriteDisk(mDisk, MbMirrorSector, Mb, true);

                    /* Wipe the bucket */
                    Byte[] Wipe = new Byte[mDisk.BucketSize * mDisk.BytesPerSector];
                    for (int i = 0; i < Wipe.Length; i++)
                        Wipe[i] = 0;

                    Console.WriteLine("Wiping new bucket");
                    WriteDisk(mDisk, (FreeBucket * mDisk.BucketSize), Wipe, true);

                    /* Update IteratorBucket */
                    IteratorBucket = FreeBucket;
                }
                    
            }
        }

        /* Fill bucketchain with data */
        static void FillBucketChain(MfsDisk mDisk, UInt32 Bucket, UInt32 BucketLength, Byte[] Data)
        {
            /* Index */
            Int64 Index = 0;
            UInt32 BucketPtr = Bucket;
            UInt32 BucketLengthItr = BucketLength;

            /* Iterate */
            while (Index < Data.LongLength)
            {
                /* Alloc Buffer */
                Byte[] Buffer = new Byte[(mDisk.BucketSize * mDisk.BytesPerSector) * BucketLengthItr];

                /* Copy */
                for (int i = 0; i < Buffer.Length; i++)
                {
                    if (Index + i >= Data.LongLength)
                        Buffer[i] = 0;
                    else
                        Buffer[i] = Data[Index + i];
                }
                    
                /* Inc */
                Index += Buffer.Length;

                /* Calculate target sector */
                UInt64 Sector = mDisk.BucketSize * BucketPtr;
                WriteDisk(mDisk, Sector, Buffer, true);

                /* Get next bucket */
                BucketPtr = GetNextBucket(mDisk, BucketPtr, out BucketLengthItr);

                /* Sanity */
                if (BucketPtr == 0xFFFFFFFF)
                    break;
            }
        }

        /* Write file */
        static void WriteToMfs(MfsDisk mDisk, String pFile, String lPath)
        {
            /* Open Disk */
            if (!OpenDisk(mDisk))
            {
                Console.WriteLine("Failed to open drive");
                return;
            }

            /* Read MBR */
            Byte[] Mbr = ReadDisk(mDisk, 0, 1);

            /* Find MB Ptr */
            UInt64 MbSector = BitConverter.ToUInt64(Mbr, 28);
            UInt64 MbMirrorSector = BitConverter.ToUInt64(Mbr, 36);
            mDisk.BucketSize = BitConverter.ToUInt16(Mbr, 26);

            /* Find Root Ptr in MB */
            Byte[] Mb = ReadDisk(mDisk, MbSector, 1);
            UInt32 FreeBucket = BitConverter.ToUInt32(Mb, 8);
            UInt32 RootBucket = BitConverter.ToUInt32(Mb, 12);

            /* Load File */
            Console.WriteLine("Reading source data");
            Byte[] FileData = File.ReadAllBytes(pFile);

            /* Can we even write a file there */
            MfsEntry nEntry = ListRecursive(mDisk, RootBucket, lPath);
            if (nEntry != null)
            {
                Console.WriteLine("File exists in table, updating");

                /* Sanity */
                if ((UInt64)FileData.LongLength > nEntry.AllocatedSize)
                {
                    /* Allocate more */
                    UInt64 NumSectors = ((UInt64)FileData.LongLength - nEntry.AllocatedSize) / mDisk.BytesPerSector;
                    if ((((UInt64)FileData.LongLength - nEntry.AllocatedSize) % mDisk.BytesPerSector) > 0)
                        NumSectors++;

                    UInt64 NumBuckets = NumSectors / mDisk.BucketSize;
                    if ((NumSectors % mDisk.BucketSize) > 0)
                        NumBuckets++;

                    /* Allocate buckets */
                    UInt32 StartBucket = FreeBucket;
                    UInt32 InitialBucketSize = 0;
                    FreeBucket = AllocateBucket(mDisk, FreeBucket, NumBuckets, out InitialBucketSize);

                    /* Get last bucket in chain */
                    UInt32 BucketPtr = nEntry.Bucket;
                    UInt32 BucketPrevPtr = 0;
                    UInt32 BucketLength = 0;
                    while (BucketPtr != 0xFFFFFFFF)
                    {
                        BucketPrevPtr = BucketPtr;
                        BucketPtr = GetNextBucket(mDisk, BucketPtr, out BucketLength);
                    }

                    /* Update pointer */
                    SetNextBucket(mDisk, BucketPrevPtr, FreeBucket);

                    /* Update MB */
                    Mb[8] = (Byte)(FreeBucket & 0xFF);
                    Mb[9] = (Byte)((FreeBucket >> 8) & 0xFF);
                    Mb[10] = (Byte)((FreeBucket >> 16) & 0xFF);
                    Mb[11] = (Byte)((FreeBucket >> 24) & 0xFF);

                    /* Write Mb */
                    WriteDisk(mDisk, MbSector, Mb, true);
                    WriteDisk(mDisk, MbMirrorSector, Mb, true);

                    /* Adjust */
                    nEntry.AllocatedSize += (NumBuckets * mDisk.BucketSize * mDisk.BytesPerSector);
                }

                /* If file size is drastically less, free some buckets */

                /* Write Data */
                Console.WriteLine("Updating Data");
                FillBucketChain(mDisk, nEntry.Bucket, nEntry.BucketLength, FileData);

                /* Update entry with new sizes, new dates, new times */
                Byte[] fBuffer = ReadDisk(mDisk, (nEntry.DirBucket * mDisk.BucketSize), mDisk.BucketSize);
                
                /* Modify */
                fBuffer[nEntry.DirIndex + 4] = (Byte)(nEntry.Bucket & 0xFF);
                fBuffer[nEntry.DirIndex + 5] = (Byte)((nEntry.Bucket >> 8) & 0xFF);
                fBuffer[nEntry.DirIndex + 6] = (Byte)((nEntry.Bucket >> 16) & 0xFF);
                fBuffer[nEntry.DirIndex + 7] = (Byte)((nEntry.Bucket >> 24) & 0xFF);

                fBuffer[nEntry.DirIndex + 8] = (Byte)(nEntry.BucketLength & 0xFF);
                fBuffer[nEntry.DirIndex + 9] = (Byte)((nEntry.BucketLength >> 8) & 0xFF);
                fBuffer[nEntry.DirIndex + 10] = (Byte)((nEntry.BucketLength >> 16) & 0xFF);
                fBuffer[nEntry.DirIndex + 11] = (Byte)((nEntry.BucketLength >> 24) & 0xFF);

                fBuffer[nEntry.DirIndex + 60] = (Byte)(FileData.LongLength & 0xFF);
                fBuffer[nEntry.DirIndex + 61] = (Byte)((FileData.LongLength >> 8) & 0xFF);
                fBuffer[nEntry.DirIndex + 62] = (Byte)((FileData.LongLength >> 16) & 0xFF);
                fBuffer[nEntry.DirIndex + 63] = (Byte)((FileData.LongLength >> 24) & 0xFF);
                fBuffer[nEntry.DirIndex + 64] = (Byte)((FileData.LongLength >> 32) & 0xFF);
                fBuffer[nEntry.DirIndex + 65] = (Byte)((FileData.LongLength >> 40) & 0xFF);
                fBuffer[nEntry.DirIndex + 66] = (Byte)((FileData.LongLength >> 48) & 0xFF);
                fBuffer[nEntry.DirIndex + 67] = (Byte)((FileData.LongLength >> 56) & 0xFF);

                fBuffer[nEntry.DirIndex + 68] = (Byte)(nEntry.AllocatedSize & 0xFF);
                fBuffer[nEntry.DirIndex + 69] = (Byte)((nEntry.AllocatedSize >> 8) & 0xFF);
                fBuffer[nEntry.DirIndex + 70] = (Byte)((nEntry.AllocatedSize >> 16) & 0xFF);
                fBuffer[nEntry.DirIndex + 71] = (Byte)((nEntry.AllocatedSize >> 24) & 0xFF);
                fBuffer[nEntry.DirIndex + 72] = (Byte)((nEntry.AllocatedSize >> 32) & 0xFF);
                fBuffer[nEntry.DirIndex + 73] = (Byte)((nEntry.AllocatedSize >> 40) & 0xFF);
                fBuffer[nEntry.DirIndex + 74] = (Byte)((nEntry.AllocatedSize >> 48) & 0xFF);
                fBuffer[nEntry.DirIndex + 75] = (Byte)((nEntry.AllocatedSize >> 56) & 0xFF);

                /* Write back */
                WriteDisk(mDisk, (nEntry.DirBucket * mDisk.BucketSize), fBuffer, true);
            }
            else
            {
                Console.WriteLine("/" + Path.GetFileName(pFile) + " is new, creating");
                MfsEntry cInfo = CreateRecursive(mDisk, RootBucket, lPath);
                if (cInfo == null)
                {
                    Console.WriteLine("The creation info returned null, somethings wrong");
                    return;
                }

                /* Get first free bucket again, could have changed after CreateRecursive */
                Mb = ReadDisk(mDisk, MbSector, 1);
                FreeBucket = BitConverter.ToUInt32(Mb, 8);

                /* Calculate Sector Count */
                UInt64 NumSectorsForBuckets = (ulong)FileData.LongLength / mDisk.BytesPerSector;
                if (((ulong)FileData.LongLength % mDisk.BytesPerSector) > 0)
                    NumSectorsForBuckets++;

                /* Get first free bucket */
                UInt64 NumBuckets = NumSectorsForBuckets / mDisk.BucketSize;
                if ((NumSectorsForBuckets % mDisk.BucketSize) > 0)
                    NumBuckets++;

                /* Allocate a chain */
                Console.WriteLine("Allocing " + NumBuckets.ToString() + " buckets, old free ptr " + FreeBucket.ToString());
                UInt32 StartBucket = FreeBucket;
                UInt32 InitialBucketSize = 0;
                FreeBucket = AllocateBucket(mDisk, FreeBucket, NumBuckets, out InitialBucketSize);
                Console.WriteLine("Done, new free pointer " + FreeBucket.ToString());

                /* Update MB */
                Mb[8] = (Byte)(FreeBucket & 0xFF);
                Mb[9] = (Byte)((FreeBucket >> 8) & 0xFF);
                Mb[10] = (Byte)((FreeBucket >> 16) & 0xFF);
                Mb[11] = (Byte)((FreeBucket >> 24) & 0xFF);

                /* Write Mb */
                WriteDisk(mDisk, MbSector, Mb, true);
                WriteDisk(mDisk, MbMirrorSector, Mb, true);

                /* Create entry */
                Console.WriteLine("Creating entry in path");
                CreateFileEntry(mDisk, (ushort)(MfsEntryFlags.MFS_FILE | MfsEntryFlags.MFS_SYSTEM | MfsEntryFlags.MFS_SECURITY), 
                    StartBucket, InitialBucketSize, FileData, Path.GetFileName(lPath), cInfo.DirBucket);
                Console.WriteLine("Done");

                /* Write Data */
                Console.WriteLine("Writing Data");
                FillBucketChain(mDisk, StartBucket, InitialBucketSize, FileData);
            }

            /* Done */
            Console.WriteLine("File Creation Done");
            CloseDisk(mDisk);
        }

        /* Install MollenOS */
        static void InstallMOS(MfsDisk mDisk)
        {
            /* Open Disk */
            if (!OpenDisk(mDisk)) {
                Console.WriteLine("Failed to open drive");
                return;
            }

            /* Load the mbr, modify it */
            Console.WriteLine("Setting up BootCode");
            Byte[] BootCode = File.ReadAllBytes("Stage1.bin");

            /* Setup Mbr */
            Byte[] Mbr = ReadDisk(mDisk, 0, 1);
            Buffer.BlockCopy(Mbr, 3, BootCode, 3, 49);

            /* Setup Flag to OS-DRIVE (0x1) */
            BootCode[8] = 0x1;

            /* Write bootloader */
            Console.WriteLine("Writing BootCode");
            WriteDisk(mDisk, 0, BootCode, true);

            /* Get size of stage2-loader */
            Console.WriteLine("Loading Stage 2");
            FileStream nLoader = new FileStream("ssbl.stm", FileMode.Open);
            Byte[] Stage2Data = new Byte[nLoader.Length];
            nLoader.Read(Stage2Data, 0, (int)nLoader.Length);
            nLoader.Close();

            /* Allocate a new buffer */
            Console.WriteLine("Writing Stage 2");
            Byte[] ReservedBuffer = new Byte[((Stage2Data.Length / mDisk.BytesPerSector) + 1) * mDisk.BytesPerSector];
            Stage2Data.CopyTo(ReservedBuffer, 0);
            WriteDisk(mDisk, 1, ReservedBuffer, true);

            /* Find MB Ptr */
            UInt64 MbSector = BitConverter.ToUInt64(Mbr, 28);
            UInt64 MbMirrorSector = BitConverter.ToUInt64(Mbr, 36);
            mDisk.BucketSize = BitConverter.ToUInt16(Mbr, 26);

            /* Find Root Ptr in MB */
            Byte[] Mb = ReadDisk(mDisk, MbSector, 1);
            UInt32 RootBucket = BitConverter.ToUInt32(Mb, 12);

            /* Setup directories */
            String BaseRoot = "hdd\\";
            Console.WriteLine("Creating system file tree");
            Console.WriteLine("Root: " + AppDomain.CurrentDomain.BaseDirectory + "hdd\\");
            String[] Dirs = Directory.GetDirectories("hdd\\", "*", SearchOption.AllDirectories);

            foreach (String pDir in Dirs)
            {
                String RelPath = pDir.Substring(BaseRoot.Length).Replace('\\', '/');
                String DirToCreate = pDir.Split(Path.DirectorySeparatorChar).Last();
                Console.WriteLine("Creating: " + RelPath + "  (" + DirToCreate + ")");

                /* Find free entry */
                MfsEntry mEntry = CreateRecursive(mDisk, RootBucket, RelPath);

                /* Create the entry */
                CreateFileEntry(mDisk, (ushort)(MfsEntryFlags.MFS_DIRECTORY | MfsEntryFlags.MFS_SYSTEM),
                    0xFFFFFFFF, 0, null, DirToCreate, mEntry.DirBucket);
            }

            /* Cleanup */
            CloseDisk(mDisk);

            /* Setup files */
            Console.WriteLine("Copying system files");
            String[] Files = Directory.GetFiles("hdd\\", "*.*", SearchOption.AllDirectories);

            foreach (String pFile in Files)
            {
                String RelPath = pFile.Substring(BaseRoot.Length).Replace('\\', '/');
                Console.WriteLine("Copying: " + RelPath);

                /* Create */
                WriteToMfs(mDisk, pFile, RelPath);
            }
        }

        /* InstallMollenOS
         * Installs mollenos on to the given filesystem and prepares
         * the proper boot-sectors for the partition */
        static void InstallMollenOS(IFileSystem FileSystem)
        {

        }

        /* LaunchCLI
         * Launches the CLI and provides commands for manipluating disks */
        static void LaunchCLI(Hashtable Drives)
        {
            Console.WriteLine("\nAvailable Commands:");
            Console.WriteLine("format <drive>");
            Console.WriteLine("write <file> <drive>");
            Console.WriteLine("ls <path> <drive>");
            Console.WriteLine("install <drive>");
            Console.WriteLine("quit");
            Console.WriteLine("");

            /* Cmd */
            while (true)
            {
                /* Spit */
                Console.Write(" $ ");

                /* Read */
                String Input = Console.ReadLine();
                String[] Tokens = Input.Split(new Char[] { ' ' });

                /* Lets see */
                switch (Tokens[0].ToLower())
                {
                    case "format":
                        {
                            /* Parse */
                            int Option = int.Parse(Tokens[1]);

                            /* Do the format */
                            Format((MfsDisk)Drives[Option]);

                        }
                        break;
                    case "write":
                        {
                            /* Path */
                            String Path = Tokens[1];

                            /* Parse */
                            int Option = int.Parse(Tokens[2]);

                            /* Gogo */
                            WriteToMfs((MfsDisk)Drives[Option], Path, "");

                        }
                        break;
                    case "ls":
                        {
                            /* Path */
                            String Path = Tokens[1];

                            /* Parse */
                            int Option = int.Parse(Tokens[2]);

                            /* Gogo */
                            ListDirectory((MfsDisk)Drives[Option], Path);

                        }
                        break;
                    case "install":
                        {
                            /* Parse */
                            int Option = int.Parse(Tokens[1]);

                            /* Gogo */
                            InstallMOS((MfsDisk)Drives[Option]);

                        }
                        break;
                    case "quit":
                        return 0;

                    default:
                        break;
                }

                /* Clean */
                GC.Collect();
            }
        }

        /* DiskUtility Entry 
         * Handles all command line switches and initializes the utility */
        static int Main(string[] args)
        {
            // Variables
            IDiskScheme Scheme;
            String Target = "live";
            String SchemeType = "mbr";
            bool Automatic = false;

            // Debug print header
            Console.WriteLine("MFS Utility Software");
            Console.WriteLine("Software Capabilities include formatting, read, write to/from MFS.\n");
            
            // Retrieve a list of physical drives
            Hashtable Drives = new Hashtable();
            Console.WriteLine("Available Drives:");
            WqlObjectQuery q = new WqlObjectQuery("SELECT * FROM Win32_DiskDrive");
            ManagementObjectSearcher res = new ManagementObjectSearcher(q);
            int Itr = 0;
            foreach (ManagementObject o in res.Get()) {
                
                // Never take main-drive into account
                if (o["DeviceID"].ToString().Contains("PHYSICALDRIVE0"))
                    continue;

                // Debug
                Console.WriteLine(Itr.ToString() + ". " + o["Caption"] + " (DeviceID = " + o["DeviceID"] + ")");

                // Create and store the disk
                Drives.Add(Itr++, new CDisk((String)o["DeviceID"], (UInt32)o["BytesPerSector"],
                    (UInt32)o["SectorsPerTrack"], (UInt32)o["TracksPerCylinder"], (UInt64)o["TotalSectors"]));
            }

            // Parse arguments
            if (args != null && args.Length > 0) {
                for (int i = 0; i < args.Length; i++) {
                    switch (args[i].ToLower()) {
                        case "-auto":
                        case "-a": {
                                Automatic = true;
                            } break;

                        case "-target": {
                                Target = args[i + 1];
                                i++;
                            } break;

                        case "-scheme": {
                                SchemeType = args[i + 1];
                                i++;
                            } break;
                    }
                }
            }

            // Should we automate the process?
            if (Automatic)
            {
                // Variables
                IFileSystem FileSystem;
                CDisk Disk = null;

                // Which kind of target?
                if (Target.ToLower() == "live" && Drives.Count > 0)
                    Disk = (CDisk)Drives[0];
                else if (Target.ToLower() == "vmdk")
                    Disk = new CDisk("VMDK", 512, 63, 255, 262144);
                else if (Target.ToLower() == "img")
                    Disk = new CDisk("IMG", 512, 63, 255, 262144);
                else {
                    Console.WriteLine("Invalid option for -target");
                    return -1;
                }

                // Which kind of disk-scheme?
                if (SchemeType.ToLower() == "mbr")
                    Scheme = new SchemeMBR();
                else if (SchemeType.ToLower() == "gpt")
                    Scheme = null;
                else {
                    Console.WriteLine("Invalid option for -scheme");
                    return -1;
                }

                // Partition setup?
                FileSystem = new CMollenOSFileSystem();

                // Setup disk partition layout
                Scheme.Create(Disk);

                // Create the requested partition setup
                Scheme.AddPartition(FileSystem, Scheme.GetFreeSectorCount());

                // Install mollenos on the disk
                InstallMollenOS(FileSystem);
            }

            // Launch CLI if none-automatic
            if (!Automatic)
                LaunchCLI(Drives);

            // Return
            return 0;
        }
    }

    /* Represents a Mfs Entry */
    public class MfsEntry
    {
        /* Name */
        public String Name;
        public UInt64 Size;
        public UInt64 AllocatedSize;
        public UInt32 Bucket;
        public UInt32 BucketLength;

        public UInt32 DirBucket;
        public UInt32 DirIndex;
    }
}
