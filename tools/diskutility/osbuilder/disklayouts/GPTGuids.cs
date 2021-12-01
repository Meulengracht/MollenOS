using System;

namespace OSBuilder.DiskLayouts
{
    public static class GPTGuids
    {
        public static readonly Guid BiosBootPartition = new Guid("21686148-6449-6E6F-744E-656564454649");
        public static readonly Guid EfiSystemPartition = new Guid("C12A7328-F81F-11D2-BA4B-00A0C93EC93B");
        public static readonly Guid ValiSystemPartition = new Guid("C4483A10-E3A0-4D3F-B7CC-C04A6E16612B");
        public static readonly Guid ValiDataUserPartition = new Guid("80C6C62A-B0D6-4FF4-A69D-558AB6FD8B53");
        public static readonly Guid ValiUserPartition = new Guid("8874F880-E7AD-4EE2-839E-6FFA54F19A72");
        public static readonly Guid ValiDataPartition = new Guid("B8E1A523-5865-4651-9548-8A43A9C21384");
    }
}
