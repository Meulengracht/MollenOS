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
        // File flags for mfs
        //uint32_t Flags;             // 0x00 - Record Flags
        //uint32_t StartBucket;       // 0x04 - First data bucket
        //uint32_t StartLength;       // 0x08 - Length of first data bucket
        //uint32_t RecordChecksum;        // 0x0C - Checksum of record excluding this entry + inline data
        //uint64_t DataChecksum;      // 0x10 - Checksum of data
        //DateTimeRecord_t CreatedAt;         // 0x18 - Created timestamp
        //DateTimeRecord_t ModifiedAt;            // 0x20 - Last modified timestamp
        //DateTimeRecord_t AccessedAt;            // 0x28 - Last accessed timestamp
        //uint64_t Size;              // 0x30 - Size of data (Set size if sparse)
        //uint64_t AllocatedSize;     // 0x38 - Actual size allocated
        //uint32_t SparseMap;         // 0x40 - Bucket of sparse-map
        //uint8_t Name[300];          // 0x44 - Record name (150 UTF16)
        //VersionRecord_t Versions[4];// 0x170 - Record Versions
        //uint8_t Integrated[512];	// 0x200
        public enum MfsEntryFlags
        {
            MFS_FILE = 0x1,
            MFS_SECURITY = 0x2,
            MFS_DIRECTORY = 0x4,
            MFS_SYSTEM = 0x8,
            MFS_HIDDEN = 0x10,
            MFS_LINK = 0x20
        }

        // Constants
        private readonly UInt32 MFS_ENDOFCHAIN = 0xFFFFFFFF;

        // Variabes
        private String m_szName;
        private CDisk m_pDisk;
        private UInt64 m_iSector;
        private UInt64 m_iSectorCount;
        private UInt16 m_iBucketSize;

        /* CalculateChecksum
         * Allocates the number of requested buckets, and spits out the initial allocation size */
        uint CalculateChecksum(Byte[] Data, int SkipIndex, int SkipLength)
        {
            // Variables
            uint Checksum = 0;

            // Build checksum
            for (int i = 0; i < Data.Length; i++) {
                if (i >= SkipIndex && i < (SkipIndex + SkipLength)) {
                    continue;
                }
                Checksum += Data[i];
            }

            // Done
            return Checksum;
        }

        /* AllocateBuckets
         * Allocates the number of requested buckets, and spits out the initial allocation size */
        UInt32 AllocateBuckets(UInt32 FreeBucketIndex, UInt64 NumBuckets, out UInt32 InitialSize)
        {
            // Calculates the position of the bucket-map
            UInt64 Buckets = m_iSectorCount / m_iBucketSize;
            UInt64 BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            UInt64 BucketMapSector = (m_iSectorCount - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));
            UInt32 BucketsPerSector = m_pDisk.BytesPerSector / 8;

            // Iterate and allocate the buckets
            UInt64 Counter = NumBuckets;
            UInt32 BucketPtr = FreeBucketIndex;
            UInt32 BucketPrevPtr = 0;
            UInt32 FirstFreeSize = 0;
            while (Counter > 0) {
                UInt32 FreeCount = 0;
                UInt32 SectorOffset = BucketPtr / BucketsPerSector;
                UInt32 SectorIndex = BucketPtr % BucketsPerSector;

                // Read the map sector
                Byte[] Sector = m_pDisk.Read(m_iSector + BucketMapSector + SectorOffset, 1);

                // Convert
                BucketPrevPtr = BucketPtr;
                BucketPtr = BitConverter.ToUInt32(Sector, (int)(SectorIndex * 8));
                FreeCount = BitConverter.ToUInt32(Sector, (int)((SectorIndex * 8) + 4));

                // Did this block have enough for us?
                if (FreeCount > Counter) {
                    // Yes, we need to split it up to two blocks now
                    UInt32 NextFreeBucket = BucketPrevPtr + (UInt32)Counter;
                    UInt32 NextFreeCount = FreeCount - (UInt32)Counter;

                    if (FirstFreeSize == 0)
                        FirstFreeSize = (UInt32)Counter;

                    // We have to adjust now, 
                    // since we are taking only a chunk
                    // of the available length 
                    Sector[SectorIndex * 8] = 0xFF;
                    Sector[SectorIndex * 8 + 1] = 0xFF;
                    Sector[SectorIndex * 8 + 2] = 0xFF;
                    Sector[SectorIndex * 8 + 3] = 0xFF;
                    Sector[SectorIndex * 8 + 4] = (Byte)(Counter & 0xFF);
                    Sector[SectorIndex * 8 + 5] = (Byte)((Counter >> 8) & 0xFF);
                    Sector[SectorIndex * 8 + 6] = (Byte)((Counter >> 16) & 0xFF);
                    Sector[SectorIndex * 8 + 7] = (Byte)((Counter >> 24) & 0xFF);

                    // Update map sector
                    m_pDisk.Write(Sector, m_iSector + BucketMapSector + SectorOffset, true);

                    // Create new block
                    SectorOffset = NextFreeBucket / BucketsPerSector;
                    SectorIndex = NextFreeBucket % BucketsPerSector;

                    // Read the map sector
                    Sector = m_pDisk.Read(m_iSector + BucketMapSector + SectorOffset, 1);

                    // Update the link
                    Sector[SectorIndex * 8] = (Byte)(BucketPtr & 0xFF);
                    Sector[SectorIndex * 8 + 1] = (Byte)((BucketPtr >> 8) & 0xFF);
                    Sector[SectorIndex * 8 + 2] = (Byte)((BucketPtr >> 16) & 0xFF);
                    Sector[SectorIndex * 8 + 3] = (Byte)((BucketPtr >> 24) & 0xFF);
                    Sector[SectorIndex * 8 + 4] = (Byte)(NextFreeCount & 0xFF);
                    Sector[SectorIndex * 8 + 5] = (Byte)((NextFreeCount >> 8) & 0xFF);
                    Sector[SectorIndex * 8 + 6] = (Byte)((NextFreeCount >> 16) & 0xFF);
                    Sector[SectorIndex * 8 + 7] = (Byte)((NextFreeCount >> 24) & 0xFF);

                    // Update the map sector again
                    m_pDisk.Write(Sector, m_iSector + BucketMapSector + SectorOffset, true);

                    // Done
                    InitialSize = FirstFreeSize;
                    return NextFreeBucket;
                }
                else
                {
                    // We can just take the whole cake
                    // no need to modify it's length 
                    if (FirstFreeSize == 0)
                        FirstFreeSize = FreeCount;

                    Counter -= FreeCount;
                }
            }

            // Update BucketPrevPtr to MFS_ENDOFCHAIN
            UInt32 _SecOff = BucketPrevPtr / BucketsPerSector;
            UInt32 _SecInd = BucketPrevPtr % BucketsPerSector;

            
            // Read the sector
            Byte[] _Sec = m_pDisk.Read(m_iSector + BucketMapSector + _SecOff, 1);

            // Modify link
            _Sec[_SecInd * 8] = 0xFF;
            _Sec[_SecInd * 8 + 1] = 0xFF;
            _Sec[_SecInd * 8 + 2] = 0xFF;
            _Sec[_SecInd * 8 + 3] = 0xFF;

            // Update sector
            m_pDisk.Write(_Sec, m_iSector + BucketMapSector + _SecOff, true);

            // Done
            InitialSize = FirstFreeSize;
            return BucketPtr;
        }

        /* GetBucketLengthAndLink
         * The next bucket is returned as result, and the length of the bucket is
         * given in the _out_ parameter */
        UInt32 GetBucketLengthAndLink(UInt32 Bucket, out UInt32 BucketLength)
        {
            // Calculates the position of the bucket-map
            UInt64 Buckets = m_iSectorCount / m_iBucketSize;
            UInt64 BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            UInt64 BucketMapSector = (m_iSectorCount - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));

            // Calculate index into bucket map
            UInt32 BucketsPerSector = m_pDisk.BytesPerSector / 8;
            UInt32 SectorOffset = Bucket / BucketsPerSector;
            UInt32 SectorIndex = Bucket % BucketsPerSector;

            // Read the calculated sector
            Byte[] Sector = m_pDisk.Read(m_iSector + BucketMapSector + SectorOffset, 1);

            // Update length and return link
            BucketLength = BitConverter.ToUInt32(Sector, (int)((SectorIndex * 8) + 4));
            return BitConverter.ToUInt32(Sector, (int)(SectorIndex * 8));
        }

        /* SetNextBucket
         * Updates the link to the next bucket for the given bucket */
        void SetNextBucket(UInt32 Bucket, UInt32 NextBucket)
        {
            // Calculates the position of the bucket-map
            UInt64 Buckets = m_iSectorCount / m_iBucketSize;
            UInt64 BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            UInt64 BucketMapSector = (m_iSectorCount - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));

            // Calculate index into bucket map
            UInt32 BucketsPerSector = m_pDisk.BytesPerSector / 8;
            UInt32 SectorOffset = Bucket / BucketsPerSector;
            UInt32 SectorIndex = Bucket % BucketsPerSector;

            // Read the calculated sector
            Byte[] Sector = m_pDisk.Read(m_iSector + BucketMapSector + SectorOffset, 1);

            // Update link
            Sector[SectorIndex * 8] = (Byte)(NextBucket & 0xFF);
            Sector[SectorIndex * 8 + 1] = (Byte)((NextBucket >> 8) & 0xFF);
            Sector[SectorIndex * 8 + 2] = (Byte)((NextBucket >> 16) & 0xFF);
            Sector[SectorIndex * 8 + 3] = (Byte)((NextBucket >> 24) & 0xFF);

            // Flush buffer to disk
            m_pDisk.Write(Sector, m_iSector + BucketMapSector + SectorOffset, true);
        }

        /* FillBucketChain
         * Fill's the given bucket chain with the given data */
        void FillBucketChain(UInt32 Bucket, UInt32 BucketLength, Byte[] Data)
        {
            // Variables
            UInt32 BucketLengthItr = BucketLength;
            UInt32 BucketPtr = Bucket;
            Int64 Index = 0;

            // Iterate through the data and write it to the buckets
            while (Index < Data.LongLength) {
                Byte[] Buffer = new Byte[(m_iBucketSize * m_pDisk.BytesPerSector) * BucketLengthItr];

                // Copy the data to the buffer manually
                for (int i = 0; i < Buffer.Length; i++) {
                    if (Index + i >= Data.LongLength)
                        Buffer[i] = 0;
                    else
                        Buffer[i] = Data[Index + i];
                }

                // Increase the pointer
                Index += Buffer.Length;

                // Calculate which sector we should write the data too
                m_pDisk.Write(Buffer, m_iSector + (m_iBucketSize * BucketPtr), true);

                // Get next bucket cluster for writing
                BucketPtr = GetBucketLengthAndLink(BucketPtr, out BucketLengthItr);

                // Are we at end of pointer?
                if (BucketPtr == MFS_ENDOFCHAIN) {
                    break;
                }

                // Get length of new bucket
                GetBucketLengthAndLink(BucketPtr, out BucketLengthItr);
            }
        }

        /* CreateFileRecord
         * Creates a new file-record with the given flags and data, and name at in the given directory start bucket. 
         * Name must not be a path. */
        void CreateFileRecord(String Name, UInt32 Flags, UInt32 Bucket, UInt32 BucketLength, Byte[] Data, UInt32 DirectoryBucket)
        {
            // Variables
            UInt32 IteratorBucket = DirectoryBucket;
            UInt32 DirectoryLength = 0;
            int End = 0;

            // Iterate through directory and find a free record
            while (End == 0) {
                // Get length of bucket
                GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                // Calculate which bucket to read in
                UInt64 Sector = IteratorBucket * m_iBucketSize;
                Byte[] fBuffer = m_pDisk.Read(m_iSector + Sector, m_iBucketSize * DirectoryLength);

                // Iterate the bucket and find a free entry
                for (int i = 0; i < (m_iBucketSize * m_pDisk.BytesPerSector * DirectoryLength); i++) {
                    if (fBuffer[i] == 0) {
                        // Variables
                        UInt64 NumBuckets = 0;
                        UInt64 AllocatedSize = 0;
                        UInt64 DataLen = 0;

                        // Do we even need to write data?
                        if (Data != null) {
                            DataLen = (UInt64)Data.LongLength;
                            NumBuckets = (UInt64)(Data.LongLength / m_pDisk.BytesPerSector) / m_iBucketSize;
                            if (((Data.LongLength / m_pDisk.BytesPerSector) % m_iBucketSize) > 0)
                                NumBuckets++;
                            AllocatedSize = NumBuckets * m_iBucketSize * m_pDisk.BytesPerSector;
                        }

                        // Setup flags
                        fBuffer[i + 0] = (Byte)(Flags & 0xFF);
                        fBuffer[i + 1] = (Byte)((Flags >> 8) & 0xFF);
                        fBuffer[i + 2] = (Byte)((Flags >> 16) & 0xFF);
                        fBuffer[i + 3] = (Byte)((Flags >> 24) & 0xFF);

                        // Initialize start buckert and length
                        fBuffer[i + 4] = (Byte)(Bucket & 0xFF);
                        fBuffer[i + 5] = (Byte)((Bucket >> 8) & 0xFF);
                        fBuffer[i + 6] = (Byte)((Bucket >> 16) & 0xFF);
                        fBuffer[i + 7] = (Byte)((Bucket >> 24) & 0xFF);

                        fBuffer[i + 8] = (Byte)(BucketLength & 0xFF);
                        fBuffer[i + 9] = (Byte)((BucketLength >> 8) & 0xFF);
                        fBuffer[i + 10] = (Byte)((BucketLength >> 16) & 0xFF);
                        fBuffer[i + 11] = (Byte)((BucketLength >> 24) & 0xFF);

                        // Initialize data checksum
                        if (Data != null) {
                            uint Checksum = CalculateChecksum(Data, -1, 0);
                            fBuffer[i + 16] = (Byte)(Checksum & 0xFF);
                            fBuffer[i + 17] = (Byte)((Checksum >> 8) & 0xFF);
                            fBuffer[i + 18] = (Byte)((Checksum >> 16) & 0xFF);
                            fBuffer[i + 19] = (Byte)((Checksum >> 24) & 0xFF);
                        }

                        // Ignore time

                        // Sizes - 0x30
                        fBuffer[i + 48] = (Byte)(DataLen & 0xFF);
                        fBuffer[i + 49] = (Byte)((DataLen >> 8) & 0xFF);
                        fBuffer[i + 50] = (Byte)((DataLen >> 16) & 0xFF);
                        fBuffer[i + 51] = (Byte)((DataLen >> 24) & 0xFF);
                        fBuffer[i + 52] = (Byte)((DataLen >> 32) & 0xFF);
                        fBuffer[i + 53] = (Byte)((DataLen >> 40) & 0xFF);
                        fBuffer[i + 54] = (Byte)((DataLen >> 48) & 0xFF);
                        fBuffer[i + 55] = (Byte)((DataLen >> 56) & 0xFF);

                        fBuffer[i + 56] = (Byte)(AllocatedSize & 0xFF);
                        fBuffer[i + 57] = (Byte)((AllocatedSize >> 8) & 0xFF);
                        fBuffer[i + 58] = (Byte)((AllocatedSize >> 16) & 0xFF);
                        fBuffer[i + 59] = (Byte)((AllocatedSize >> 24) & 0xFF);
                        fBuffer[i + 60] = (Byte)((AllocatedSize >> 32) & 0xFF);
                        fBuffer[i + 61] = (Byte)((AllocatedSize >> 40) & 0xFF);
                        fBuffer[i + 62] = (Byte)((AllocatedSize >> 48) & 0xFF);
                        fBuffer[i + 63] = (Byte)((AllocatedSize >> 56) & 0xFF);
                        
                        // SparseMap
                        fBuffer[i + 64] = 0xFF;
                        fBuffer[i + 65] = 0xFF;
                        fBuffer[i + 66] = 0xFF;
                        fBuffer[i + 67] = 0xFF;

                        // Name at 68 + 300
                        Byte[] NameData = System.Text.Encoding.UTF8.GetBytes(Name);
                        for (int j = 0; j < NameData.Length; j++)
                            fBuffer[i + 68 + j] = NameData[j];

                        // Everything else 0
                        // Write new entry to disk and return
                        m_pDisk.Write(fBuffer, m_iSector + Sector, true);
                        return;
                    }

                    // Entries are 1K, skip with 1024-1
                    i += 1023;
                }

                // Store previous and get link
                UInt32 PreviousBucket = IteratorBucket;
                if (End == 0) {
                    IteratorBucket = GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
                }

                // If we reach end of directory we need to expand
                if (IteratorBucket == MFS_ENDOFCHAIN) {
                    Console.WriteLine("Directory - Expansion");
                    Byte[] Bootsector = m_pDisk.Read(m_iSector, 1);

                    // Get relevant locations
                    UInt64 MasterRecordSector = BitConverter.ToUInt64(Bootsector, 28);
                    UInt64 MasterRecordMirrorSector = BitConverter.ToUInt64(Bootsector, 36);

                    // Load master-record and get free entry
                    Byte[] MasterRecord = m_pDisk.Read(m_iSector + MasterRecordSector, 1);
                    UInt32 FreeBucket = BitConverter.ToUInt32(MasterRecord, 8);

                    /* Allocate bucket */
                    Console.WriteLine("New bucket: " + FreeBucket.ToString());
                    UInt32 NextFreeBucket = AllocateBuckets(FreeBucket, 4, out DirBucketLength);
                    Console.WriteLine("Next free bucket: " + NextFreeBucket.ToString());

                    /* Extend directory */
                    SetNextBucket(PreviousBucket, FreeBucket);

                    /* Update Mb(s) */
                    Mb[8] = (Byte)(NextFreeBucket & 0xFF);
                    Mb[9] = (Byte)((NextFreeBucket >> 8) & 0xFF);
                    Mb[10] = (Byte)((NextFreeBucket >> 16) & 0xFF);
                    Mb[11] = (Byte)((NextFreeBucket >> 24) & 0xFF);
                    WriteDisk(mDisk, MbSector, Mb, true);
                    WriteDisk(mDisk, MbMirrorSector, Mb, true);

                    /* Wipe the bucket */
                    Byte[] Wipe = new Byte[mDisk.BucketSize * mDisk.BytesPerSector];
                    Console.WriteLine("Wiping new bucket");
                    m_pDisk.Write(Wipe, m_iSector + (FreeBucket * m_iBucketSize), true);

                    /* Update IteratorBucket */
                    IteratorBucket = FreeBucket;
                }
            }
        }

        /* Recursive List */
        MfsEntry ListRecursive(MfsDisk mDisk, UInt32 DirBucket, String pPath)
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
        MfsEntry CreateRecursive(MfsDisk mDisk, UInt32 DirBucket, String pPath)
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
        
        /* Constructor
         * Zeroes out and initializes local members */
        public CMollenOSFileSystem(String PartitionName = "System")
        {
            // Initialize
            m_pDisk = null;
            m_iBucketSize = 0;
            m_szName = PartitionName;
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
            // if <1gb = 2 Kb (8 sectors) 
            // If <64gb = 4 Kb (16 sectors)
            // If >64gb = 8 Kb (32 sectors)
            // If >256gb = 16 Kb (64 sectors)
            if (DriveSizeBytes >= (256 * GigaByte))
                BucketSize = 64;
            else if (DriveSizeBytes >= (64 * GigaByte))
                BucketSize = 32;
            else if (DriveSizeBytes <= GigaByte)
                BucketSize = 8;
            else
                BucketSize = 16;

            // Save bucket size and debug
            m_iBucketSize = (UInt16)BucketSize;
            Console.WriteLine("Format - Bucket Size: " + BucketSize.ToString());

            // Sanitize that bootloaders are present
            if (!File.Exists("stage2.sys") || !File.Exists("stage1.sys"))
            {
                Console.WriteLine("Format - Bootloaders are missing (stage1.sys & stage2.sys)");
                return false;
            }

            // We need to calculate size of bootloader
            Byte[] Stage2Data = File.ReadAllBytes("stage2.sys");

            // Calculate reserved sector count
            Int64 ReservedSectors = 1 + ((Stage2Data.Length / m_pDisk.BytesPerSector) + 1);

            // Debug
            Console.WriteLine("Format - Reserved Sectors: " + ReservedSectors.ToString());

            // Setup Bucket-list
            // SectorCount / BucketSize
            // Each bucket must point to the next, untill we reach the end of buckets
            // Position at end of drive
            Buckets = m_iSectorCount / BucketSize;
            BucketMapSize = Buckets * 8; // One bucket descriptor is 8 bytes
            BucketMapSector = (m_iSectorCount - ((BucketMapSize / m_pDisk.BytesPerSector) + 1));
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
            Int64 ValToMove = (Int64)m_iSector + ((Int64)BucketMapSector * m_pDisk.BytesPerSector);
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

            // Allocate some start buckets to use for journaling etc
            UInt32 BucketStartFree = (UInt32)((MasterBucketSector / BucketSize) + 1);
            Console.WriteLine("Format - First Free Bucket: " + BucketStartFree.ToString());

            // Allocate for:
            // - Root directory - 8 buckets
            // - Bad-bucket list - 1 bucket
            // - Journal list - 8 buckets
            UInt32 InitialBucketSize = 0;
            UInt32 RootIndex = BucketStartFree;
            BucketStartFree = AllocateBuckets(BucketStartFree, 8, out InitialBucketSize);
            UInt32 JournalIndex = BucketStartFree;
            BucketStartFree = AllocateBuckets(BucketStartFree, 8, out InitialBucketSize);
            UInt32 BadBucketIndex = BucketStartFree;
            BucketStartFree = AllocateBuckets(BucketStartFree, 1, out InitialBucketSize);
            Console.WriteLine("Format - Free bucket pointer after setup: " + BucketStartFree.ToString());

            // Build a new master-record structure
            //uint32_t Magic;
            //uint32_t Flags;
            //uint32_t Checksum;      // Checksum of the master-record
            //uint8_t PartitionName[64];

            //uint32_t FreeBucket;        // Pointer to first free index
            //uint32_t RootIndex;     // Pointer to root directory
            //uint32_t BadBucketIndex;    // Pointer to list of bad buckets
            //uint32_t JournalIndex;  // Pointer to journal file

            //uint64_t MapSector;     // Start sector of bucket-map
            //uint64_t MapSize;		// Size of bucket map
            Byte[] MasterRecord = new Byte[512];

            // Initialize magic
            MasterRecord[0] = 0x4D;
            MasterRecord[1] = 0x46;
            MasterRecord[2] = 0x53;
            MasterRecord[3] = 0x31;

            // Flags are 0

            // Initialize partition name
            Byte[] NameBytes = Encoding.UTF8.GetBytes(m_szName);
            Array.Copy(NameBytes, 0, MasterRecord, 12, NameBytes.Length);

            // Initialize free pointer
            MasterRecord[76] = (Byte)(BucketStartFree & 0xFF);
            MasterRecord[77] = (Byte)((BucketStartFree >> 8) & 0xFF);
            MasterRecord[78] = (Byte)((BucketStartFree >> 16) & 0xFF);
            MasterRecord[79] = (Byte)((BucketStartFree >> 24) & 0xFF);

            // Initialize root directory pointer
            MasterRecord[80] = (Byte)(RootIndex & 0xFF);
            MasterRecord[81] = (Byte)((RootIndex >> 8) & 0xFF);
            MasterRecord[82] = (Byte)((RootIndex >> 16) & 0xFF);
            MasterRecord[83] = (Byte)((RootIndex >> 24) & 0xFF);

            // Initialize bad bucket list pointer
            MasterRecord[84] = (Byte)(BadBucketIndex & 0xFF);
            MasterRecord[85] = (Byte)((BadBucketIndex >> 8) & 0xFF);
            MasterRecord[86] = (Byte)((BadBucketIndex >> 16) & 0xFF);
            MasterRecord[87] = (Byte)((BadBucketIndex >> 24) & 0xFF);

            // Initialize journal list pointer
            MasterRecord[88] = (Byte)(JournalIndex & 0xFF);
            MasterRecord[89] = (Byte)((JournalIndex >> 8) & 0xFF);
            MasterRecord[90] = (Byte)((JournalIndex >> 16) & 0xFF);
            MasterRecord[91] = (Byte)((JournalIndex >> 24) & 0xFF);

            // Initialize map sector pointer
            MasterRecord[92] = (Byte)(BucketMapSector & 0xFF);
            MasterRecord[93] = (Byte)((BucketMapSector >> 8) & 0xFF);
            MasterRecord[94] = (Byte)((BucketMapSector >> 16) & 0xFF);
            MasterRecord[95] = (Byte)((BucketMapSector >> 24) & 0xFF);
            MasterRecord[96] = (Byte)((BucketMapSector >> 32) & 0xFF);
            MasterRecord[97] = (Byte)((BucketMapSector >> 40) & 0xFF);
            MasterRecord[98] = (Byte)((BucketMapSector >> 48) & 0xFF);
            MasterRecord[99] = (Byte)((BucketMapSector >> 56) & 0xFF);

            // Initialize map size
            MasterRecord[100] = (Byte)(BucketMapSize & 0xFF);
            MasterRecord[101] = (Byte)((BucketMapSize >> 8) & 0xFF);
            MasterRecord[102] = (Byte)((BucketMapSize >> 16) & 0xFF);
            MasterRecord[103] = (Byte)((BucketMapSize >> 24) & 0xFF);
            MasterRecord[104] = (Byte)((BucketMapSize >> 32) & 0xFF);
            MasterRecord[105] = (Byte)((BucketMapSize >> 40) & 0xFF);
            MasterRecord[106] = (Byte)((BucketMapSize >> 48) & 0xFF);
            MasterRecord[107] = (Byte)((BucketMapSize >> 56) & 0xFF);

            // Initialize checksum
            uint Checksum = CalculateChecksum(MasterRecord, 8, 4);
            MasterRecord[8] = (Byte)(Checksum & 0xFF);
            MasterRecord[9] = (Byte)((Checksum >> 8) & 0xFF);
            MasterRecord[10] = (Byte)((Checksum >> 16) & 0xFF);
            MasterRecord[11] = (Byte)((Checksum >> 24) & 0xFF);

            // Flush it to disk
            m_pDisk.Write(MasterRecord, m_iSector + (UInt64)MasterBucketSector, true);
            m_pDisk.Write(MasterRecord, m_iSector + MirrorMasterBucketSector, true);

            // Allocate a zero array to fill the allocated sectors with
            Byte[] Wipe = new Byte[BucketSize * m_pDisk.BytesPerSector];

            Console.WriteLine("Format - wiping bad bucket list");
            m_pDisk.Write(Wipe, m_iSector + (BadBucketIndex * BucketSize), true);

            Wipe = new Byte[(BucketSize * m_pDisk.BytesPerSector) * 8];

            Console.WriteLine("Format - wiping root directory");
            m_pDisk.Write(Wipe, m_iSector + (RootIndex * BucketSize), true);

            Console.WriteLine("Format - wiping journal list");
            m_pDisk.Write(Wipe, m_iSector + (JournalIndex * BucketSize), true);

            // Last step is to update the bootsector
            Console.WriteLine("Format - updating bootsector");

            // Initialize the MBR
            //uint8_t JumpCode[3];
            //uint32_t Magic;
            //uint8_t Version;
            //uint8_t Flags;
            //uint8_t MediaType;
            //uint16_t SectorSize;
            //uint16_t SectorsPerTrack;
            //uint16_t HeadsPerCylinder;
            //uint64_t SectorCount;
            //uint16_t ReservedSectors;
            //uint16_t SectorsPerBucket;
            //uint64_t MasterRecordSector;
            //uint64_t MasterRecordMirror;
            Byte[] Bootsector = new Byte[m_pDisk.BytesPerSector];

            // Initialize magic
            Bootsector[3] = 0x4D;
            Bootsector[4] = 0x46;
            Bootsector[5] = 0x53;
            Bootsector[6] = 0x31;

            // Initialize version
            Bootsector[7] = 0x1;

            // Initialize disk metrics
            Bootsector[9] = 0x80;
            Bootsector[10] = (Byte)(m_pDisk.BytesPerSector & 0xFF);
            Bootsector[11] = (Byte)((m_pDisk.BytesPerSector >> 8) & 0xFF);

            // Sectors per track
            Bootsector[12] = (Byte)(m_pDisk.SectorsPerTrack & 0xFF);
            Bootsector[13] = (Byte)((m_pDisk.SectorsPerTrack >> 8) & 0xFF);

            // Heads per cylinder
            Bootsector[14] = (Byte)(m_pDisk.TracksPerCylinder & 0xFF);
            Bootsector[15] = (Byte)((m_pDisk.TracksPerCylinder >> 8) & 0xFF);

            // Total sectors on partition
            Bootsector[16] = (Byte)(m_iSectorCount & 0xFF);
            Bootsector[17] = (Byte)((m_iSectorCount >> 8) & 0xFF);
            Bootsector[18] = (Byte)((m_iSectorCount >> 16) & 0xFF);
            Bootsector[19] = (Byte)((m_iSectorCount >> 24) & 0xFF);
            Bootsector[20] = (Byte)((m_iSectorCount >> 32) & 0xFF);
            Bootsector[21] = (Byte)((m_iSectorCount >> 40) & 0xFF);
            Bootsector[22] = (Byte)((m_iSectorCount >> 48) & 0xFF);
            Bootsector[23] = (Byte)((m_iSectorCount >> 56) & 0xFF);

            // Reserved sectors
            Bootsector[24] = (Byte)(ReservedSectors & 0xFF);
            Bootsector[25] = (Byte)((ReservedSectors >> 8) & 0xFF);

            // Size of an bucket in sectors
            Bootsector[26] = (Byte)(BucketSize & 0xFF);
            Bootsector[27] = (Byte)((BucketSize >> 8) & 0xFF);

            // Bucket of master-record
            Bootsector[28] = (Byte)(MasterBucketSector & 0xFF);
            Bootsector[29] = (Byte)((MasterBucketSector >> 8) & 0xFF);
            Bootsector[30] = (Byte)((MasterBucketSector >> 16) & 0xFF);
            Bootsector[31] = (Byte)((MasterBucketSector >> 24) & 0xFF);
            Bootsector[32] = (Byte)((MasterBucketSector >> 32) & 0xFF);
            Bootsector[33] = (Byte)((MasterBucketSector >> 40) & 0xFF);
            Bootsector[34] = (Byte)((MasterBucketSector >> 48) & 0xFF);
            Bootsector[35] = (Byte)((MasterBucketSector >> 56) & 0xFF);

            // Bucket of master-record mirror
            Bootsector[36] = (Byte)(MirrorMasterBucketSector & 0xFF);
            Bootsector[37] = (Byte)((MirrorMasterBucketSector >> 8) & 0xFF);
            Bootsector[38] = (Byte)((MirrorMasterBucketSector >> 16) & 0xFF);
            Bootsector[39] = (Byte)((MirrorMasterBucketSector >> 24) & 0xFF);
            Bootsector[40] = (Byte)((MirrorMasterBucketSector >> 32) & 0xFF);
            Bootsector[41] = (Byte)((MirrorMasterBucketSector >> 40) & 0xFF);
            Bootsector[42] = (Byte)((MirrorMasterBucketSector >> 48) & 0xFF);
            Bootsector[43] = (Byte)((MirrorMasterBucketSector >> 56) & 0xFF);

            // Flush to disk
            m_pDisk.Write(Bootsector, m_iSector, true);

            // Done!
            return true;
        }

        /* MakeBoot
         * Readies the filesystem for being the primary bootable filesystem by preparing a bootsector */
        public bool MakeBoot()
        {
            // Sanitize variables
            if (m_pDisk == null)
                return false;

            // Load up boot-sector
            Console.WriteLine("Install - loading bootsector (stage1.sys)");
            Byte[] Bootsector = File.ReadAllBytes("stage1.sys");

            // Modify boot-sector by preserving the header
            Byte[] Existing = m_pDisk.Read(m_iSector, 1);
            Buffer.BlockCopy(Existing, 3, Bootsector, 3, 49);

            // Mark the partition as os-partition
            Bootsector[8] = 0x1;

            // Flush the modified sector back to disk
            Console.WriteLine("Install - writing bootsector to disk");
            m_pDisk.Write(Bootsector, m_iSector, true);

            // Write stage2 to disk
            Console.WriteLine("Install - loading stage2 (stage2.sys)");
            Byte[] Stage2Data = File.ReadAllBytes("stage2.sys");

            // Make sure we allocate a sector-aligned buffer
            Console.WriteLine("Install - writing stage2 to disk");
            Byte[] ReservedBuffer = new Byte[((Stage2Data.Length / m_pDisk.BytesPerSector) + 1) * m_pDisk.BytesPerSector];
            Stage2Data.CopyTo(ReservedBuffer, 0);
            m_pDisk.Write(ReservedBuffer, m_iSector + 1, true);

            // Done
            return true;
        }

        /* ListDirectory
         * List's the contents of the given path - that must be a directory path */
        public bool ListDirectory(String Path)
        {
            // Sanitize variables
            if (m_pDisk == null)
                return false;

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

            // Done
            return true;
        }

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        public bool WriteFile(String Path, FileFlags Flags, Byte[] Data)
        {
            // Sanitize variables
            if (m_pDisk == null)
                return false;

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

            // Done
            return true;
        }
    }
}
