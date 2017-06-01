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
        [Flags]
        public enum RecordFlags : uint
        {
            Directory       = 0x1,
            Link            = 0x2,
            Security        = 0x4,
            System          = 0x8,
            Hidden          = 0x10,
            Chained         = 0x20,
            Locked          = 0x40,

            Versioned       = 0x10000000,
            Inline          = 0x20000000,
            Sparse          = 0x40000000,
            InUse           = 0x80000000
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
        void CreateFileRecord(String Name, RecordFlags Flags, UInt32 Bucket, UInt32 BucketLength, Byte[] Data, UInt32 DirectoryBucket)
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
                        uint iFlags = (uint)Flags;
                        fBuffer[i + 0] = (Byte)(iFlags & 0xFF);
                        fBuffer[i + 1] = (Byte)((iFlags >> 8) & 0xFF);
                        fBuffer[i + 2] = (Byte)((iFlags >> 16) & 0xFF);
                        fBuffer[i + 3] = (Byte)((iFlags >> 24) & 0xFF);

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
                        Byte[] NameData = Encoding.UTF8.GetBytes(Name);
                        for (int j = 0; j < NameData.Length; j++)
                            fBuffer[i + 68 + j] = NameData[j];

                        // Everything else 0
                        // Write new entry to disk and return
                        m_pDisk.Write(fBuffer, m_iSector + Sector, true);
                        Console.WriteLine("  - Writing " + fBuffer.Length.ToString() + " bytes to disk at sector " + (m_iSector + Sector).ToString());
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
                    UInt32 Allocation = BitConverter.ToUInt32(MasterRecord, 76);
                    UInt32 AllocationLength = 0;

                    // Allocate a bunch of new buckets for expansion
                    UInt32 UpdatedFreePointer = AllocateBuckets(Allocation, 4, out AllocationLength);
                    SetNextBucket(PreviousBucket, Allocation);

                    // Update the master-record to reflect the new index
                    MasterRecord[76] = (Byte)(UpdatedFreePointer & 0xFF);
                    MasterRecord[77] = (Byte)((UpdatedFreePointer >> 8) & 0xFF);
                    MasterRecord[78] = (Byte)((UpdatedFreePointer >> 16) & 0xFF);
                    MasterRecord[79] = (Byte)((UpdatedFreePointer >> 24) & 0xFF);
                    m_pDisk.Write(MasterRecord, m_iSector + MasterRecordSector, true);
                    m_pDisk.Write(MasterRecord, m_iSector + MasterRecordMirrorSector, true);

                    // Wipe the new allocated directory block
                    Console.WriteLine("Directory - Wipe");
                    Byte[] Wipe = new Byte[m_iBucketSize * m_pDisk.BytesPerSector * AllocationLength];
                    m_pDisk.Write(Wipe, m_iSector + (Allocation * m_iBucketSize), true);

                    // Update iterator
                    IteratorBucket = Allocation;
                }
            }
        }

        /* ListRecursive 
         * Has two purposes - either it recursively lists the entries of end directory or it
         * can locate a file if the path contains '.' and return it's placement */
        MfsRecord ListRecursive(UInt32 DirectoryBucket, String LocalPath, Boolean Verbose = true)
        {
            // Sanitize path, if it starts with / skip it
            String mPath = LocalPath;
            if (mPath.StartsWith("/"))
                mPath = mPath.Substring(1, mPath.Length - 1);

            // Extract the next token we are looking for
            int iDex = mPath.IndexOf("/");
            String LookFor = mPath.Substring(0, iDex == -1 ? mPath.Length : iDex);

            // Detect end of path
            if (String.IsNullOrEmpty(LookFor) || LookFor.Contains(".")) {
                // Variables
                UInt32 IteratorBucket = DirectoryBucket;
                UInt32 DirectoryLength = 0;
                int End = 0;

                while (End == 0) {
                    // Get length of bucket
                    GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    UInt64 Sector = IteratorBucket * m_iBucketSize;
                    Byte[] fBuffer = m_pDisk.Read(m_iSector + Sector, m_iBucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (int i = 0; i < (m_iBucketSize * m_pDisk.BytesPerSector * DirectoryLength); i++) {
                        if (fBuffer[i] == 0) {
                            i += 1023;
                            continue;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (fBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(fBuffer, i + 68, Len);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(fBuffer, i);

                        // Have we found the record we were looking for?
                        if (LookFor.Contains(".")
                            && Name.ToLower() == LookFor.ToLower()) {
                            MfsRecord nEntry = new MfsRecord();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(fBuffer, i + 48);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(fBuffer, i + 56);
                            nEntry.Bucket = BitConverter.ToUInt32(fBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(fBuffer, i + 8);

                            nEntry.DirectoryBucket = IteratorBucket;
                            nEntry.DirectoryLength = DirectoryLength;
                            nEntry.DirectoryIndex = (uint)i;

                            // Done - we found the record
                            return nEntry;
                        }
                        else {
                            if (Flags.HasFlag(RecordFlags.Directory)) {
                                if (Verbose) {
                                    Console.WriteLine("Dir: " + Name);
                                }
                            }
                            else {
                                if (Verbose) {
                                    Console.WriteLine("File: " + Name + " (" + BitConverter.ToUInt64(fBuffer, i + 48).ToString() + " Bytes)");
                                }
                            }
                            
                        }

                        // Advance to next entry
                        i += 1023;
                    }

                    // Get next bucket link
                    if (End == 0) {
                        IteratorBucket = GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
                    }
                    
                    // Have we reached end?
                    if (IteratorBucket == MFS_ENDOFCHAIN) {
                        End = 1;
                        break;
                    }
                }

                // Success
                return null;
            }
            else {
                // Variables
                UInt32 IteratorBucket = DirectoryBucket;
                UInt32 DirectoryLength = 0;
                int End = 0;

                while (End == 0) {
                    // Get length of bucket
                    GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    UInt64 Sector = IteratorBucket * m_iBucketSize;
                    Byte[] fBuffer = m_pDisk.Read(m_iSector + Sector, m_iBucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (int i = 0; i < (m_iBucketSize * m_pDisk.BytesPerSector * DirectoryLength); i++) {
                        if (fBuffer[i] == 0) {
                            i += 1023;
                            continue;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (fBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(fBuffer, i + 68, Len);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(fBuffer, i);

                        // Have we found the record we were looking for?
                        // This entry must be a directory
                        if (Name.ToLower() == LookFor.ToLower()) {
                            if (!Flags.HasFlag(RecordFlags.Directory)) {
                                Console.WriteLine(LookFor + " is not a directory");
                                return null;
                            }

                            // Create a new record
                            MfsRecord nEntry = new MfsRecord();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(fBuffer, i + 48);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(fBuffer, i + 56);
                            nEntry.Bucket = BitConverter.ToUInt32(fBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(fBuffer, i + 8);

                            nEntry.DirectoryBucket = IteratorBucket;
                            nEntry.DirectoryLength = DirectoryLength;
                            nEntry.DirectoryIndex = (uint)i;

                            // Sanitize - directory must have data allocated
                            if (nEntry.Bucket == MFS_ENDOFCHAIN) {
                                return null;
                            }
                            
                            // Go further down the rabbit-hole
                            return ListRecursive(nEntry.Bucket, mPath.Substring(LookFor.Length), Verbose);
                        }

                        // Advance to next entry
                        i += 1023;
                    }

                    // Get next bucket link
                    if (End == 0) {
                        IteratorBucket = GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
                    }
                    
                    // Have we reached end?
                    if (IteratorBucket == MFS_ENDOFCHAIN) {
                        End = 1;
                        break;
                    }
                }
            }

            // Success
            return null;
        }

        /* CreateRecursive 
         * Recursively iterates through the path and creates the path. 
         * If path exists nothing happens */
        MfsRecord CreateRecursive(UInt32 DirectoryBucket, String LocalPath)
        {
            /* Sanity, if start with "/" skip */
            String mPath = LocalPath;
            if (mPath.StartsWith("/"))
                mPath = mPath.Substring(1, mPath.Length - 1);
            if (mPath.EndsWith("/"))
                mPath = mPath.Substring(0, mPath.Length - 1);

            // Get token
            int iDex = mPath.IndexOf("/");
            String LookFor = mPath.Substring(0, iDex == -1 ? mPath.Length : iDex);

            // Handle end of path
            if (String.IsNullOrEmpty(LookFor) || iDex == -1) {
                // Variables
                UInt32 IteratorBucket = DirectoryBucket, PreviousBucket = MFS_ENDOFCHAIN;
                UInt32 DirectoryLength = 0;
                int End = 0;
                int i = 0;

                while (End == 0) {
                    // Get length of bucket
                    GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    UInt64 Sector = IteratorBucket * m_iBucketSize;
                    Byte[] fBuffer = m_pDisk.Read(m_iSector + Sector, m_iBucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (i = 0; i < (m_iBucketSize * m_pDisk.BytesPerSector * DirectoryLength); i++) {
                        if (fBuffer[i] == 0) {
                            End = 1;
                            break;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (fBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(fBuffer, i + 68, Len);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(fBuffer, i);

                        // Does it exist already?
                        if (Name.ToLower() == LookFor.ToLower()) {
                            Console.WriteLine("Creation - Entry did exist already");
                            return null;
                        }

                        // Advance to next entry
                        i += 1023;
                    }

                    // Handle the case where we found free entry
                    if (End == 1) {
                        break;
                    }
                    
                    // Get next bucket link
                    if (End == 0) {
                        PreviousBucket = IteratorBucket;
                        IteratorBucket = GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
                    }
                    
                    // Have we reached end?
                    if (IteratorBucket == MFS_ENDOFCHAIN) {
                        End = 1;
                    }
                }

                // Must reach here
                MfsRecord nEntry = new MfsRecord();
                nEntry.DirectoryBucket = (IteratorBucket == MFS_ENDOFCHAIN) ? PreviousBucket : IteratorBucket;
                nEntry.DirectoryLength = DirectoryLength;
                nEntry.DirectoryIndex = (uint)i;
                return nEntry;
            }
            else {
                // Variables
                UInt32 IteratorBucket = DirectoryBucket;
                UInt32 DirectoryLength = 0;
                int End = 0;

                while (End == 0) {
                    // Get length of bucket
                    GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    UInt64 Sector = IteratorBucket * m_iBucketSize;
                    Byte[] fBuffer = m_pDisk.Read(m_iSector + Sector, m_iBucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (int i = 0; i < (m_iBucketSize * m_pDisk.BytesPerSector * DirectoryLength); i++) {
                        if (fBuffer[i] == 0) {
                            i += 1023;
                            continue;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (fBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(fBuffer, i + 68, Len);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(fBuffer, i);

                        // Have we found the record we were looking for?
                        // This entry must be a directory
                        if (Name.ToLower() == LookFor.ToLower()) {
                            if (!Flags.HasFlag(RecordFlags.Directory)) {
                                Console.WriteLine(LookFor + " is not a directory");
                                return null;
                            }

                            // Create a new record
                            MfsRecord nEntry = new MfsRecord();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(fBuffer, i + 48);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(fBuffer, i + 56);
                            nEntry.Bucket = BitConverter.ToUInt32(fBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(fBuffer, i + 8);

                            nEntry.DirectoryBucket = IteratorBucket;
                            nEntry.DirectoryLength = DirectoryLength;
                            nEntry.DirectoryIndex = (uint)i;

                            // Sanitize the case where we need to expand
                            if (nEntry.Bucket == MFS_ENDOFCHAIN) {
                                // Read bootsector
                                Byte[] Bootsector = m_pDisk.Read(m_iSector, 1);

                                // Load some data (master-record and bucket-size)
                                UInt64 MasterRecordSector = BitConverter.ToUInt64(Bootsector, 28);
                                UInt64 MasterRecordMirrorSector = BitConverter.ToUInt64(Bootsector, 36);
                                m_iBucketSize = BitConverter.ToUInt16(Bootsector, 26);

                                // Read master-record
                                Byte[] MasterRecord = m_pDisk.Read(m_iSector + MasterRecordSector, 1);
                                UInt32 FreeBucket = BitConverter.ToUInt32(MasterRecord, 76);

                                // Allocate new buckets
                                nEntry.Bucket = FreeBucket;
                                UInt32 InitialBucketSize = 0;
                                UInt32 NextFree = AllocateBuckets(FreeBucket, 4, out InitialBucketSize);

                                // Update the master-record
                                MasterRecord[76] = (Byte)(NextFree & 0xFF);
                                MasterRecord[77] = (Byte)((NextFree >> 8) & 0xFF);
                                MasterRecord[78] = (Byte)((NextFree >> 16) & 0xFF);
                                MasterRecord[79] = (Byte)((NextFree >> 24) & 0xFF);
                                m_pDisk.Write(MasterRecord, m_iSector + MasterRecordSector, true);
                                m_pDisk.Write(MasterRecord, m_iSector + MasterRecordMirrorSector, true);

                                // Update the file-record
                                fBuffer[i + 4] = (Byte)(nEntry.Bucket & 0xFF);
                                fBuffer[i + 5] = (Byte)((nEntry.Bucket >> 8) & 0xFF);
                                fBuffer[i + 6] = (Byte)((nEntry.Bucket >> 16) & 0xFF);
                                fBuffer[i + 7] = (Byte)((nEntry.Bucket >> 24) & 0xFF);

                                fBuffer[i + 8] = (Byte)(InitialBucketSize & 0xFF);
                                fBuffer[i + 9] = (Byte)((InitialBucketSize >> 8) & 0xFF);
                                fBuffer[i + 10] = (Byte)((InitialBucketSize >> 16) & 0xFF);
                                fBuffer[i + 11] = (Byte)((InitialBucketSize >> 24) & 0xFF);

                                m_pDisk.Write(fBuffer, m_iSector + Sector, true);

                                // Wipe directory bucket
                                Byte[] Wipe = new Byte[m_iBucketSize * m_pDisk.BytesPerSector * InitialBucketSize];
                                m_pDisk.Write(Wipe, m_iSector + (nEntry.Bucket * m_iBucketSize), true);
                            }

                            // Go further down the rabbit hole
                            return CreateRecursive(nEntry.Bucket, mPath.Substring(LookFor.Length));
                        }

                        // Advance to next entry
                        i += 1023;
                    }

                    // Get next bucket link
                    if (End == 0) {
                        IteratorBucket = GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
                    }
                    
                    // Have we reached end?
                    if (IteratorBucket == MFS_ENDOFCHAIN) {
                        End = 1;
                    }
                }
            }

            // Creation failed
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
            // if <1gb = 4 Kb (8 sectors) 
            // If <64gb = 8 Kb (16 sectors)
            // If >64gb = 16 Kb (32 sectors)
            // If >256gb = 32 Kb (64 sectors)
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
            if (!File.Exists("deploy/stage2.sys") || !File.Exists("deploy/stage1.sys"))
            {
                Console.WriteLine("Format - Bootloaders are missing (stage1.sys & stage2.sys)");
                return false;
            }

            // We need to calculate size of bootloader
            Byte[] Stage2Data = File.ReadAllBytes("deploy/stage2.sys");

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
            Byte[] Bootsector = File.ReadAllBytes("deploy/stage1.sys");

            // Modify boot-sector by preserving the header 44
            Byte[] Existing = m_pDisk.Read(m_iSector, 1);
            Buffer.BlockCopy(Existing, 3, Bootsector, 3, 41);

            // Mark the partition as os-partition
            Bootsector[8] = 0x1;

            // Flush the modified sector back to disk
            Console.WriteLine("Install - writing bootsector to disk");
            m_pDisk.Write(Bootsector, m_iSector, true);

            // Write stage2 to disk
            Console.WriteLine("Install - loading stage2 (stage2.sys)");
            Byte[] Stage2Data = File.ReadAllBytes("deploy/stage2.sys");

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

            // Read bootsector
            Byte[] Bootsector = m_pDisk.Read(m_iSector, 1);

            // Load some data (master-record and bucket-size)
            UInt64 MasterRecordSector = BitConverter.ToUInt64(Bootsector, 28);
            m_iBucketSize = BitConverter.ToUInt16(Bootsector, 26);

            // Read master-record
            Byte[] MasterRecord = m_pDisk.Read(m_iSector + MasterRecordSector, 1);
            UInt32 RootBucket = BitConverter.ToUInt32(MasterRecord, 80);

            // Call our recursive function to list everything
            Console.WriteLine("Files in " + Path + ":");
            ListRecursive(RootBucket, Path);
            Console.WriteLine("");

            // Done
            return true;
        }

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        public bool WriteFile(String LocalPath, FileFlags Flags, Byte[] Data)
        {
            // Sanitize variables
            if (m_pDisk == null)
                return false;

            // Read bootsector
            Byte[] Bootsector = m_pDisk.Read(m_iSector, 1);

            // Load some data (master-record and bucket-size)
            UInt64 MasterRecordSector = BitConverter.ToUInt64(Bootsector, 28);
            UInt64 MasterRecordMirrorSector = BitConverter.ToUInt64(Bootsector, 36);
            m_iBucketSize = BitConverter.ToUInt16(Bootsector, 26);

            // Read master-record
            Byte[] MasterRecord = m_pDisk.Read(m_iSector + MasterRecordSector, 1);
            UInt32 FreeBucket = BitConverter.ToUInt32(MasterRecord, 76);
            UInt32 RootBucket = BitConverter.ToUInt32(MasterRecord, 80);
            UInt64 SectorsRequired = 0;
            UInt64 BucketsRequired = 0;

            // Sanitize
            if (Data != null) {
                // Calculate number of sectors required
                SectorsRequired = (ulong)Data.LongLength / m_pDisk.BytesPerSector;
                if (((ulong)Data.LongLength % m_pDisk.BytesPerSector) > 0)
                    SectorsRequired++;

                // Calculate the number of buckets required
                BucketsRequired = SectorsRequired / m_iBucketSize;
                if ((SectorsRequired % m_iBucketSize) > 0)
                    BucketsRequired++;
            }

            // Try to locate if the record exists already
            // Because if it exists - then we update it
            // If it does not exist - we then create it
            MfsRecord nEntry = ListRecursive(RootBucket, LocalPath, false);
            if (nEntry != null) {
                Console.WriteLine("File exists in table, updating");

                // Handle expansion if we are trying to write more than what
                // is currently allocated
                if ((UInt64)Data.LongLength > nEntry.AllocatedSize) {

                    // Calculate only the difference in allocation size
                    UInt64 NumSectors = ((UInt64)Data.LongLength - nEntry.AllocatedSize) / m_pDisk.BytesPerSector;
                    if ((((UInt64)Data.LongLength - nEntry.AllocatedSize) % m_pDisk.BytesPerSector) > 0)
                        NumSectors++;
                    UInt64 NumBuckets = NumSectors / m_iBucketSize;
                    if ((NumSectors % m_iBucketSize) > 0)
                        NumBuckets++;

                    // Do the allocation
                    Console.WriteLine("  - allocating " + NumBuckets.ToString() + " buckets");
                    Console.WriteLine("  - old free pointer " + FreeBucket.ToString());
                    UInt32 StartBucket = FreeBucket;
                    UInt32 InitialBucketSize = 0;
                    FreeBucket = AllocateBuckets(FreeBucket, NumBuckets, out InitialBucketSize);
                    Console.WriteLine("  - new free pointer " + FreeBucket.ToString());

                    // Iterate to end of data chain, but keep a pointer to the previous
                    UInt32 BucketPtr = nEntry.Bucket;
                    UInt32 BucketPrevPtr = 0;
                    UInt32 BucketLength = 0;
                    while (BucketPtr != MFS_ENDOFCHAIN) {
                        BucketPrevPtr = BucketPtr;
                        BucketPtr = GetBucketLengthAndLink(BucketPtr, out BucketLength);
                    }

                    // Update the last link to the newly allocated
                    SetNextBucket(BucketPrevPtr, FreeBucket);

                    // Update the master-record
                    MasterRecord[76] = (Byte)(FreeBucket & 0xFF);
                    MasterRecord[77] = (Byte)((FreeBucket >> 8) & 0xFF);
                    MasterRecord[78] = (Byte)((FreeBucket >> 16) & 0xFF);
                    MasterRecord[79] = (Byte)((FreeBucket >> 24) & 0xFF);
                    m_pDisk.Write(MasterRecord, m_iSector + MasterRecordSector, true);
                    m_pDisk.Write(MasterRecord, m_iSector + MasterRecordMirrorSector, true);

                    // Update the allocated size in cached
                    nEntry.AllocatedSize += (NumBuckets * m_iBucketSize * m_pDisk.BytesPerSector);
                }

                // We should free buckets that are not used here if data size is less
                Console.WriteLine("  - updating data for file");
                FillBucketChain(nEntry.Bucket, nEntry.BucketLength, Data);

                // Read the in the relevant bucket for directory
                Byte[] fBuffer = m_pDisk.Read(m_iSector + (nEntry.DirectoryBucket * m_iBucketSize), 
                    m_iBucketSize * nEntry.DirectoryLength);

                // Update fields
                fBuffer[nEntry.DirectoryIndex + 4] = (Byte)(nEntry.Bucket & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 5] = (Byte)((nEntry.Bucket >> 8) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 6] = (Byte)((nEntry.Bucket >> 16) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 7] = (Byte)((nEntry.Bucket >> 24) & 0xFF);

                fBuffer[nEntry.DirectoryIndex + 8] = (Byte)(nEntry.BucketLength & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 9] = (Byte)((nEntry.BucketLength >> 8) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 10] = (Byte)((nEntry.BucketLength >> 16) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 11] = (Byte)((nEntry.BucketLength >> 24) & 0xFF);

                fBuffer[nEntry.DirectoryIndex + 48] = (Byte)(Data.LongLength & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 49] = (Byte)((Data.LongLength >> 8) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 50] = (Byte)((Data.LongLength >> 16) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 51] = (Byte)((Data.LongLength >> 24) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 52] = (Byte)((Data.LongLength >> 32) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 53] = (Byte)((Data.LongLength >> 40) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 54] = (Byte)((Data.LongLength >> 48) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 55] = (Byte)((Data.LongLength >> 56) & 0xFF);

                fBuffer[nEntry.DirectoryIndex + 56] = (Byte)(nEntry.AllocatedSize & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 57] = (Byte)((nEntry.AllocatedSize >> 8) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 58] = (Byte)((nEntry.AllocatedSize >> 16) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 59] = (Byte)((nEntry.AllocatedSize >> 24) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 60] = (Byte)((nEntry.AllocatedSize >> 32) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 61] = (Byte)((nEntry.AllocatedSize >> 40) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 62] = (Byte)((nEntry.AllocatedSize >> 48) & 0xFF);
                fBuffer[nEntry.DirectoryIndex + 63] = (Byte)((nEntry.AllocatedSize >> 56) & 0xFF);

                // Flush the modified directory back to disk
                m_pDisk.Write(fBuffer, m_iSector + (nEntry.DirectoryBucket * m_iBucketSize), true);
            }
            else {
                Console.WriteLine("/" + LocalPath + " is a new " 
                    + (Flags.HasFlag(FileFlags.Directory) ? "directory" : "file"));
                MfsRecord cInfo = CreateRecursive(RootBucket, LocalPath);
                if (cInfo == null) {
                    Console.WriteLine("The creation info returned null, somethings wrong");
                    return false;
                }

                Console.WriteLine("  - room in bucket " + cInfo.DirectoryBucket.ToString() + " at index " + cInfo.DirectoryIndex.ToString());

                // Reload master-record and update free-bucket variable 
                // as it could have changed when expanding directory
                UInt32 StartBucket = MFS_ENDOFCHAIN;
                UInt32 InitialBucketSize = 0;
                if (Data != null) {
                    MasterRecord = m_pDisk.Read(m_iSector + MasterRecordSector, 1);
                    FreeBucket = BitConverter.ToUInt32(MasterRecord, 76);

                    // Do the allocation
                    Console.WriteLine("  - allocating " + BucketsRequired.ToString() + " buckets");
                    Console.WriteLine("  - old free pointer " + FreeBucket.ToString());
                    StartBucket = FreeBucket;
                    FreeBucket = AllocateBuckets(FreeBucket, BucketsRequired, out InitialBucketSize);
                    Console.WriteLine("  - new free pointer " + FreeBucket.ToString());

                    // Update the master-record
                    MasterRecord[76] = (Byte)(FreeBucket & 0xFF);
                    MasterRecord[77] = (Byte)((FreeBucket >> 8) & 0xFF);
                    MasterRecord[78] = (Byte)((FreeBucket >> 16) & 0xFF);
                    MasterRecord[79] = (Byte)((FreeBucket >> 24) & 0xFF);
                    m_pDisk.Write(MasterRecord, m_iSector + MasterRecordSector, true);
                    m_pDisk.Write(MasterRecord, m_iSector + MasterRecordMirrorSector, true);
                }
                
                // Build flags
                RecordFlags rFlags = RecordFlags.InUse | RecordFlags.Chained;
                if (Flags.HasFlag(FileFlags.Directory))
                    rFlags |= RecordFlags.Directory;
                if (Flags.HasFlag(FileFlags.System))
                    rFlags |= RecordFlags.System;

                // Create entry in base directory
                Console.WriteLine("  - creating directory entry");
                CreateFileRecord(Path.GetFileName(LocalPath), rFlags, StartBucket, InitialBucketSize, Data, cInfo.DirectoryBucket);

                // Now fill the allocated buckets with data
                if (Data != null) {
                    Console.WriteLine("  - writing file-data");
                    FillBucketChain(StartBucket, InitialBucketSize, Data);
                }
            }

            // Done
            return true;
        }

        /* File record cache structure
         * Represenst a file-entry in cached format */
        class MfsRecord {
            public String Name;
            public UInt64 Size;
            public UInt64 AllocatedSize;
            public UInt32 Bucket;
            public UInt32 BucketLength;

            public UInt32 DirectoryBucket;
            public UInt32 DirectoryLength;
            public UInt32 DirectoryIndex;
        }
    }
}
