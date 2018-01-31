using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace DiskUtility
{
    class Program
    {
        /* InstallMollenOS
         * Installs mollenos on to the given filesystem and prepares
         * the proper boot-sectors for the partition */
        static void InstallMollenOS(IFileSystem FileSystem)
        {
            // Make the given filesystem boot
            if (!FileSystem.MakeBoot()) {
                Console.WriteLine("Failed to make the partition primary boot");
                return;
            }

            // Iterate through and create all directories
            String BaseRoot = "deploy/hdd/";
            Console.WriteLine("Creating system file tree");
            Console.WriteLine("Root: " + AppDomain.CurrentDomain.BaseDirectory + "deploy/hdd/");
            String[] Dirs = Directory.GetDirectories("deploy/hdd/", "*", SearchOption.AllDirectories);

            foreach (String pDir in Dirs) {
                String RelPath = pDir.Substring(BaseRoot.Length).Replace('\\', '/');
                String DirToCreate = pDir.Split(Path.DirectorySeparatorChar).Last();
                Console.WriteLine("Creating: " + RelPath + "  (" + DirToCreate + ")");

                // Create the directory
                if (!FileSystem.WriteFile(RelPath, FileFlags.Directory | FileFlags.System, null)) {
                    Console.WriteLine("Failed to create directory: " + DirToCreate);
                    return;
                }
            }

            // Iterate through deployment folder and install system files
            Console.WriteLine("Copying system files");
            String[] Files = Directory.GetFiles("deploy/hdd/", "*", SearchOption.AllDirectories);

            foreach (String pFile in Files) {
                String RelPath = pFile.Substring(BaseRoot.Length).Replace('\\', '/');
                Console.WriteLine("Copying: " + RelPath);

                // Create the file
                if (!FileSystem.WriteFile(RelPath, FileFlags.System, File.ReadAllBytes(pFile))) {
                    Console.WriteLine("Failed to install file: " + RelPath);
                    return;
                }
            }
        }

        /* LaunchCLI
         * Launches the CLI and provides commands for manipluating disks */
        static void LaunchCLI(Hashtable Drives)
        {
            Console.WriteLine("\nAvailable Commands:");
            Console.WriteLine("format <drive>");
            Console.WriteLine("write <file> <drive>");
            Console.WriteLine("ls <path> <drive>");
            Console.WriteLine("install <drive>");
            Console.WriteLine("quit");
            Console.WriteLine("");

            /* Cmd */
            while (true)
            {
                /* Spit */
                Console.Write(" $ ");

                /* Read */
                String Input = Console.ReadLine();
                String[] Tokens = Input.Split(new Char[] { ' ' });

                /* Lets see */
                switch (Tokens[0].ToLower())
                {
                    case "format":
                        {
                            /* Parse */
                            int Option = int.Parse(Tokens[1]);

                            /* Do the format */
                            //Format((MfsDisk)Drives[Option]);

                        }
                        break;
                    case "write":
                        {
                            /* Path */
                            String Path = Tokens[1];

                            /* Parse */
                            int Option = int.Parse(Tokens[2]);

                            /* Gogo */
                            //WriteToMfs((MfsDisk)Drives[Option], Path, "");

                        }
                        break;
                    case "ls":
                        {
                            /* Path */
                            String Path = Tokens[1];

                            /* Parse */
                            int Option = int.Parse(Tokens[2]);

                            /* Gogo */
                            //ListDirectory((MfsDisk)Drives[Option], Path);

                        }
                        break;
                    case "install":
                        {
                            /* Parse */
                            int Option = int.Parse(Tokens[1]);

                            /* Gogo */
                            //InstallMOS((MfsDisk)Drives[Option]);

                        }
                        break;
                    case "quit":
                        return;

                    default:
                        break;
                }

                /* Clean */
                GC.Collect();
            }
        }

        /* DiskUtility Entry 
         * Handles all command line switches and initializes the utility */
        static int Main(string[] args)
        {
            // Variables
            IDiskScheme Scheme;
            String Target = "live";
            String SchemeType = "mbr";
            bool Automatic = false;

            // Debug print header
            Console.WriteLine("MFS Utility Software");
            Console.WriteLine("Software Capabilities include formatting, read, write to/from MFS.\n");

            // Parse arguments
            if (args != null && args.Length > 0) {
                for (int i = 0; i < args.Length; i++) {
                    switch (args[i].ToLower()) {
                        case "-auto":
                        case "-a": {
                                Automatic = true;
                            } break;

                        case "-target": {
                                Target = args[i + 1];
                                i++;
                            } break;

                        case "-scheme": {
                                SchemeType = args[i + 1];
                                i++;
                            } break;
                    }
                }
            }
            
            // Retrieve a list of physical drives
            Hashtable Drives = new Hashtable();

            if (Target == "live") {
                Console.WriteLine("Available Drives:");
                WqlObjectQuery q = new WqlObjectQuery("SELECT * FROM Win32_DiskDrive");
                ManagementObjectSearcher res = new ManagementObjectSearcher(q);
                int Itr = 0;
                foreach (ManagementObject o in res.Get())
                {

                    // Never take main-drive into account
                    if (o["DeviceID"].ToString().Contains("PHYSICALDRIVE0"))
                        continue;

                    // Debug
                    Console.WriteLine(Itr.ToString() + ". " + o["Caption"] + " (DeviceID = " + o["DeviceID"] + ")");

                    var bps = o.GetPropertyValue("BytesPerSector");
                    var spt = o["SectorsPerTrack"];
                    var tpc = o["TracksPerCylinder"];
                    var ts = o["TotalSectors"];

                    // Create and store the disk
                    Drives.Add(Itr, new CDisk((String)o["DeviceID"], (UInt32)o["BytesPerSector"],
                        (UInt32)o["SectorsPerTrack"], (UInt32)o["TracksPerCylinder"], (UInt64)o["TotalSectors"]));
                    Itr++;
                }
            }
            
            // Should we automate the process?
            if (Automatic)
            {
                // Variables
                IFileSystem FileSystem;
                CDisk Disk = null;

                // Which kind of target?
                if (Target.ToLower() == "live" && Drives.Count > 0)
                    Disk = (CDisk)Drives[0];
                else if (Target.ToLower() == "vmdk")
                    Disk = new CDisk("VMDK", 512, 63, 255, 2097152);
                else if (Target.ToLower() == "img")
                    Disk = new CDisk("IMG", 512, 63, 255, 2097152);
                else {
                    Console.WriteLine("Invalid option for -target");
                    return -1;
                }

                // Which kind of disk-scheme?
                if (SchemeType.ToLower() == "mbr")
                    Scheme = new SchemeMBR();
                else if (SchemeType.ToLower() == "gpt")
                    Scheme = null;
                else {
                    Console.WriteLine("Invalid option for -scheme");
                    return -1;
                }

                // Partition setup?
                FileSystem = new CMollenOSFileSystem("MollenOS");

                // Setup disk partition layout
                Scheme.Create(Disk);

                // Create the requested partition setup
                Scheme.AddPartition(FileSystem, Scheme.GetFreeSectorCount());

                // Install mollenos on the disk
                InstallMollenOS(FileSystem);

                // Finish
                Scheme.Finalize();
            }

            // Launch CLI if none-automatic
            if (!Automatic)
                LaunchCLI(Drives);

            // Return
            return 0;
        }
    }
}
