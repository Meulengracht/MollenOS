using System;

namespace OSBuilder.FileSystems.MFS
{
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
}
