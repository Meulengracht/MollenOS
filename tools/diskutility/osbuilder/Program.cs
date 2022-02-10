using System;
using System.Collections;
using System.IO;
using System.Linq;

namespace OSBuilder
{
    class Program
    {
        static ulong GetSectorCountFromMB(int bytesPerSector, ulong mb)
        {
            return (mb * 1024 * 1024) / (ulong)bytesPerSector;
        }

        static byte[] GetFileData(string path)
        {
            try
            {
                return File.ReadAllBytes(path);
            }
            catch (Exception)
            {
                Console.WriteLine($"Failed to read file: {path}");
                return null;
            }
        }

        static FileInfo GetFileInfo(string path)
        {
            try
            {
                return new FileInfo(path);
            }
            catch (Exception)
            {
                Console.WriteLine($"Failed to stat source: {path}");
                return null;
            }
        }

        static void InstallSource(FileSystems.IFileSystem fileSystem, ProjectSource source)
        {
            if (fileSystem == null)
                return;

            FileInfo fileInfo = GetFileInfo(source.Path);
            if (fileInfo == null)
                return;

            // strip leading path
            if (source.Target.StartsWith("/"))
                source.Target = source.Target.Substring(1);

            if (fileInfo.Attributes.HasFlag(FileAttributes.Directory))
            {
                Console.WriteLine($"Installing directory: {source.Path}");

                // create target directory
                fileSystem.CreateDirectory(source.Target, 0);

                // create directory structure first
                string[] directories = Directory.GetDirectories(source.Path, "*", SearchOption.AllDirectories);
                foreach (string dir in directories) {
                    // extract relative path from root to destination
                    var relativePath = dir.Substring(source.Path.Length + 1).Replace('\\', '/');
                    var dirToCreate = dir.Split(Path.DirectorySeparatorChar).Last();
                    var targetPath = source.Target + relativePath;

                    Console.WriteLine("Creating: " + targetPath + "  (" + dirToCreate + ")");
                    if (!fileSystem.CreateDirectory(targetPath, 0)) {
                        Console.WriteLine("Failed to create directory: " + dirToCreate);
                        return;
                    }
                }

                // Iterate through deployment folder and install system files
                string[] installationFiles = Directory.GetFiles(source.Path, "*", SearchOption.AllDirectories);
                foreach (string file in installationFiles) {
                    var relativePath = file.Substring(source.Path.Length + 1).Replace('\\', '/');
                    var targetPath = source.Target + relativePath;
                    
                    Console.WriteLine("Copying: " + targetPath);
                    if (!fileSystem.CreateFile(targetPath, 0, File.ReadAllBytes(file))) {
                        Console.WriteLine("Failed to install file: " + targetPath);
                        return;
                    }
                }
            }
            else
            {
                Console.WriteLine($"Installing file: {source.Path} => {source.Target}");
                var data = GetFileData(source.Path);
                if (data == null)
                {
                    Console.WriteLine($"Failed to read file: {source.Path}");
                    return;
                }
                
                var rootPath = Path.GetDirectoryName(source.Target);
                if (!string.IsNullOrEmpty(rootPath))
                {
                    if (!fileSystem.CreateDirectory(rootPath, 0))
                    {
                        Console.WriteLine("Failed to create directory: " + rootPath);
                        return;
                    }
                }

                if (!fileSystem.CreateFile(source.Target, 0, data))
                    Console.WriteLine($"Failed to write file: {source.Target}");
            }
        }

        static IDisk DetectDiskType(string diskPath)
        {
            if (diskPath.ToLower().EndsWith("img"))
                return new ImgDisk(diskPath);
            else if (diskPath.ToLower().EndsWith("vmdk"))
                return new VmdkDisk(diskPath);
            else
                return null;
        }

        static DiskLayouts.IDiskScheme DetectDiskScheme(IDisk disk)
        {
            byte[] sector = disk.Read(1, 1);
            DiskLayouts.IDiskScheme scheme;
            
            var gptSignature = System.Text.Encoding.ASCII.GetString(sector, 0, 8);
            if (gptSignature == "EFI PART")
                scheme = new DiskLayouts.GPT();
            else
                scheme = new DiskLayouts.MBR();
            return scheme;
        }

