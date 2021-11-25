using System.IO;

namespace OSBuilder
{
    public interface IDisk
    {
        uint BytesPerSector { get; }
        ulong SectorCount { get; }
        uint SectorsPerTrack { get; }
        uint Heads { get; }
        uint Cylinders { get; }

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
