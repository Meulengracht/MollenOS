using System.IO;

namespace OSBuilder.FileSystems
{
    public class TemporaryFileStream : FileStream
    {
        public TemporaryFileStream()
            : base(Path.GetTempFileName(), FileMode.Create, FileAccess.ReadWrite, FileShare.Read, 4096, FileOptions.DeleteOnClose) { }
        public TemporaryFileStream(FileAccess access)
            : base(Path.GetTempFileName(), FileMode.Create, access, FileShare.Read, 4096, FileOptions.DeleteOnClose) { }
        public TemporaryFileStream(FileAccess access, FileShare share)
            : base(Path.GetTempFileName(), FileMode.Create, access, share, 4096, FileOptions.DeleteOnClose) { }
        public TemporaryFileStream(FileAccess access, FileShare share, int bufferSize)
            : base(Path.GetTempFileName(), FileMode.Create, access, share, bufferSize, FileOptions.DeleteOnClose) { }
    }
}
