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

        static void InstallMollenOS(FileSystems.IFileSystem systemFileSystem, FileSystems.IFileSystem dataFileSystem)
        {
            // Install neccessary boot stuff
            systemFileSystem.MakeBoot();

            // Install files into each partition
            InstallFolder(systemFileSystem, "deploy/hdd/system", FileSystems.FileFlags.System);
            InstallFolder(dataFileSystem, "deploy/hdd/shared", FileSystems.FileFlags.None);
        }

        /* LaunchCLI
         * Launches the CLI and provides commands for manipluating disks */
        static void LaunchCLI(Hashtable drives)
        {
            Console.WriteLine("\nAvailable Commands:");
            Console.WriteLine("format <drive>");
            Console.WriteLine("write <file> <drive>");
            Console.WriteLine("ls <path> <drive>");
            Console.WriteLine("install <drive>");
            Console.WriteLine("quit");
            Console.WriteLine("");

            while (true)
            {
                Console.Write(" $ ");

                String input = Console.ReadLine();
                String[] inputTokens = input.Split(new Char[] { ' ' });

                switch (inputTokens[0].ToLower())
                {
                    case "format":
                        {
                            if (!int.TryParse(inputTokens[1], out int drive)) {
                                Console.WriteLine("Invalid drive number");
                                continue;
                            }
                            //Format((MfsDisk)Drives[Option]);
                        }
                        break;
                    case "write":
                        {
                            string path = inputTokens[1];
                            if (!int.TryParse(inputTokens[2], out int drive)) {
                                Console.WriteLine("Invalid drive number");
                                continue;
                            }
                            //WriteToMfs((MfsDisk)Drives[Option], Path, "");
                        }
                        break;
                    case "ls":
                        {
                            string path = inputTokens[1];
                            if (!int.TryParse(inputTokens[2], out int drive)) {
                                Console.WriteLine("Invalid drive number");
                                continue;
                            }
                            //ListDirectory((MfsDisk)Drives[Option], Path);
                        }
                        break;
                    case "install":
                        {
                            if (!int.TryParse(inputTokens[1], out int drive)) {
                                Console.WriteLine("Invalid drive number");
                                continue;
                            }
                            //InstallMOS((MfsDisk)Drives[Option]);

                        }
                        break;
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

        static int SilentInstall(Hashtable drives, string installationType, string diskLayout, string diskSize)
        {
            FileSystems.IFileSystem systemFileSystem, dataFileSystem;
            DiskLayouts.IDiskScheme diskScheme;
            Disk disk;
            ulong diskSizeInMBytes = GetMByteCountFromString(diskSize);
            ulong diskSectorCount = GetSectorCountFromMB(512, diskSizeInMBytes);
            Console.WriteLine("size of disk: " + diskSizeInMBytes.ToString() + "mb");
            Console.WriteLine("sector count: " + diskSectorCount.ToString());

            // Which kind of target?
            if (installationType.ToLower() == "live" && drives.Count > 0)
                disk = (Disk)drives[0];
            else if (installationType.ToLower() == "vmdk")
                disk = new Disk("VMDK", 512, 63, 255, diskSectorCount);
            else if (installationType.ToLower() == "img")
                disk = new Disk("IMG", 512, 63, 255, diskSectorCount);
            else {
                Console.WriteLine("Invalid option for -target");
                return -1;
            }

            // Which kind of disk-scheme?
            if (diskLayout.ToLower() == "mbr")
                diskScheme = new DiskLayouts.MBR();
            else if (diskLayout.ToLower() == "gpt")
                diskScheme = null;
            else {
                Console.WriteLine("Invalid option for -scheme");
                return -1;
            }

            systemFileSystem = new FileSystems.MFS.FileSystem("vali-system", true, 
                FileSystems.MFS.PartitionFlags.SystemDrive);
            dataFileSystem = new FileSystems.MFS.FileSystem("vali-data", false, 
                FileSystems.MFS.PartitionFlags.DataDrive | FileSystems.MFS.PartitionFlags.UserDrive);

            // Setup disk partition layout
            diskScheme.Create(disk);

            // reserve 128mb for the system partition
            diskScheme.AddPartition(systemFileSystem, GetSectorCountFromMB((int)disk.BytesPerSector, 128));

            // add rest to the data partition
            diskScheme.AddPartition(dataFileSystem, diskScheme.GetFreeSectorCount());

            // install the OS
            InstallMollenOS(systemFileSystem, dataFileSystem);

            // finalize the disk layout
            diskScheme.Finalize();
            return 0;
        }

        static void Main(string[] args)
        {
            var installationType = "live";
            var diskLayout = "mbr";
            var diskSize = "2gb";
            var silentInstall = false;

            // Initialize DiscUtils
            DiscUtils.Complete.SetupHelper.SetupComplete();

            // Debug print header
            Console.WriteLine("MFS Utility Software");
            Console.WriteLine("Software Capabilities include formatting, read, write to/from MFS.\n");

            // Parse arguments
            if (args != null && args.Length > 0) {
                for (int i = 0; i < args.Length; i++) {
                    switch (args[i].ToLower()) {
                        case "-auto":
                        case "-a": {
                                silentInstall = true;
                            } break;

                        case "-target": {
                                installationType = args[i + 1];
                                i++;
                            } break;

                        case "-scheme": {
                                diskLayout = args[i + 1];
                                i++;
                            } break;

                        case "-size": {
                                diskSize = args[i + 1];
                                i++;
                            } break;
                    }
                }
            }
            
            // get a list of available drives for installation
            var drives = GetDrives();
            if (silentInstall)
                SilentInstall(drives, installationType, diskLayout, diskSize.ToUpper());
            else
                LaunchCLI(drives);
        }
    }
}
