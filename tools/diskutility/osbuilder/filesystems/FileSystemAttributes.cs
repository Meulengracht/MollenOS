using System;

namespace OSBuilder.FileSystems
{
    [Flags]
    public enum FileSystemAttributes
    {
        None = 0x0,
        Boot = 0x1,
        Hidden = 0x2,
        ReadOnly = 0x4,
        NoAutoMount = 0x8
    };
}
