using System;

namespace OSBuilder.FileSystems.MFS
{
    [Flags]
    public enum PartitionFlags
    {
        SystemDrive = 0x1,
        DataDrive = 0x2,
        UserDrive = 0x4,
        HiddenDrive = 0x8,
        JournalCheck = 0x10
    };
}
