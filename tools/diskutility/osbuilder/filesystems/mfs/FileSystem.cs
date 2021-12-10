using System;
using System.IO;
using System.Text;

namespace OSBuilder.FileSystems.MFS
{
    public class FileSystem : IFileSystem
    {
        public static readonly byte TYPE = 0x61;

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

        private static readonly uint MFS_ENDOFCHAIN = 0xFFFFFFFF;

        private static readonly uint KILOBYTE = 1024;
        private static readonly uint MEGABYTE = (KILOBYTE * 1024);
        private static readonly ulong GIGABYTE = (MEGABYTE * 1024);

        private String _partitionName;
        private Guid _partitionGuid;
        private bool _bootable = false;
        private PartitionFlags _partitionFlags = 0;
        private IDisk _disk = null;
        private BucketMap _bucketMap = null;
        private ulong _sector = 0;
        private ulong _sectorCount = 0;
        private ushort _reservedSectorCount = 0;
        private ushort _bucketSize = 0;

        uint CalculateChecksum(Byte[] Data, int SkipIndex, int SkipLength)
        {
            uint Checksum = 0;
            for (int i = 0; i < Data.Length; i++) {
                if (i >= SkipIndex && i < (SkipIndex + SkipLength)) {
                    continue;
                }
                Checksum += Data[i];
            }
            return Checksum;
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
                Byte[] Buffer = new Byte[(_bucketSize * _disk.Geometry.BytesPerSector) * BucketLengthItr];

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
                _disk.Write(Buffer, BucketToSector(BucketPtr), true);

                // Get next bucket cluster for writing
                BucketPtr = _bucketMap.GetBucketLengthAndLink(BucketPtr, out BucketLengthItr);

                // Are we at end of pointer?
                if (BucketPtr == MFS_ENDOFCHAIN) {
                    break;
                }

                // Get length of new bucket
                _bucketMap.GetBucketLengthAndLink(BucketPtr, out BucketLengthItr);
            }
        }

