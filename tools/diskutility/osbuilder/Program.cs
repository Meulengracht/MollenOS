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

        static void InstallFolder(FileSystems.IFileSystem fileSystem, string root, FileSystems.FileFlags fileFlags)
        {
            if (fileSystem == null)
                return;

            Console.WriteLine("Installing fs-root: " + System.IO.Directory.GetCurrentDirectory() + "/" + root);
            string[] directories = Directory.GetDirectories(root, "*", SearchOption.AllDirectories);

            Console.WriteLine("Creating structure");
            foreach (string dir in directories) {
                // extract relative path form root to destination
                string relativePath = dir.Substring(root.Length + 1).Replace('\\', '/');
                string dirToCreate = dir.Split(Path.DirectorySeparatorChar).Last();
                Console.WriteLine("Creating: " + relativePath + "  (" + dirToCreate + ")");

                // Create the directory
                if (!fileSystem.WriteFile(relativePath, FileSystems.FileFlags.Directory | fileFlags, null)) {
                    Console.WriteLine("Failed to create directory: " + dirToCreate);
                    return;
                }
            }

            // Iterate through deployment folder and install system files
            Console.WriteLine("Copying files");
            string[] installationFiles = Directory.GetFiles(root, "*", SearchOption.AllDirectories);

            foreach (string file in installationFiles) {
                string relativePath = file.Substring(root.Length + 1).Replace('\\', '/');
                Console.WriteLine("Copying: " + relativePath);

                // Create the file
                if (!fileSystem.WriteFile(relativePath, fileFlags, File.ReadAllBytes(file))) {
                    Console.WriteLine("Failed to install file: " + relativePath);
                    return;
                }
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
                        currentFileSystem.WriteFile(targetPath, FileSystems.FileFlags.None, File.ReadAllBytes(sourcePath));
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
            foreach (var attribute in attributes)
            {
                switch (attribute.ToLower())
                {
                    case "efi":
                        attr |= FileSystems.FileSystemAttributes.EFI;
                        break;
                    case "boot":
                        attr |= FileSystems.FileSystemAttributes.Boot;
                        break;
                    case "system":
                        attr |= FileSystems.FileSystemAttributes.System;
                        break;
                    case "data":
                        attr |= FileSystems.FileSystemAttributes.Data;
                        break;
                    case "user":
                        attr |= FileSystems.FileSystemAttributes.User;
                        break;
                    case "hidden":
                        attr |= FileSystems.FileSystemAttributes.Hidden;
                        break;
                    case "readonly":
                        attr |= FileSystems.FileSystemAttributes.ReadOnly;
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
            
            switch (partition.Type.ToLower())
            {
                case "fat":
                    fileSystem = new FileSystems.FAT.FileSystem(partition.Label, attributes);
                    break;
                case "mfs":
                    fileSystem = new FileSystems.MFS.FileSystem(partition.Label, attributes);
                    break;
                default:
                    return null;
            }
            return fileSystem;
        }

        static int SilentInstall(Hashtable drives, string installationType, ProjectConfiguration config)
        {
            DiskLayouts.IDiskScheme diskScheme;
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

            // Which kind of disk-scheme?
            if (config.Scheme.ToLower() == "mbr")
                diskScheme = new DiskLayouts.MBR();
            else if (config.Scheme.ToLower() == "gpt")
                diskScheme = new DiskLayouts.GPT();
            else {
                Console.WriteLine("Invalid option for -scheme");
                return -1;
            }

            // Create the disk
            if (!disk.Create()) {
                Console.WriteLine("Failed to create disk");
                return -1;
            }

            diskScheme.Create(disk);
            foreach (var partition in config.Partitions)
            {
                var fileSystem = CreateFileSystem(partition);
                if (diskScheme.GetFreeSectorCount() == 0)
                    throw new Exception($"No free sectors left for partition {partition.Label}");

                if (!string.IsNullOrEmpty(partition.Size))
                {
                    var partitionSizeMb = GetMByteCountFromString(partition.Size);
                    var partitionSizeSectors = GetSectorCountFromMB((int)disk.BytesPerSector, partitionSizeMb);
                    if (diskScheme.GetFreeSectorCount() < partitionSizeSectors)
                        throw new Exception($"Not enough free space for partition {partition.Label}");

                    diskScheme.AddPartition(fileSystem, partitionSizeSectors);
                }
                else
                    diskScheme.AddPartition(fileSystem, diskScheme.GetFreeSectorCount());
                
                foreach (var source in partition.Sources)
                {
                    InstallFolder(fileSystem, source, 0);
                }
            }

            diskScheme.Finalize();
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
