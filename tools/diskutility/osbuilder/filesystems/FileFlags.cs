using System;

namespace OSBuilder.FileSystems
{
    [Flags]
    public enum FileFlags
    {
        None = 0x0,
        Directory = 0x1,
        System = 0x2
    };
}
