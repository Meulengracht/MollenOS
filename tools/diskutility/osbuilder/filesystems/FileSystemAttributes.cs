using System;

namespace OSBuilder.FileSystems
{
    [Flags]
    public enum FileSystemAttributes
    {
        None = 0x0,
        Boot = 0x1,
        System = 0x2,
        Data = 0x4,
        User = 0x8,
        Hidden = 0x10,
        ReadOnly = 0x20,
        EFI = 0x40
    };
}
