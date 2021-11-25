using System;

namespace OSBuilder.DiskLayouts
{
    public class GPTHeader
    {
        public string Signature = "EFI PART";
        public uint Revision = 0x00010000;
        public uint HeaderSize = 92;
        public uint HeaderCRC32 = 0;
        public uint Reserved = 0;
        public ulong CurrentLBA = 1;
        public ulong BackupLBA = 0xFFFFFFFF;
        public ulong FirstUsableLBA = 0xFFFFFFFF;
        public ulong LastUsableLBA = 0xFFFFFFFF;
        public Guid DiskGUID = Guid.NewGuid();
        public ulong PartitionEntryLBA = 0xFFFFFFFF;
        public uint NumberOfPartitionEntries = 0;
        public uint SizeOfPartitionEntry = 128;
        public uint PartitionEntryArrayCRC32 = 0;

        static bool Parse(byte[] data, GPTHeader header)
        {
            Crc32 crc32 = new Crc32();

            header.Signature = System.Text.Encoding.ASCII.GetString(data, 0, 8);
            if (header.Signature != "EFI PART")
            {
                return false;
            }

            header.Revision = BitConverter.ToUInt32(data, 8);
            if (header.Revision != 0x00010000)
            {
                return false;
            }

            header.HeaderSize = BitConverter.ToUInt32(data, 12);
            if (header.HeaderSize != 92)
            {
                return false;
            }

            header.HeaderCRC32 = BitConverter.ToUInt32(data, 16);
            header.Reserved = BitConverter.ToUInt32(data, 20);
            if (header.Reserved != 0)
            {
                return false;
            }

            header.CurrentLBA = BitConverter.ToUInt64(data, 24);
            header.BackupLBA = BitConverter.ToUInt64(data, 32);

            header.FirstUsableLBA = BitConverter.ToUInt64(data, 40);
            header.LastUsableLBA = BitConverter.ToUInt64(data, 48);

            header.DiskGUID = new Guid(new ReadOnlySpan<byte>(data, 56, 16));
            header.PartitionEntryLBA = BitConverter.ToUInt64(data, 72);
            header.NumberOfPartitionEntries = BitConverter.ToUInt32(data, 80);
            header.SizeOfPartitionEntry = BitConverter.ToUInt32(data, 84);
            if (header.SizeOfPartitionEntry != 128)
            {
                return false;
            }

            header.PartitionEntryArrayCRC32 = BitConverter.ToUInt32(data, 88);
            return true;
        }

        public static GPTHeader Parse(byte[] data)
        {
            GPTHeader header = new GPTHeader();
            if (!Parse(data, header))
                return null;
            return header;
        }

        public void Write(byte[] data)
        {
            Array.Copy(System.Text.Encoding.ASCII.GetBytes(Signature), 0, data, 0, 8);
            Array.Copy(BitConverter.GetBytes(Revision), 0, data, 8, 4);
            Array.Copy(BitConverter.GetBytes(HeaderSize), 0, data, 12, 4);
            Array.Copy(BitConverter.GetBytes(HeaderCRC32), 0, data, 16, 4);
            Array.Copy(BitConverter.GetBytes(Reserved), 0, data, 20, 4);
            Array.Copy(BitConverter.GetBytes(CurrentLBA), 0, data, 24, 8);
            Array.Copy(BitConverter.GetBytes(BackupLBA), 0, data, 32, 8);
            Array.Copy(BitConverter.GetBytes(FirstUsableLBA), 0, data, 40, 8);
            Array.Copy(BitConverter.GetBytes(LastUsableLBA), 0, data, 48, 8);
            Array.Copy(DiskGUID.ToByteArray(), 0, data, 56, 16);
            Array.Copy(BitConverter.GetBytes(PartitionEntryLBA), 0, data, 72, 8);
            Array.Copy(BitConverter.GetBytes(NumberOfPartitionEntries), 0, data, 80, 4);
            Array.Copy(BitConverter.GetBytes(SizeOfPartitionEntry), 0, data, 84, 4);
            Array.Copy(BitConverter.GetBytes(PartitionEntryArrayCRC32), 0, data, 88, 4);
        }
    }
}