        /* LaunchCLI
         * Launches the CLI and provides commands for manipluating disks */
        static void LaunchCLI(Hashtable drives)
        {
            Console.WriteLine("\nAvailable Commands:");
            Console.WriteLine("open <path>");
            Console.WriteLine("select <filesystem>");
            Console.WriteLine("write <source> <target>");
            Console.WriteLine("ls <path>");
            Console.WriteLine("close");
            Console.WriteLine("quit");
            Console.WriteLine("");

            IDisk currentDisk = null;
            DiskLayouts.IDiskScheme currentScheme = null;
            FileSystems.IFileSystem currentFileSystem = null;

            while (true)
            {
                Console.Write(" $ ");

                String input = Console.ReadLine();
                String[] inputTokens = input.Split(new Char[] { ' ' });

                switch (inputTokens[0].ToLower())
                {
                    case "open":
                    {
                        string path = inputTokens[1];

                        // open disk
                        currentDisk = DetectDiskType(path);
                        if (!currentDisk.Open())
                        {
                            Console.WriteLine("Failed to open disk: " + path);
                            currentDisk = null;
                            break;
                        }
                        
                        Console.WriteLine("Opened disk: " + path);
                        
                        // open partitioning scheme
                        currentScheme = DetectDiskScheme(currentDisk);
                        if (!currentScheme.Open(currentDisk))
                        {
                            Console.WriteLine("Failed to open disk layout");
                            currentScheme = null;
                            currentDisk = null;
                        }
                        
                        var index = 0;
                        foreach (var fileSystem in currentScheme.GetFileSystems())
                        {
                            Console.WriteLine($"{index++}: " + fileSystem.GetName());
                        }
                    }
                    break;
                    case "select":
                    {
                        int index = int.Parse(inputTokens[1]);
                        if (index < 0 || index >= currentScheme.GetFileSystems().Count())
                        {
                            Console.WriteLine("Invalid filesystem index");
                            break;
                        }
                        
                        currentFileSystem = currentScheme.GetFileSystems().ElementAt(index);
                        Console.WriteLine("Selected filesystem: " + currentFileSystem.GetName());
                    } break;
                    case "write":
                    {
                        string sourcePath = inputTokens[1];
                        string targetPath = inputTokens[2];
                        if (currentFileSystem == null) {
                            Console.WriteLine("No filesystem selected");
                            continue;
                        }
                        currentFileSystem.CreateFile(targetPath, FileSystems.FileFlags.None, File.ReadAllBytes(sourcePath));
                    } break;
                    case "ls":
                    {
                        string path = inputTokens[1];
                        if (currentFileSystem == null) {
                            Console.WriteLine("No filesystem selected");
                            continue;
                        }
                        currentFileSystem.ListDirectory(path);
                    } break;
                    case "quit":
                        return;

                    default:
                        break;
                }
                GC.Collect();
            }
        }

        static Hashtable GetDrives()
        {
            Hashtable drives = new Hashtable();
            /*
            ManagementObjectSearcher searcher = new ManagementObjectSearcher("SELECT * FROM Win32_DiskDrive");
            int diskIndex = 0;
            Console.WriteLine("Available Drives:");
            foreach (ManagementObject o in searcher.Get())
            {
                // Never take main-drive into account
                if (o["DeviceID"].ToString().Contains("PHYSICALDRIVE0"))
                    continue;

                // Debug
                Console.WriteLine(diskIndex.ToString() + ". " + o["Caption"] + " (DeviceID = " + o["DeviceID"] + ")");

                var bps = o.GetPropertyValue("BytesPerSector");
                var spt = o["SectorsPerTrack"];
                var tpc = o["TracksPerCylinder"];
                var ts = o["TotalSectors"];

                // Create and store the disk
                drives.Add(diskIndex, new Disk((String)o["DeviceID"], (UInt32)o["BytesPerSector"],
                    (UInt32)o["SectorsPerTrack"], (UInt32)o["TracksPerCylinder"], (UInt64)o["TotalSectors"]));
                diskIndex++;
            }
            */
            return drives;
        }

        static ulong GetMByteCountFromString(string size)
        {
            if (size.EndsWith("MB"))
                return ulong.Parse(size.Substring(0, size.Length - 2));
            else if (size.EndsWith("GB"))
                return ulong.Parse(size.Substring(0, size.Length - 2)) * 1024;
            else if (size.EndsWith("TB"))
                return ulong.Parse(size.Substring(0, size.Length - 2)) * 1024 * 1024;
            else
                return ulong.Parse(size); // assume MB
        }

        static FileSystems.FileSystemAttributes GetFileSystemAttributes(
            System.Collections.Generic.List<string> attributes)
        {
            FileSystems.FileSystemAttributes attr = FileSystems.FileSystemAttributes.None;
            if (attributes == null)
                return attr;

            foreach (var attribute in attributes)
            {
                switch (attribute.ToLower())
                {
                    case "boot":
                        attr |= FileSystems.FileSystemAttributes.Boot;
                        break;
                    case "hidden":
                        attr |= FileSystems.FileSystemAttributes.Hidden;
                        break;
                    case "readonly":
                        attr |= FileSystems.FileSystemAttributes.ReadOnly;
                        break;
                    case "noautomount":
                        attr |= FileSystems.FileSystemAttributes.NoAutoMount;
                        break;
                    default:
                        break;
                }
            }
            return attr;
        }