        /* CreateFileRecord
         * Creates a new file-record with the given flags and data, and name at in the given directory start bucket. 
         * Name must not be a path. */
        void CreateFileRecord(String Name, RecordFlags Flags, UInt32 Bucket, UInt32 BucketLength, Byte[] Data, UInt32 DirectoryBucket)
        {
            uint currentDirectoryBucket = DirectoryBucket;
            uint currentBucketLength = 0;
            int End = 0;

            // Iterate through directory and find a free record
            while (End == 0) {
                // Get length of bucket
                _bucketMap.GetBucketLengthAndLink(currentDirectoryBucket, out currentBucketLength);

                // Calculate which bucket to read in
                Byte[] directoryBuffer = _disk.Read(BucketToSector(currentDirectoryBucket), _bucketSize * currentBucketLength);

                // Iterate the bucket and find a free entry
                for (int i = 0; i < (_bucketSize * _disk.Geometry.BytesPerSector * currentBucketLength); i += 1024) {
                    if (directoryBuffer[i] == 0) {
                        // Variables
                        ulong NumBuckets = 0;
                        ulong AllocatedSize = 0;
                        ulong DataLen = 0;

                        // Do we even need to write data?
                        if (Data != null) {
                            DataLen = (ulong)Data.LongLength;
                            NumBuckets = (ulong)(Data.LongLength / _disk.Geometry.BytesPerSector) / _bucketSize;
                            if (((Data.LongLength / _disk.Geometry.BytesPerSector) % _bucketSize) > 0)
                                NumBuckets++;
                            AllocatedSize = NumBuckets * _bucketSize * _disk.Geometry.BytesPerSector;
                        }

                        // Setup flags
                        uint iFlags = (uint)Flags;
                        directoryBuffer[i + 0] = (Byte)(iFlags & 0xFF);
                        directoryBuffer[i + 1] = (Byte)((iFlags >> 8) & 0xFF);
                        directoryBuffer[i + 2] = (Byte)((iFlags >> 16) & 0xFF);
                        directoryBuffer[i + 3] = (Byte)((iFlags >> 24) & 0xFF);

                        // Initialize start buckert and length
                        directoryBuffer[i + 4] = (Byte)(Bucket & 0xFF);
                        directoryBuffer[i + 5] = (Byte)((Bucket >> 8) & 0xFF);
                        directoryBuffer[i + 6] = (Byte)((Bucket >> 16) & 0xFF);
                        directoryBuffer[i + 7] = (Byte)((Bucket >> 24) & 0xFF);

                        directoryBuffer[i + 8] = (Byte)(BucketLength & 0xFF);
                        directoryBuffer[i + 9] = (Byte)((BucketLength >> 8) & 0xFF);
                        directoryBuffer[i + 10] = (Byte)((BucketLength >> 16) & 0xFF);
                        directoryBuffer[i + 11] = (Byte)((BucketLength >> 24) & 0xFF);

                        // Initialize data checksum
                        if (Data != null) {
                            uint Checksum = CalculateChecksum(Data, -1, 0);
                            directoryBuffer[i + 16] = (Byte)(Checksum & 0xFF);
                            directoryBuffer[i + 17] = (Byte)((Checksum >> 8) & 0xFF);
                            directoryBuffer[i + 18] = (Byte)((Checksum >> 16) & 0xFF);
                            directoryBuffer[i + 19] = (Byte)((Checksum >> 24) & 0xFF);
                        }

                        // Ignore time

                        // Sizes - 0x30
                        directoryBuffer[i + 48] = (Byte)(DataLen & 0xFF);
                        directoryBuffer[i + 49] = (Byte)((DataLen >> 8) & 0xFF);
                        directoryBuffer[i + 50] = (Byte)((DataLen >> 16) & 0xFF);
                        directoryBuffer[i + 51] = (Byte)((DataLen >> 24) & 0xFF);
                        directoryBuffer[i + 52] = (Byte)((DataLen >> 32) & 0xFF);
                        directoryBuffer[i + 53] = (Byte)((DataLen >> 40) & 0xFF);
                        directoryBuffer[i + 54] = (Byte)((DataLen >> 48) & 0xFF);
                        directoryBuffer[i + 55] = (Byte)((DataLen >> 56) & 0xFF);

                        directoryBuffer[i + 56] = (Byte)(AllocatedSize & 0xFF);
                        directoryBuffer[i + 57] = (Byte)((AllocatedSize >> 8) & 0xFF);
                        directoryBuffer[i + 58] = (Byte)((AllocatedSize >> 16) & 0xFF);
                        directoryBuffer[i + 59] = (Byte)((AllocatedSize >> 24) & 0xFF);
                        directoryBuffer[i + 60] = (Byte)((AllocatedSize >> 32) & 0xFF);
                        directoryBuffer[i + 61] = (Byte)((AllocatedSize >> 40) & 0xFF);
                        directoryBuffer[i + 62] = (Byte)((AllocatedSize >> 48) & 0xFF);
                        directoryBuffer[i + 63] = (Byte)((AllocatedSize >> 56) & 0xFF);
                        
                        // SparseMap
                        directoryBuffer[i + 64] = 0xFF;
                        directoryBuffer[i + 65] = 0xFF;
                        directoryBuffer[i + 66] = 0xFF;
                        directoryBuffer[i + 67] = 0xFF;

                        // Name at 68 + 300
                        Byte[] NameData = Encoding.UTF8.GetBytes(Name);
                        for (int j = 0; j < NameData.Length; j++)
                            directoryBuffer[i + 68 + j] = NameData[j];

                        // Everything else 0
                        // Write new entry to disk and return
                        _disk.Write(directoryBuffer, BucketToSector(currentDirectoryBucket), true);
                        Console.WriteLine(
                            "  - Writing " + directoryBuffer.Length.ToString() + 
                            " bytes to disk at sector " + BucketToSector(currentDirectoryBucket).ToString()
                        );
                        return;
                    }
                }

                // Store previous and get link
                UInt32 previousBucket = currentDirectoryBucket;
                if (End == 0) {
                    currentDirectoryBucket = _bucketMap.GetBucketLengthAndLink(currentDirectoryBucket, out currentBucketLength);
                }

                // If we reach end of directory we need to expand
                if (currentDirectoryBucket == MFS_ENDOFCHAIN) {
                    Console.WriteLine("Directory - Expansion");
                    byte[] bootsector = _disk.Read(_sector, 1);

                    // Get relevant locations
                    ulong MasterRecordSector = BitConverter.ToUInt64(bootsector, 28);
                    ulong MasterRecordMirrorSector = BitConverter.ToUInt64(bootsector, 36);
                    
                    // Allocate a bunch of new buckets for expansion
                    uint allocationLength = 0;
                    uint allocation = _bucketMap.AllocateBuckets(4, out allocationLength);
                    _bucketMap.SetNextBucket(previousBucket, allocation);

                    // Update the master-record to reflect the new index
                    byte[] masterRecord = _disk.Read(_sector + MasterRecordSector, 1);
                    masterRecord[76] = (Byte)(_bucketMap.NextFreeBucket & 0xFF);
                    masterRecord[77] = (Byte)((_bucketMap.NextFreeBucket >> 8) & 0xFF);
                    masterRecord[78] = (Byte)((_bucketMap.NextFreeBucket >> 16) & 0xFF);
                    masterRecord[79] = (Byte)((_bucketMap.NextFreeBucket >> 24) & 0xFF);
                    _disk.Write(masterRecord, _sector + MasterRecordSector, true);
                    _disk.Write(masterRecord, _sector + MasterRecordMirrorSector, true);

                    // Wipe the new allocated directory block
                    Console.WriteLine("Directory - Wipe");
                    Byte[] Wipe = new Byte[_bucketSize * _disk.Geometry.BytesPerSector * allocationLength];
                    _disk.Write(Wipe, BucketToSector(allocation), true);

                    // Update iterator
                    currentDirectoryBucket = allocation;
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
                UInt32 IteratorBucket = DirectoryBucket;
                UInt32 DirectoryLength = 0;
                int End = 0;

                while (End == 0) {
                    // Get length of bucket
                    _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    Byte[] directoryBuffer = _disk.Read(BucketToSector(IteratorBucket), _bucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (int i = 0; i < (_bucketSize * _disk.Geometry.BytesPerSector * DirectoryLength); i += 1024) {
                        if (directoryBuffer[i] == 0) {
                            continue;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (directoryBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(directoryBuffer, i + 68, Len);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(directoryBuffer, i);

                        // Have we found the record we were looking for?
                        if (LookFor.Contains(".")
                            && Name.ToLower() == LookFor.ToLower()) {
                            MfsRecord nEntry = new MfsRecord();
                            nEntry.Name = Name;
                            nEntry.Size = BitConverter.ToUInt64(directoryBuffer, i + 48);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(directoryBuffer, i + 56);
                            nEntry.Bucket = BitConverter.ToUInt32(directoryBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(directoryBuffer, i + 8);

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
                                    Console.WriteLine("File: " + Name + " (" + BitConverter.ToUInt64(directoryBuffer, i + 48).ToString() + " Bytes)");
                                }
                            }
                            
                        }
                    }

                    // Get next bucket link
                    if (End == 0) {
                        IteratorBucket = _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
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
                    _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    Byte[] directoryBuffer = _disk.Read(BucketToSector(IteratorBucket), _bucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (int i = 0; i < (_bucketSize * _disk.Geometry.BytesPerSector * DirectoryLength); i += 1024) {
                        if (directoryBuffer[i] == 0) {
                            continue;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (directoryBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(directoryBuffer, i + 68, Len);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(directoryBuffer, i);

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
                            nEntry.Size = BitConverter.ToUInt64(directoryBuffer, i + 48);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(directoryBuffer, i + 56);
                            nEntry.Bucket = BitConverter.ToUInt32(directoryBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(directoryBuffer, i + 8);

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
                    }

                    // Get next bucket link
                    if (End == 0) {
                        IteratorBucket = _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
                    }
                    
                    // Have we reached end?
                    if (IteratorBucket == MFS_ENDOFCHAIN) {
                        End = 1;
                        break;
                    }
                }
            }
            return null;
        }

        /* CreateRecursive 
         * Recursively iterates through the path and creates the path. 
         * If path exists nothing happens */
        MfsRecord CreateRecursive(uint DirectoryBucket, string LocalPath)
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
                    _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    Byte[] directoryBuffer = _disk.Read(BucketToSector(IteratorBucket), _bucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (i = 0; i < (_bucketSize * _disk.Geometry.BytesPerSector * DirectoryLength); i += 1024) {
                        if (directoryBuffer[i] == 0) {
                            End = 1;
                            break;
                        }

                        // Do some name matching to see if we have found token
                        int Len = 0;
                        while (directoryBuffer[i + 68 + Len] != 0)
                            Len++;
                        String Name = Encoding.UTF8.GetString(directoryBuffer, i + 68, Len);
                        //RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(fBuffer, i);

                        // Does it exist already?
                        if (Name.ToLower() == LookFor.ToLower()) {
                            Console.WriteLine("Creation - Entry did exist already");
                            return null;
                        }
                    }

                    // Handle the case where we found free entry
                    if (End == 1) {
                        break;
                    }
                    
                    // Get next bucket link
                    if (End == 0) {
                        PreviousBucket = IteratorBucket;
                        IteratorBucket = _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
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
                    _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);

                    // Calculate the bucket we should load
                    Byte[] directoryBuffer = _disk.Read(BucketToSector(IteratorBucket), _bucketSize * DirectoryLength);

                    // Iterate the number of records
                    for (int i = 0; i < (_bucketSize * _disk.Geometry.BytesPerSector * DirectoryLength); i += 1024) {
                        if (directoryBuffer[i] == 0) {
                            continue;
                        }

                        // Do some name matching to see if we have found token
                        int nameLength = 0;
                        while (directoryBuffer[i + 68 + nameLength] != 0)
                            nameLength++;
                        String Name = Encoding.UTF8.GetString(directoryBuffer, i + 68, nameLength);
                        RecordFlags Flags = (RecordFlags)BitConverter.ToUInt32(directoryBuffer, i);

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
                            nEntry.Size = BitConverter.ToUInt64(directoryBuffer, i + 48);
                            nEntry.AllocatedSize = BitConverter.ToUInt64(directoryBuffer, i + 56);
                            nEntry.Bucket = BitConverter.ToUInt32(directoryBuffer, i + 4);
                            nEntry.BucketLength = BitConverter.ToUInt32(directoryBuffer, i + 8);

                            nEntry.DirectoryBucket = IteratorBucket;
                            nEntry.DirectoryLength = DirectoryLength;
                            nEntry.DirectoryIndex = (uint)i;

                            // Sanitize the case where we need to expand
                            if (nEntry.Bucket == MFS_ENDOFCHAIN) {
                                // Read bootsector
                                Byte[] Bootsector = _disk.Read(_sector, 1);

                                // Load some data (master-record and bucket-size)
                                ulong MasterRecordSector = BitConverter.ToUInt64(Bootsector, 28);
                                ulong MasterRecordMirrorSector = BitConverter.ToUInt64(Bootsector, 36);

                                // Read master-record
                                byte[] MasterRecord = _disk.Read(_sector + MasterRecordSector, 1);

                                // Allocate new buckets
                                uint initialBucketSize = 0;
                                nEntry.Bucket = _bucketMap.AllocateBuckets(4, out initialBucketSize);

                                // Update the master-record
                                MasterRecord[76] = (byte)(_bucketMap.NextFreeBucket & 0xFF);
                                MasterRecord[77] = (byte)((_bucketMap.NextFreeBucket >> 8) & 0xFF);
                                MasterRecord[78] = (byte)((_bucketMap.NextFreeBucket >> 16) & 0xFF);
                                MasterRecord[79] = (byte)((_bucketMap.NextFreeBucket >> 24) & 0xFF);
                                _disk.Write(MasterRecord, _sector + MasterRecordSector, true);
                                _disk.Write(MasterRecord, _sector + MasterRecordMirrorSector, true);

                                // Update the file-record
                                directoryBuffer[i + 4] = (Byte)(nEntry.Bucket & 0xFF);
                                directoryBuffer[i + 5] = (Byte)((nEntry.Bucket >> 8) & 0xFF);
                                directoryBuffer[i + 6] = (Byte)((nEntry.Bucket >> 16) & 0xFF);
                                directoryBuffer[i + 7] = (Byte)((nEntry.Bucket >> 24) & 0xFF);

                                directoryBuffer[i + 8] = (Byte)(initialBucketSize & 0xFF);
                                directoryBuffer[i + 9] = (Byte)((initialBucketSize >> 8) & 0xFF);
                                directoryBuffer[i + 10] = (Byte)((initialBucketSize >> 16) & 0xFF);
                                directoryBuffer[i + 11] = (Byte)((initialBucketSize >> 24) & 0xFF);

                                _disk.Write(directoryBuffer, BucketToSector(IteratorBucket), true);

                                // Wipe directory bucket
                                byte[] wipeBuffer = new byte[_bucketSize * _disk.Geometry.BytesPerSector * initialBucketSize];
                                _disk.Write(wipeBuffer, BucketToSector(nEntry.Bucket), true);
                            }

                            // Go further down the rabbit hole
                            return CreateRecursive(nEntry.Bucket, mPath.Substring(LookFor.Length));
                        }
                    }

                    // Get next bucket link
                    if (End == 0) {
                        IteratorBucket = _bucketMap.GetBucketLengthAndLink(IteratorBucket, out DirectoryLength);
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
        
        public FileSystem(IDisk disk, ulong startSector, ulong sectorCount)
        {
            _disk = disk;
            _sector = startSector;

            // parse the virtual boot record for info we need
            // name, bootable, partition type, etc
            byte[] vbr = disk.Read(startSector, 1);

            _bootable = vbr[8] != 0;
            _sectorCount = BitConverter.ToUInt64(vbr, 16);
            _reservedSectorCount = BitConverter.ToUInt16(vbr, 24);
            _bucketSize = BitConverter.ToUInt16(vbr, 26);
            
            // parse the master boot record
            var masterRecordOffset = BitConverter.ToUInt64(vbr, 28);
            byte[] masterRecord = disk.Read(startSector + masterRecordOffset, 1);

            _partitionFlags = (PartitionFlags)BitConverter.ToUInt32(masterRecord, 4);
            _partitionName = Encoding.UTF8.GetString(masterRecord, 12, 64);

            var bucketMapOffset = BitConverter.ToUInt64(masterRecord, 92);
            var freeBucketIndex = BitConverter.ToUInt32(masterRecord, 76);

            _bucketMap = new BucketMap(_disk, 
                (_sector + _reservedSectorCount), 
                (_sectorCount - _reservedSectorCount),
                _bucketSize);
            _bucketMap.Open(bucketMapOffset, freeBucketIndex);
        }

        public FileSystem(string partitionName, Guid partitionGuid, FileSystemAttributes attributes)
        {
            _partitionName = partitionName;
            _partitionGuid = partitionGuid;
            if (attributes.HasFlag(FileSystemAttributes.Boot)) {
                _bootable = true;
            }
            
            if (partitionGuid == DiskLayouts.GPTGuids.ValiSystemPartition) {
                _partitionFlags |= PartitionFlags.SystemDrive;
            }
            if (partitionGuid == DiskLayouts.GPTGuids.ValiDataUserPartition ||
                partitionGuid == DiskLayouts.GPTGuids.ValiDataPartition) {
                _partitionFlags |= PartitionFlags.DataDrive;
            }
            if (partitionGuid == DiskLayouts.GPTGuids.ValiDataUserPartition ||
                partitionGuid == DiskLayouts.GPTGuids.ValiUserPartition) {
                _partitionFlags |= PartitionFlags.UserDrive;
            }
            if (attributes.HasFlag(FileSystemAttributes.Hidden)) {
                _partitionFlags |= PartitionFlags.HiddenDrive;
            }
        }

        public void Dispose()
        {
            
        }

        public void Initialize(IDisk disk, ulong startSector, ulong sectorCount)
        {
            _disk = disk;
            _sector = startSector;
            _sectorCount = sectorCount;
        }

        private ulong BucketToSector(uint bucket)
        {
            return _sector + _reservedSectorCount + (ulong)(bucket * _bucketSize);
        }

        private ushort DetermineBucketSize(ulong driveSizeBytes)
        {
            if (driveSizeBytes <= GIGABYTE)
                return 8;
            else if (driveSizeBytes <= (64UL * GIGABYTE))
                return 16;
            else if (driveSizeBytes <= (256UL * GIGABYTE))
                return 32;
            else
                return 64;
        }
        
        private void BuildMasterRecord(uint rootBucket, uint journalBucket, uint badListBucket, 
            ulong masterRecordSector, ulong masterRecordMirrorSector)
        {
            // Build a new master-record structure
            //uint32_t Magic;
            //uint32_t Flags;
            //uint32_t Checksum;      // Checksum of the master-record
            //uint8_t PartitionName[64];

            //uint32_t FreeBucket;        // Pointer to first free index
            //uint32_t RootIndex;     // Pointer to root directory
            //uint32_t BadBucketIndex;    // Pointer to list of bad buckets
            //uint32_t JournalIndex;  // Pointer to journal file

            //uint64_t MapSector;     // Start sector of bucket-map_sector
            //uint64_t MapSize;		// Size of bucket map
            byte[] masterRecord = new byte[512];
            masterRecord[0] = 0x4D;
            masterRecord[1] = 0x46;
            masterRecord[2] = 0x53;
            masterRecord[3] = 0x31;

            // Initialize partition flags
            uint flagsAsUInt = (uint)_partitionFlags;
            masterRecord[4] = (byte)(flagsAsUInt & 0xFF);
            masterRecord[5] = (byte)((flagsAsUInt >> 8) & 0xFF);

            // Initialize partition name
            byte[] NameBytes = Encoding.UTF8.GetBytes(_partitionName);
            Array.Copy(NameBytes, 0, masterRecord, 12, NameBytes.Length);

            // Initialize free pointer
            masterRecord[76] = (Byte)(_bucketMap.NextFreeBucket & 0xFF);
            masterRecord[77] = (Byte)((_bucketMap.NextFreeBucket >> 8) & 0xFF);
            masterRecord[78] = (Byte)((_bucketMap.NextFreeBucket >> 16) & 0xFF);
            masterRecord[79] = (Byte)((_bucketMap.NextFreeBucket >> 24) & 0xFF);

            // Initialize root directory pointer
            masterRecord[80] = (Byte)(rootBucket & 0xFF);
            masterRecord[81] = (Byte)((rootBucket >> 8) & 0xFF);
            masterRecord[82] = (Byte)((rootBucket >> 16) & 0xFF);
            masterRecord[83] = (Byte)((rootBucket >> 24) & 0xFF);

            // Initialize bad bucket list pointer
            masterRecord[84] = (Byte)(badListBucket & 0xFF);
            masterRecord[85] = (Byte)((badListBucket >> 8) & 0xFF);
            masterRecord[86] = (Byte)((badListBucket >> 16) & 0xFF);
            masterRecord[87] = (Byte)((badListBucket >> 24) & 0xFF);

            // Initialize journal list pointer
            masterRecord[88] = (Byte)(journalBucket & 0xFF);
            masterRecord[89] = (Byte)((journalBucket >> 8) & 0xFF);
            masterRecord[90] = (Byte)((journalBucket >> 16) & 0xFF);
            masterRecord[91] = (Byte)((journalBucket >> 24) & 0xFF);

            // Initialize map sector pointer
            ulong offset = _bucketMap.MapStartSector - _sector;
            masterRecord[92] = (Byte)(offset & 0xFF);
            masterRecord[93] = (Byte)((offset >> 8) & 0xFF);
            masterRecord[94] = (Byte)((offset >> 16) & 0xFF);
            masterRecord[95] = (Byte)((offset >> 24) & 0xFF);
            masterRecord[96] = (Byte)((offset >> 32) & 0xFF);
            masterRecord[97] = (Byte)((offset >> 40) & 0xFF);
            masterRecord[98] = (Byte)((offset >> 48) & 0xFF);
            masterRecord[99] = (Byte)((offset >> 56) & 0xFF);

            // Initialize map size
            ulong mapSize = _bucketMap.GetSizeOfMap();
            masterRecord[100] = (Byte)(mapSize & 0xFF);
            masterRecord[101] = (Byte)((mapSize >> 8) & 0xFF);
            masterRecord[102] = (Byte)((mapSize >> 16) & 0xFF);
            masterRecord[103] = (Byte)((mapSize >> 24) & 0xFF);
            masterRecord[104] = (Byte)((mapSize >> 32) & 0xFF);
            masterRecord[105] = (Byte)((mapSize >> 40) & 0xFF);
            masterRecord[106] = (Byte)((mapSize >> 48) & 0xFF);
            masterRecord[107] = (Byte)((mapSize >> 56) & 0xFF);

            // Initialize checksum
            uint checksum = CalculateChecksum(masterRecord, 8, 4);
            masterRecord[8] = (Byte)(checksum & 0xFF);
            masterRecord[9] = (Byte)((checksum >> 8) & 0xFF);
            masterRecord[10] = (Byte)((checksum >> 16) & 0xFF);
            masterRecord[11] = (Byte)((checksum >> 24) & 0xFF);

            // Flush it to disk
            _disk.Write(masterRecord, masterRecordSector, true);
            _disk.Write(masterRecord, masterRecordMirrorSector, true);
        }

        private void BuildVBR(ulong masterBucketSector, ulong mirrorMasterBucketSector)
        {
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
            byte[] bootsector = new byte[_disk.Geometry.BytesPerSector];

            // Initialize magic
            bootsector[3] = 0x4D;
            bootsector[4] = 0x46;
            bootsector[5] = 0x53;
            bootsector[6] = 0x31;

            // Initialize version
            bootsector[7] = 0x1;

            // Initialize flags
            // 0x1 - BootDrive
            // 0x2 - Encrypted
            bootsector[8] = _bootable ? (byte)0x1 : (byte)0x0;

            // Initialize disk metrics
            bootsector[9] = 0x80;
            bootsector[10] = (Byte)(_disk.Geometry.BytesPerSector & 0xFF);
            bootsector[11] = (Byte)((_disk.Geometry.BytesPerSector >> 8) & 0xFF);

            // Sectors per track
            bootsector[12] = (Byte)(_disk.Geometry.SectorsPerTrack & 0xFF);
            bootsector[13] = (Byte)((_disk.Geometry.SectorsPerTrack >> 8) & 0xFF);

            // Heads per cylinder
            bootsector[14] = (Byte)(_disk.Geometry.HeadsPerCylinder & 0xFF);
            bootsector[15] = (Byte)((_disk.Geometry.HeadsPerCylinder >> 8) & 0xFF);

            // Total sectors on partition
            bootsector[16] = (Byte)(_sectorCount & 0xFF);
            bootsector[17] = (Byte)((_sectorCount >> 8) & 0xFF);
            bootsector[18] = (Byte)((_sectorCount >> 16) & 0xFF);
            bootsector[19] = (Byte)((_sectorCount >> 24) & 0xFF);
            bootsector[20] = (Byte)((_sectorCount >> 32) & 0xFF);
            bootsector[21] = (Byte)((_sectorCount >> 40) & 0xFF);
            bootsector[22] = (Byte)((_sectorCount >> 48) & 0xFF);
            bootsector[23] = (Byte)((_sectorCount >> 56) & 0xFF);

            // Reserved sectors
            bootsector[24] = (Byte)(_reservedSectorCount & 0xFF);
            bootsector[25] = (Byte)((_reservedSectorCount >> 8) & 0xFF);

            // Size of an bucket in sectors
            bootsector[26] = (Byte)(_bucketSize & 0xFF);
            bootsector[27] = (Byte)((_bucketSize >> 8) & 0xFF);

            // Sector of master-record
            ulong offset = masterBucketSector - _sector;
            bootsector[28] = (Byte)(offset & 0xFF);
            bootsector[29] = (Byte)((offset >> 8) & 0xFF);
            bootsector[30] = (Byte)((offset >> 16) & 0xFF);
            bootsector[31] = (Byte)((offset >> 24) & 0xFF);
            bootsector[32] = (Byte)((offset >> 32) & 0xFF);
            bootsector[33] = (Byte)((offset >> 40) & 0xFF);
            bootsector[34] = (Byte)((offset >> 48) & 0xFF);
            bootsector[35] = (Byte)((offset >> 56) & 0xFF);

            // Sector of master-record mirror
            offset = masterBucketSector - _sector;
            bootsector[36] = (Byte)(offset & 0xFF);
            bootsector[37] = (Byte)((offset >> 8) & 0xFF);
            bootsector[38] = (Byte)((offset >> 16) & 0xFF);
            bootsector[39] = (Byte)((offset >> 24) & 0xFF);
            bootsector[40] = (Byte)((offset >> 32) & 0xFF);
            bootsector[41] = (Byte)((offset >> 40) & 0xFF);
            bootsector[42] = (Byte)((offset >> 48) & 0xFF);
            bootsector[43] = (Byte)((offset >> 56) & 0xFF);

            _disk.Write(bootsector, _sector, true);
        }

        public void InstallBootloaders()
        {
            // Load up boot-sector
            Console.WriteLine("MakeBoot - loading bootsector (stage1.sys)");
            byte[] bootsector = File.ReadAllBytes("deploy/stage1.sys");

            // Modify boot-sector by preserving the header 44
            byte[] existingSectorContent = _disk.Read(_sector, 1);
            Buffer.BlockCopy(existingSectorContent, 3, bootsector, 3, 41);

            // Mark the partition as os-partition
            bootsector[8] = 0x1;

            // Flush the modified sector back to disk
            Console.WriteLine("MakeBoot - writing bootsector to disk");
            _disk.Write(bootsector, _sector, true);

            // Write stage2 to disk
            Console.WriteLine("MakeBoot - loading stage2 (stage2.sys)");
            byte[] stage2Data = File.ReadAllBytes("deploy/stage2.sys");
            byte[] sectorAlignedBuffer = new Byte[((stage2Data.Length / _disk.Geometry.BytesPerSector) + 1) * _disk.Geometry.BytesPerSector];
            stage2Data.CopyTo(sectorAlignedBuffer, 0);

            // Make sure we allocate a sector-aligned buffer
            Console.WriteLine("MakeBoot - writing stage2 to disk");
            _disk.Write(sectorAlignedBuffer, _sector + 1, true);
        }

        public bool Format()
        {
            _reservedSectorCount = 1; // VBR

            if (_disk == null)
                return false;

            // Sanitize that bootloaders are present if the partition is marked bootable
            if (_bootable)
            {
                if (!File.Exists("deploy/stage2.sys") || !File.Exists("deploy/stage1.sys"))
                {
                    Console.WriteLine("Format - Bootloaders are missing in deploy folder (stage1.sys & stage2.sys)");
                    return false;
                }

                byte[] stage2Buffer = File.ReadAllBytes("deploy/stage2.sys");
                _reservedSectorCount += (ushort)((stage2Buffer.Length / _disk.Geometry.BytesPerSector) + 1);
            }

            ulong partitionSizeBytes = _sectorCount * _disk.Geometry.BytesPerSector;
            Console.WriteLine("Format - size of partition " + partitionSizeBytes.ToString() + " bytes");

            _bucketSize = DetermineBucketSize(_sectorCount * _disk.Geometry.BytesPerSector);
            uint masterBucketSectorOffset = (uint)_reservedSectorCount;

            // round the number of reserved sectors up to a equal of buckets
            _reservedSectorCount = (ushort)((((_reservedSectorCount + 1) / _bucketSize) + 1) * _bucketSize);

            Console.WriteLine("Format - Bucket Size: " + _bucketSize.ToString());
            Console.WriteLine("Format - Reserved Sectors: " + _reservedSectorCount.ToString());

            _bucketMap = new BucketMap(_disk, 
                (_sector + _reservedSectorCount), 
                (_sectorCount - _reservedSectorCount),
                _bucketSize);
            var mapCreated = _bucketMap.Create();
            if (!mapCreated)
                return false;

            ulong masterBucketSector = _sector + masterBucketSectorOffset;
            ulong mirrorMasterBucketSector = _bucketMap.MapStartSector - 1;

            // Debug
            Console.WriteLine("Format - Creating master-records");
            Console.WriteLine("Format - Original: " + masterBucketSectorOffset.ToString());
            Console.WriteLine("Format - Mirror: " + mirrorMasterBucketSector.ToString());

            // Allocate for:
            // - Root directory - 8 buckets
            // - Bad-bucket list - 1 bucket
            // - Journal list - 8 buckets
            uint initialBucketSize = 0;
            uint rootIndex = _bucketMap.AllocateBuckets(8, out initialBucketSize);
            uint journalIndex = _bucketMap.AllocateBuckets(8, out initialBucketSize);
            uint badBucketIndex = _bucketMap.AllocateBuckets(1, out initialBucketSize);
            Console.WriteLine("Format - Free bucket pointer after setup: " + _bucketMap.NextFreeBucket.ToString());
            Console.WriteLine("Format - Wiping root data");

            // Allocate a zero array to fill the allocated sectors with
            byte[] wipeBuffer = new byte[_bucketSize * _disk.Geometry.BytesPerSector];
            _disk.Write(wipeBuffer, BucketToSector(badBucketIndex), true);

            wipeBuffer = new Byte[(_bucketSize * _disk.Geometry.BytesPerSector) * 8];
            _disk.Write(wipeBuffer, BucketToSector(rootIndex), true);
            _disk.Write(wipeBuffer, BucketToSector(journalIndex), true);

            // build master record
            Console.WriteLine("Format - Installing Master Records");
            BuildMasterRecord(rootIndex, journalIndex, badBucketIndex, masterBucketSector, mirrorMasterBucketSector);

            // install vbr
            Console.WriteLine("Format - Installing VBR");
            BuildVBR(masterBucketSector, mirrorMasterBucketSector);

            // make bootable if requested
            if (_bootable)
            {
                InstallBootloaders();
            }
            return true;
        }


        /* ListDirectory
         * List's the contents of the given path - that must be a directory path */
        public bool ListDirectory(String Path)
        {
            // Sanitize variables
            if (_disk == null)
                return false;

            // Read bootsector
            Byte[] Bootsector = _disk.Read(_sector, 1);

            // Load some data (master-record and bucket-size)
            ulong MasterRecordSector = BitConverter.ToUInt64(Bootsector, 28);

            // Read master-record
            Byte[] MasterRecord = _disk.Read(_sector + MasterRecordSector, 1);
            uint RootBucket = BitConverter.ToUInt32(MasterRecord, 80);

            // Call our recursive function to list everything
            Console.WriteLine("Files in " + Path + ":");
            ListRecursive(RootBucket, Path);
            Console.WriteLine("");
            return true;
        }

        /* WriteFile 
         * Creates a new file or directory with the given path, flags and data */
        public bool CreateFile(string localPath, FileFlags fileFlags, byte[] fileContents)
        {
            if (_disk == null)
                return false;

            byte[] bootsector = _disk.Read(_sector, 1);

            // Load some data (master-record and bucket-size)
            ulong MasterRecordSector = BitConverter.ToUInt64(bootsector, 28);
            ulong MasterRecordMirrorSector = BitConverter.ToUInt64(bootsector, 36);

            // Read master-record
            byte[] masterRecord = _disk.Read(_sector + MasterRecordSector, 1);
            uint RootBucket = BitConverter.ToUInt32(masterRecord, 80);
            ulong SectorsRequired = 0;
            uint BucketsRequired = 0;

            // Sanitize
            if (fileContents != null) {
                // Calculate number of sectors required
                SectorsRequired = (ulong)fileContents.LongLength / _disk.Geometry.BytesPerSector;
                if (((ulong)fileContents.LongLength % _disk.Geometry.BytesPerSector) > 0)
                    SectorsRequired++;

                // Calculate the number of buckets required
                BucketsRequired = (uint)(SectorsRequired / _bucketSize);
                if ((SectorsRequired % _bucketSize) > 0)
                    BucketsRequired++;
            }

            // Try to locate if the record exists already
            // Because if it exists - then we update it
            // If it does not exist - we then create it
            MfsRecord nEntry = ListRecursive(RootBucket, localPath, false);
            if (nEntry != null) {
                Console.WriteLine("File exists in table, updating");

                // Handle expansion if we are trying to write more than what
                // is currently allocated
                if ((ulong)fileContents.LongLength > nEntry.AllocatedSize) {

                    // Calculate only the difference in allocation size
                    ulong sectorCount = ((ulong)fileContents.LongLength - nEntry.AllocatedSize) / _disk.Geometry.BytesPerSector;
                    if ((((ulong)fileContents.LongLength - nEntry.AllocatedSize) % _disk.Geometry.BytesPerSector) > 0)
                        sectorCount++;
                    uint bucketCount = (uint)(sectorCount / _bucketSize);
                    if ((sectorCount % _bucketSize) > 0)
                        bucketCount++;

                    // Do the allocation
                    Console.WriteLine("  - allocating " + bucketCount.ToString() + " buckets");

                    uint initialBucketSize = 0;
                    uint bucketAllocation = _bucketMap.AllocateBuckets(bucketCount, out initialBucketSize);

                    // Iterate to end of data chain, but keep a pointer to the previous
                    UInt32 BucketPtr = nEntry.Bucket;
                    UInt32 bucketPrevPtr = 0;
                    UInt32 BucketLength = 0;
                    while (BucketPtr != MFS_ENDOFCHAIN) {
                        bucketPrevPtr = BucketPtr;
                        BucketPtr = _bucketMap.GetBucketLengthAndLink(BucketPtr, out BucketLength);
                    }

                    // Update the last link to the newly allocated
                    _bucketMap.SetNextBucket(bucketPrevPtr, bucketAllocation);

                    // Update the master-record
                    masterRecord[76] = (Byte)(_bucketMap.NextFreeBucket & 0xFF);
                    masterRecord[77] = (Byte)((_bucketMap.NextFreeBucket >> 8) & 0xFF);
                    masterRecord[78] = (Byte)((_bucketMap.NextFreeBucket >> 16) & 0xFF);
                    masterRecord[79] = (Byte)((_bucketMap.NextFreeBucket >> 24) & 0xFF);
                    _disk.Write(masterRecord, _sector + MasterRecordSector, true);
                    _disk.Write(masterRecord, _sector + MasterRecordMirrorSector, true);

                    // Update the allocated size in cached
                    nEntry.AllocatedSize += (bucketCount * _bucketSize * _disk.Geometry.BytesPerSector);
                }

                // We should free buckets that are not used here if data size is less
                Console.WriteLine("  - updating data for file");
                FillBucketChain(nEntry.Bucket, nEntry.BucketLength, fileContents);

                // Read the in the relevant bucket for directory
                byte[] directoryBuffer = _disk.Read(
                    BucketToSector(nEntry.DirectoryBucket), 
                    _bucketSize * nEntry.DirectoryLength
                );

                // Update fields
                directoryBuffer[nEntry.DirectoryIndex + 4] = (Byte)(nEntry.Bucket & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 5] = (Byte)((nEntry.Bucket >> 8) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 6] = (Byte)((nEntry.Bucket >> 16) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 7] = (Byte)((nEntry.Bucket >> 24) & 0xFF);

                directoryBuffer[nEntry.DirectoryIndex + 8] = (Byte)(nEntry.BucketLength & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 9] = (Byte)((nEntry.BucketLength >> 8) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 10] = (Byte)((nEntry.BucketLength >> 16) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 11] = (Byte)((nEntry.BucketLength >> 24) & 0xFF);

                directoryBuffer[nEntry.DirectoryIndex + 48] = (Byte)(fileContents.LongLength & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 49] = (Byte)((fileContents.LongLength >> 8) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 50] = (Byte)((fileContents.LongLength >> 16) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 51] = (Byte)((fileContents.LongLength >> 24) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 52] = (Byte)((fileContents.LongLength >> 32) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 53] = (Byte)((fileContents.LongLength >> 40) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 54] = (Byte)((fileContents.LongLength >> 48) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 55] = (Byte)((fileContents.LongLength >> 56) & 0xFF);

                directoryBuffer[nEntry.DirectoryIndex + 56] = (Byte)(nEntry.AllocatedSize & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 57] = (Byte)((nEntry.AllocatedSize >> 8) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 58] = (Byte)((nEntry.AllocatedSize >> 16) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 59] = (Byte)((nEntry.AllocatedSize >> 24) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 60] = (Byte)((nEntry.AllocatedSize >> 32) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 61] = (Byte)((nEntry.AllocatedSize >> 40) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 62] = (Byte)((nEntry.AllocatedSize >> 48) & 0xFF);
                directoryBuffer[nEntry.DirectoryIndex + 63] = (Byte)((nEntry.AllocatedSize >> 56) & 0xFF);

                // Flush the modified directory back to disk
                _disk.Write(directoryBuffer, BucketToSector(nEntry.DirectoryBucket), true);
            }
            else {
                Console.WriteLine("/" + localPath + " is a new " 
                    + (fileFlags.HasFlag(FileFlags.Directory) ? "directory" : "file"));
                MfsRecord cInfo = CreateRecursive(RootBucket, localPath);
                if (cInfo == null) {
                    Console.WriteLine("The creation info returned null, somethings wrong");
                    return false;
                }

                Console.WriteLine("  - room in bucket " + cInfo.DirectoryBucket.ToString() + " at index " + cInfo.DirectoryIndex.ToString());

                // Reload master-record and update free-bucket variable 
                // as it could have changed when expanding directory
                uint startBucket = MFS_ENDOFCHAIN;
                uint initialBucketSize = 0;
                if (fileContents != null) {
                    startBucket = _bucketMap.AllocateBuckets(BucketsRequired, out initialBucketSize);

                    // Update the master-record
                    masterRecord = _disk.Read(_sector + MasterRecordSector, 1);
                    masterRecord[76] = (Byte)(_bucketMap.NextFreeBucket & 0xFF);
                    masterRecord[77] = (Byte)((_bucketMap.NextFreeBucket >> 8) & 0xFF);
                    masterRecord[78] = (Byte)((_bucketMap.NextFreeBucket >> 16) & 0xFF);
                    masterRecord[79] = (Byte)((_bucketMap.NextFreeBucket >> 24) & 0xFF);
                    _disk.Write(masterRecord, _sector + MasterRecordSector, true);
                    _disk.Write(masterRecord, _sector + MasterRecordMirrorSector, true);
                }
                
                // Build flags
                RecordFlags recordFlags = RecordFlags.InUse | RecordFlags.Chained;
                if (fileFlags.HasFlag(FileFlags.Directory))
                    recordFlags |= RecordFlags.Directory;
                if (fileFlags.HasFlag(FileFlags.System))
                    recordFlags |= RecordFlags.System;

                // Create entry in base directory
                Console.WriteLine("  - creating directory entry");
                CreateFileRecord(Path.GetFileName(localPath), recordFlags, startBucket, initialBucketSize, fileContents, cInfo.DirectoryBucket);

                // Now fill the allocated buckets with data
                if (fileContents != null) {
                    Console.WriteLine("  - writing file-data to bucket " + startBucket.ToString());
                    FillBucketChain(startBucket, initialBucketSize, fileContents);
                }
            }
            return true;
        }

        public bool CreateDirectory(string localPath, FileFlags flags)
        {
            return CreateFile(localPath, flags | FileFlags.Directory, null);
        }

        public bool IsBootable()
        {
            return _bootable;
        }

        public byte GetFileSystemType()
        {
            return TYPE;
        }
        
        public Guid GetFileSystemTypeGuid()
        {
            return _partitionGuid;
        }

        public ulong GetSectorStart()
        {
            return _sector;
        }

        public ulong GetSectorCount()
        {
            return _sectorCount;
        }
        
        public string GetName()
        {
            return _partitionName;
        }

        /* File record cache structure
         * Represenst a file-entry in cached format */
        class MfsRecord {
            public string Name;
            public ulong Size;
            public ulong AllocatedSize;
            public uint Bucket;
            public uint BucketLength;

            public uint DirectoryBucket;
            public uint DirectoryLength;
            public uint DirectoryIndex;
        }
    }
}
