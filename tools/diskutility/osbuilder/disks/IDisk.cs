using System.IO;

namespace OSBuilder
{
    public interface IDisk
    {
        ulong SectorCount { get; }
        DiskGeometry Geometry { get; }

        bool Create();
        bool Open();
        void Close();

        Stream GetStream();
        bool IsOpen();

        void Seek(long offset);
        void Write(byte[] buffer, ulong atSector = 0, bool seekFirst = false);
        byte[] Read(ulong sector, ulong sectorCount);
    }
}