        static FileSystems.IFileSystem CreateFileSystem(ProjectPartition partition)
        {
            FileSystems.IFileSystem fileSystem;
            var attributes = GetFileSystemAttributes(partition.Attributes);
            Guid guid;

            if (!string.IsNullOrEmpty(partition.Guid))
                guid = Guid.Parse(partition.Guid);
            else
                guid = Guid.NewGuid();

            switch (partition.Type.ToLower())
            {
                case "fat":
                    fileSystem = new FileSystems.FAT.FileSystem(partition.Label, guid, attributes);
                    break;
                case "mfs":
                    fileSystem = new FileSystems.MFS.FileSystem(partition.Label, guid, attributes);
                    break;
                default:
                    return null;
            }
            return fileSystem;
        }

        static DiskLayouts.IDiskScheme CreateDiskScheme(IDisk disk, ProjectConfiguration config)
        {
            DiskLayouts.IDiskScheme scheme;

            // Which kind of disk-scheme?
            if (config.Scheme.ToLower() == "mbr")
                scheme = new DiskLayouts.MBR();
            else if (config.Scheme.ToLower() == "gpt")
                scheme = new DiskLayouts.GPT();
            else {
                throw new Exception("Invalid schema specified in the model");
            }
            
            if (!scheme.Create(disk))
                throw new Exception("Failed to create disk scheme");
            return scheme;
        }

        static int SilentInstall(Hashtable drives, string installationType, ProjectConfiguration config)
        {
            IDisk disk;
            
            ulong diskSizeInMBytes = GetMByteCountFromString(config.Size);
            ulong diskSectorCount = GetSectorCountFromMB(512, diskSizeInMBytes);
            Console.WriteLine("size of disk: " + diskSizeInMBytes.ToString() + "mb");
            Console.WriteLine("sector count: " + diskSectorCount.ToString());
            if (diskSizeInMBytes < 512)
            {
                Console.WriteLine("Disk size must be at least 512MB");
                return -1;
            }

            // Which kind of target?
            if (installationType.ToLower() == "live" && drives.Count > 0)
                disk = (Win32Disk)drives[0];
            else if (installationType.ToLower() == "vmdk")
                disk = new VmdkDisk(512, diskSectorCount);
            else if (installationType.ToLower() == "img")
                disk = new ImgDisk(512, diskSectorCount);
            else {
                Console.WriteLine("Invalid option for -target");
                return -1;
            }


            // Create the disk
            if (!disk.Create()) {
                Console.WriteLine("Failed to create disk");
                return -1;
            }

            using (var diskScheme = CreateDiskScheme(disk, config))
            {
                foreach (var partition in config.Partitions)
                {
                    var fileSystem = CreateFileSystem(partition);
                    if (diskScheme.GetFreeSectorCount() == 0)
                        throw new Exception($"No free sectors left for partition {partition.Label}");

                    if (!string.IsNullOrEmpty(partition.Size))
                    {
                        var partitionSizeMb = GetMByteCountFromString(partition.Size);
                        var partitionSizeSectors = GetSectorCountFromMB((int)disk.Geometry.BytesPerSector, partitionSizeMb);
                        if (diskScheme.GetFreeSectorCount() < partitionSizeSectors)
                            throw new Exception($"Not enough free space for partition {partition.Label}");

                        diskScheme.AddPartition(fileSystem, partitionSizeSectors);
                    }
                    else
                        diskScheme.AddPartition(fileSystem, diskScheme.GetFreeSectorCount());
                    
                    foreach (var source in partition.Sources)
                    {
                        InstallSource(fileSystem, source);
                    }
                }
            }
            disk.Close();
            return 0;
        }

        static void Main(string[] args)
        {
            var installationType = "live";
            var projectFile = "";

            // Initialize DiscUtils
            DiscUtils.Complete.SetupHelper.SetupComplete();

            // Debug print header
            Console.WriteLine("OS Installation Utility Software");
            Console.WriteLine(" - use this tool to read, write and install MollenOS.\n");

            // Parse arguments
            if (args != null && args.Length > 0) {
                for (int i = 0; i < args.Length; i++) {
                    switch (args[i].ToLower()) {
                        case "--project": {
                                if (i + 1 < args.Length)
                                    projectFile = args[i + 1];
                                else
                                    Console.WriteLine("Missing argument for --project");
                                i++;
                            } break;

                        case "--target": {
                                if (i + 1 < args.Length)
                                    installationType = args[i + 1];
                                else
                                    Console.WriteLine("Missing argument for --target");
                                i++;
                            } break;
                    }
                }
            }
            
            // get a list of available drives for installation
            var drives = GetDrives();
            var configuration = ProjectConfiguration.Parse(projectFile);
            if (configuration != null)
                SilentInstall(drives, installationType, configuration);
            else
                LaunchCLI(drives);
        }
    }
}
