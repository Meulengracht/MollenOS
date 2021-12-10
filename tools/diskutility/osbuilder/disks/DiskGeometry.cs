using System.IO;

namespace OSBuilder
{
    public class DiskGeometry
    {
        private static readonly uint KILOBYTE = 1024;
        private static readonly uint MEGABYTE = (KILOBYTE * 1024);
        private static readonly ulong GIGABYTE = (MEGABYTE * 1024);

        public uint Cylinders { get { return _cylinders; } }
        public uint SectorsPerTrack { get { return _sectorsPerTrack; } }
        public uint HeadsPerCylinder { get { return _headsPerCylinder; } }
        public uint BytesPerSector { get { return _bytesPerSector; } }

        private uint _cylinders;
        private uint _sectorsPerTrack;
        private uint _headsPerCylinder;
        private uint _bytesPerSector;

        public DiskGeometry(uint cylinders, uint headsPerCylinder, uint sectorsPerTrack, uint bytesPerSector)
        {
            _cylinders = cylinders;
            _sectorsPerTrack = sectorsPerTrack;
            _headsPerCylinder = headsPerCylinder;
            _bytesPerSector = bytesPerSector;
        }

        /// <summary>
        /// Calculates a sensible disk geometry for a disk capacity using the VHD algorithm (errs under).
        /// </summary>
        /// <param name="capacity">The desired capacity of the disk.</param>
        /// <param name="sectorSize">The logical sector size of the disk.</param>
        /// <returns>The appropriate disk geometry.</returns>
        /// <remarks>
        /// The geometry returned tends to produce a disk with less capacity
        /// than requested (an exact capacity is not always possible).  The geometry returned is the IDE
        /// (aka Physical) geometry of the disk, not necessarily the geometry used by the BIOS.
        /// </remarks>
        public static DiskGeometry CalculateGeometry(ulong diskSize, uint bytesPerSector)
        {
            uint totalSectors;
            uint cylinders;
            uint headsPerCylinder;
            uint sectorsPerTrack;

            // If more than ~128GB truncate at ~128GB
            if (diskSize > 65535UL * 16UL * 255UL * bytesPerSector)
            {
                totalSectors = 65535 * 16 * 255;
            }
            else
            {
                totalSectors = (uint)(diskSize / bytesPerSector);
            }

            // If more than ~32GB, break partition table compatibility.
            // Partition table has max 63 sectors per track.  Otherwise
            // we're looking for a geometry that's valid for both BIOS
            // and ATA.
            if (totalSectors > 65535 * 16 * 63)
            {
                sectorsPerTrack = 255;
                headsPerCylinder = 16;
            }
            else
            {
                sectorsPerTrack = 17;
                uint cylindersTimesHeads = totalSectors / sectorsPerTrack;
                headsPerCylinder = (cylindersTimesHeads + 1023) / 1024;
                
                if (headsPerCylinder < 4)
                {
                    headsPerCylinder = 4;
                }

                // If we need more than 1023 cylinders, or 16 heads, try more sectors per track
                if (cylindersTimesHeads >= headsPerCylinder * 1024U || headsPerCylinder > 16)
                {
                    sectorsPerTrack = 31;
                    headsPerCylinder = 16;
                    cylindersTimesHeads = totalSectors / sectorsPerTrack;
                }

                // We need 63 sectors per track to keep the cylinder count down
                if (cylindersTimesHeads >= headsPerCylinder * 1024U)
                {
                    sectorsPerTrack = 63;
                    headsPerCylinder = 16;
                }
            }

            cylinders = totalSectors / sectorsPerTrack / headsPerCylinder;

            return new DiskGeometry(cylinders, headsPerCylinder, sectorsPerTrack, bytesPerSector);
        }

        /// <summary>
        /// Gets the 'LBA Assisted' BIOS geometry for a disk, given it's capacity.
        /// </summary>
        /// <param name="capacity">The capacity of the disk.</param>
        /// <returns>The geometry a BIOS using the 'LBA Assisted' method for calculating disk geometry will indicate for the disk.</returns>
        public static DiskGeometry CalculateGeometryLbaAssisted(ulong capacity, uint bytesPerSector)
        {
            uint heads;
            if (capacity <= 504 * MEGABYTE)
            {
                heads = 16;
            }
            else if (capacity <= (1008 * MEGABYTE))
            {
                heads = 32;
            }
            else if (capacity <= (2016 * MEGABYTE))
            {
                heads = 64;
            }
            else if (capacity <= (4032 * MEGABYTE))
            {
                heads = 128;
            }
            else
            {
                heads = 255;
            }

            uint sectors = 63;
            uint cylinders = (uint)System.Math.Min(1024U, capacity / (sectors * (ulong)heads * bytesPerSector));
            return new DiskGeometry(cylinders, heads, sectors, bytesPerSector);
        }
    }
}
