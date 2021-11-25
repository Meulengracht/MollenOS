using System;

namespace OSBuilder.DiskLayouts
{
    public static class GPTGuids
    {
        public static readonly Guid BiosBootPartition = new Guid("21686148-6449-6E6F-744E-656564454649");
        public static readonly Guid EfiSystemPartition = new Guid("C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
        public static readonly Guid Fat32 = new Guid("21686148-6449-6E6F-744E-656564454649");
        public static readonly Guid Mfs = new Guid("59b777d8-6457-498c-a19d-11a68d1a46e3");
    }
}
