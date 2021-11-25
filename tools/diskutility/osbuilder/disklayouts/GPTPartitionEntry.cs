using System;
using System.Text;

namespace OSBuilder.DiskLayouts
{
    public class GPTPartitionEntry
    {
        public Guid TypeGuid { get; set; }
        public Guid UniqueId { get; set; }
        public ulong FirstSector { get; set; }
        public ulong LastSector { get; set; }
        public GPTPartitionAttributes Attributes { get; set; }
        public string Name { get; set; }

        public static GPTPartitionEntry Parse(byte[] data, int offset)
        {
            GPTPartitionEntry entry = new GPTPartitionEntry();
            
            entry.TypeGuid = new Guid(new ReadOnlySpan<byte>(data, offset, 16));
            entry.UniqueId = new Guid(new ReadOnlySpan<byte>(data, offset + 16, 16));
            entry.FirstSector = BitConverter.ToUInt64(data, offset + 32);
            entry.LastSector = BitConverter.ToUInt64(data, offset + 40);
            entry.Attributes = (GPTPartitionAttributes)BitConverter.ToUInt64(data, offset + 48);
            entry.Name = Encoding.Unicode.GetString(data, offset + 56, 72).TrimEnd('\0');
            return entry;
        }

        public void Write(byte[] data, int offset)
        {
            TypeGuid.ToByteArray().CopyTo(data, offset);
            UniqueId.ToByteArray().CopyTo(data, offset + 16);
            BitConverter.GetBytes(FirstSector).CopyTo(data, offset + 32);
            BitConverter.GetBytes(LastSector).CopyTo(data, offset + 40);
            BitConverter.GetBytes((ulong)Attributes).CopyTo(data, offset + 48);

            var nameBytes = Encoding.Unicode.GetBytes(Name);
            Array.Copy(nameBytes, 0, data, offset + 56, Math.Min(72, nameBytes.Length));
        }
    }
}
