using System;

namespace OSBuilder.DiskLayouts
{
    [Flags]
    public enum GPTPartitionAttributes
    {
        None = 0,
        PlatformRequired = 0x0001,
        Ignore = 0x0002,
        LegacyBiosBootable = 0x0004,

        ReadOnly = (1 << 60),
        ShadowCopy = (1 << 61),
        Hidden = (1 << 62),
        NoAutoMount = (1 << 63)
    }
}
