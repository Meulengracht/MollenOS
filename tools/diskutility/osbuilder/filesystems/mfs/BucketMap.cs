using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace OSBuilder.FileSystems.MFS
{
    public class BucketMap
    {
        private readonly uint MFS_ENDOFCHAIN = 0xFFFFFFFF;
        private readonly int MAPENTRY_SIZE = 8;

        public uint NextFreeBucket { get { return _nextFreeBucket; } }
        public ulong MapStartSector { get { return _mapSector; } }

        private IDisk _disk = null;
        private ulong _sector = 0;
        private ulong _sectorCount = 0;
        private ushort _sectorsPerBucket = 0;
        private ulong _mapSector = 0;
        private uint _nextFreeBucket = 0;

        /// <summary>
        /// Constructor
        /// </summary>
        /// <param name="disk">Disk interface</param>
        /// <param name="sector">Start sector of the partition</param>
        /// <param name="sectorCount">Size of the partition</param>
        /// <param name="sectorsPerBucket">Sectors per bucket</param>

        public BucketMap(IDisk disk, ulong sector, ulong sectorCount, ushort sectorsPerBucket)
        {
            _disk = disk;
            _sector = sector;
            _sectorCount = sectorCount;
            _sectorsPerBucket = sectorsPerBucket;
        }

        public ulong GetSizeOfMap()
        {
            return (_sectorCount / _sectorsPerBucket) * (ulong)MAPENTRY_SIZE;
        }

        // length is upper dword, link is lower dword
        private void WriteEntryToBuffer(byte[] buffer, uint index, uint length, uint nextBucket)
        {
            // write link
            byte[] entry = BitConverter.GetBytes(nextBucket);
            Array.Copy(entry, 0, buffer, index * 8, 4);

            // write size
            entry = BitConverter.GetBytes(length);
            Array.Copy(entry, 0, buffer, (index * 8) + 4, 4);
        }

        public bool Create()
        {
            if (_disk == null)
                return false;

            // Start by calculating the maximum size of the map. 
            // MasterBucket | Data | MasterBucketMirror | Map
            ulong maxMapSize = GetSizeOfMap(); // Bytes
            ulong mapSectorCount = (ulong)Math.Ceiling((double)maxMapSize / _disk.Geometry.BytesPerSector); // Sectors
            ulong mapBucketCount = (_sectorCount - mapSectorCount) / _sectorsPerBucket; // Upper bound of the map
            
            _mapSector = (_sector + _sectorCount - 1) - mapSectorCount;

            // Reserve an additional bucket for the MasterBucketMirror
            mapBucketCount--; 

            Console.WriteLine("MfsBucketMap: Start Sector: " + _mapSector.ToString());
            Console.WriteLine("MfsBucketMap: Map Size (bytes): " + maxMapSize.ToString());
            Console.WriteLine("MfsBucketMap: Available Buckets: " + mapBucketCount.ToString());
            Console.WriteLine("MfsBucketMap: Building map");

            // Seek to start of map
            _disk.Seek((Int64)(_mapSector * _disk.Geometry.BytesPerSector));

            // A map entry consists of the length of the bucket, and it's link
            // To get the length of the link, you must lookup it's length by accessing Map[Link]
            // Length of bucket 0 is HIDWORD(Map[0]), Link of bucket 0 is LODWORD(Map[0])
            // If the link equals 0xFFFFFFFF there is no link
            byte[] map = new Byte[_disk.Geometry.BytesPerSector];
            WriteEntryToBuffer(map, 0, (uint)mapBucketCount, MFS_ENDOFCHAIN);
            _disk.Write(map);
            return true;
        }

        public void Open(ulong mapSector, uint nextFreeBucket)
        {
            _mapSector = mapSector;
            _nextFreeBucket = nextFreeBucket;
        }

        public uint AllocateBuckets(uint bucketCount, out uint sizeOfFirstBucket)
        {
            if (bucketCount == 0)
            {
                sizeOfFirstBucket = 0;
                return MFS_ENDOFCHAIN;
            }

            uint mapEntriesPerSector = _disk.Geometry.BytesPerSector / 8;
            uint allocation = _nextFreeBucket;
            
            uint bucketsLeft = bucketCount;
            uint bucketLink = _nextFreeBucket; // we start at the free one
            uint bucketLinkPrevious = MFS_ENDOFCHAIN;
            uint firstFreeSize = 0;

            while (bucketsLeft > 0) {
                uint sizeOfBucket = 0;
                uint sectorOffset = bucketLink / mapEntriesPerSector; // entry offset (sector)
                uint sectorIndex = bucketLink % mapEntriesPerSector;  // entry offset (in-sector)
                byte[] sectorBuffer = _disk.Read(_mapSector + sectorOffset, 1);

                bucketLinkPrevious = bucketLink;
                bucketLink = BitConverter.ToUInt32(sectorBuffer, (int)(sectorIndex * 8)); // link is lower DWORD
                sizeOfBucket = BitConverter.ToUInt32(sectorBuffer, (int)((sectorIndex * 8) + 4)); // length of bucket is upper DWORD

                // Did this block have enough for us?
                if (sizeOfBucket > bucketsLeft) {
                    // Yes, we need to split it up to two blocks now
                    uint nextFreeBucket = bucketLinkPrevious + bucketsLeft;
                    uint nextFreeCount = sizeOfBucket - bucketsLeft;

                    if (firstFreeSize == 0)
                        firstFreeSize = (uint)bucketsLeft;

                    // We have to adjust now, since we are taking only a chunk
                    // of the available length.
                    // bucketsLeft = size we allocate.
                    WriteEntryToBuffer(sectorBuffer, sectorIndex, (uint)bucketsLeft, MFS_ENDOFCHAIN);
                    _disk.Write(sectorBuffer, _mapSector + sectorOffset, true);

                    // Create new block at the next link. Recalculate position
                    sectorOffset = nextFreeBucket / mapEntriesPerSector;
                    sectorIndex = nextFreeBucket % mapEntriesPerSector;

                    // Update the map entry
                    sectorBuffer = _disk.Read(_mapSector + sectorOffset, 1);
                    WriteEntryToBuffer(sectorBuffer, sectorIndex, nextFreeCount, bucketLink);
                    _disk.Write(sectorBuffer, _mapSector + sectorOffset, true);

                    _nextFreeBucket = nextFreeBucket;
                    sizeOfFirstBucket = firstFreeSize;
                    return allocation;
                }
                else
                {
                    // We can just take the whole cake no need to modify it's length 
                    if (firstFreeSize == 0)
                        firstFreeSize = sizeOfBucket;

                    bucketsLeft -= sizeOfBucket;

                    if (bucketsLeft != 0 && bucketLink == MFS_ENDOFCHAIN)
                        throw new OutOfMemoryException("MfsBucketMap: AllocateBuckets: Out of sectors, partition is full.");
                }
            }

            // Update BucketPrevPtr to MFS_ENDOFCHAIN
            if (bucketLinkPrevious != MFS_ENDOFCHAIN)
            {
                uint __sectorOffset = bucketLinkPrevious / mapEntriesPerSector;
                uint __sectorIndex = bucketLinkPrevious % mapEntriesPerSector;
                byte[] __sectorBuffer = _disk.Read(_mapSector + __sectorOffset, 1);

                // Modify link
                __sectorBuffer[__sectorIndex * 8] = 0xFF;
                __sectorBuffer[(__sectorIndex * 8) + 1] = 0xFF;
                __sectorBuffer[(__sectorIndex * 8) + 2] = 0xFF;
                __sectorBuffer[(__sectorIndex * 8) + 3] = 0xFF;
                _disk.Write(__sectorBuffer, _mapSector + __sectorOffset, true);
            }
            
            _nextFreeBucket = bucketLink;
            sizeOfFirstBucket = firstFreeSize;
            return allocation;
        }

        /* GetBucketLengthAndLink
         * The next bucket is returned as result, and the length of the bucket is
         * given in the _out_ parameter */
        public uint GetBucketLengthAndLink(uint bucket, out uint bucketLength)
        {
            // Calculate index into bucket map
            uint mapEntriesPerSector = _disk.Geometry.BytesPerSector / 8;
            uint sectorOffset = bucket / mapEntriesPerSector;
            uint sectorIndex = bucket % mapEntriesPerSector;

            // Read the calculated sector
            byte[] sectorBuffer = _disk.Read(_mapSector + sectorOffset, 1);

            // Update length and return link
            bucketLength = BitConverter.ToUInt32(sectorBuffer, (int)((sectorIndex * 8) + 4));
            return BitConverter.ToUInt32(sectorBuffer, (int)(sectorIndex * 8));
        }

        /* SetNextBucket
         * Updates the link to the next bucket for the given bucket */
        public void SetNextBucket(uint bucket, uint nextBucket)
        {
            // Calculate index into bucket map
            uint mapEntriesPerSector = _disk.Geometry.BytesPerSector / 8;
            uint sectorOffset = bucket / mapEntriesPerSector;
            uint sectorIndex = bucket % mapEntriesPerSector;

            // Read the calculated sector
            byte[] sectorBuffer = _disk.Read(_mapSector + sectorOffset, 1);

            // Update link
            sectorBuffer[sectorIndex * 8] = (Byte)(nextBucket & 0xFF);
            sectorBuffer[(sectorIndex * 8) + 1] = (Byte)((nextBucket >> 8) & 0xFF);
            sectorBuffer[(sectorIndex * 8) + 2] = (Byte)((nextBucket >> 16) & 0xFF);
            sectorBuffer[(sectorIndex * 8) + 3] = (Byte)((nextBucket >> 24) & 0xFF);

            // Flush buffer to disk
            _disk.Write(sectorBuffer, _mapSector + sectorOffset, true);
        }
    }
}
