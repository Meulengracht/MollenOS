using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace RdBuilder
{
    class Program
    {
        static void Main(string[] args)
        {
            /* Print Header */
            Console.WriteLine("RamDisk Builder for MollenOS - Version 0.1 Alpha");
            Console.WriteLine("Written by Philip Meulengracht");
            Console.WriteLine("");

            /* Get architecture */
            String Arch = "";
//            if (args != null && args.Length >= 1 && args[0].Length > 1 && ((int)args[0][0] == 45 || (int)args[0][0] == 47))
//            {
//                switch (args[0].Substring(1).ToLower())
//                {
//                    case "arch":
//                        {
//                            /* Sanity */
//                            if (args.Length < 2)
//                            {
//                                /* Default to x86 */
//                                Arch = "x86";
//                            }
//                            else
//                                Arch = args[1];
//                        } break;
//                }
//            }
//            else
//            {
//                /* Default to x86 */
//                Arch = "x86";
//            }
 
            /* Get a list of all *.mod */
            String[] lFiles = Directory.GetFiles(Arch + "Build/", "*.mod");

            /* Iterate */
            Console.WriteLine("Drivers Found:");
            Console.WriteLine("----------------------------");
            foreach (String mFile in lFiles)
            {
                /* Debug */
                Console.WriteLine(mFile);
            }

            /* Info */
            Console.WriteLine("");
            Console.WriteLine("Building RamDisk...");

            /* Open File */
            using (FileStream rdWriter = new FileStream("InitRd.mos", FileMode.Create))
            {
                /* Setup Header */
                Byte[] Header = new Byte[16];

                /* Magic - 0x3144524D */
                Header[0] = 0x4D;
                Header[1] = 0x52;
                Header[2] = 0x44;
                Header[3] = 0x31;

                /* Version - 0x1 */
                Header[4] = 0x1;
                Header[5] = 0;
                Header[6] = 0;
                Header[7] = 0;

                /* Arch - x86_32 = 0x08, x86_64 = 0x10 */
                Header[8] = 0x8;
                Header[9] = 0;
                Header[10] = 0;
                Header[11] = 0;

                /* FileCount */
                Header[12] = (Byte)(lFiles.Length & 0xFF);
                Header[13] = (Byte)((lFiles.Length >> 8) & 0xFF);
                Header[14] = (Byte)((lFiles.Length >> 16) & 0xFF);
                Header[15] = (Byte)((lFiles.Length >> 24) & 0xFF);

                /* Write Header */
                rdWriter.Write(Header, 0, 16);

                /* Write emptiness */
                Byte[] Emptiness = new Byte[0x1000 - 16];
                for (int i = 0; i < Emptiness.Length; i++)
                    Emptiness[i] = 0;

                /* Save entry pos */
                long fEntryPos = rdWriter.Position;

                /* Write empty space */
                rdWriter.Write(Emptiness, 0, Emptiness.Length);
                rdWriter.Flush();

                /* Save data pos */
                long fDataPos = rdWriter.Position;

                /* First we write file entries */
                foreach (String mFile in lFiles)
                {
                    Console.WriteLine("Writing " + Path.GetFileName(mFile));

                    /* Convert name to bytes */
                    Byte[] NameData = new Byte[64];
                    Byte[] StringBytes = Encoding.UTF8.GetBytes(Path.GetFileName(mFile));

                    /* Copy over */
                    for (int i = 0; i < StringBytes.Length; i++)
                        NameData[i] = StringBytes[i];

                    /* Seek to entry */
                    rdWriter.Seek(fEntryPos, SeekOrigin.Begin);

                    /* Write name bytes */
                    rdWriter.Write(NameData, 0, NameData.Length);

                    /* Write Type (0x4 = Module, 0x1 = File(Generic)) */
                    rdWriter.WriteByte(0x4);
                    rdWriter.WriteByte(0);
                    rdWriter.WriteByte(0);
                    rdWriter.WriteByte(0);

                    /* Write Data Pos */
                    rdWriter.WriteByte((Byte)(fDataPos & 0xFF));
                    rdWriter.WriteByte((Byte)((fDataPos >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((fDataPos >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((fDataPos >> 24) & 0xFF));
                    rdWriter.Flush();

                    /* Update entry pos */
                    fEntryPos = rdWriter.Position;

                    /* Seek to Data */
                    rdWriter.Seek(fDataPos, SeekOrigin.Begin);

                    /* Write Module */

                    /* Step 1. Write Name */
                    Byte[] ModNameBytes = Encoding.UTF8.GetBytes(Path.GetFileNameWithoutExtension(mFile));
                    for (int i = 0; i < NameData.Length; i++)
                        NameData[i] = 0;

                    /* Copy over */
                    for (int i = 0; i < StringBytes.Length; i++)
                        NameData[i] = StringBytes[i];

                    /* Write name bytes */
                    rdWriter.Write(NameData, 0, NameData.Length);

                    /* Load Device Info */
                    uint VendorId = 0;
                    uint DeviceId = 0;
                    uint DeviceClass = 0;
                    uint DeviceSubClass = 0;
                    uint DeviceFlags = 0;

                    using (StreamReader diReader = new StreamReader(Path.GetDirectoryName(mFile) + "\\" + Path.GetFileNameWithoutExtension(mFile) + ".mdrv"))
                    {
                        String Line = "";
                        while ((Line = diReader.ReadLine()) != null)
                        {
                            /* Comment? */
                            if (Line.StartsWith("#"))
                                continue;

                            /* Split into key-value */
                            String[] Tokens = Line.Split(new Char[] { '=' });

                            /* Sanity */
                            if (Tokens.Length != 2)
                                continue;

                            /* Vendor Id ? */
                            if (Tokens[0].Trim().ToLower() == "vendorid")
                                VendorId = Convert.ToUInt32(Tokens[1].Trim().ToLower(), 16);

                            /* Device Id ? */
                            if (Tokens[0].Trim().ToLower() == "deviceid")
                                DeviceId = Convert.ToUInt32(Tokens[1].Trim().ToLower(), 16);

                            /* Device Class ? */
                            if (Tokens[0].Trim().ToLower() == "class")
                                DeviceClass = Convert.ToUInt32(Tokens[1].Trim().ToLower(), 16);

                            /* Device SubClass ? */
                            if (Tokens[0].Trim().ToLower() == "subclass")
                                DeviceSubClass = Convert.ToUInt32(Tokens[1].Trim().ToLower(), 16);
                            
                            /* Device Shared ? */
                            if (Tokens[0].Trim().ToLower() == "shared")
                                DeviceFlags = Convert.ToUInt32(Tokens[1].Trim().ToLower(), 10);
                        }

                        /* Cleanup */
                        diReader.Close();
                    }

                    /* Write Vendor Id */
                    rdWriter.WriteByte((Byte)(VendorId & 0xFF));
                    rdWriter.WriteByte((Byte)((VendorId >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((VendorId >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((VendorId >> 24) & 0xFF));

                    /* Write Driver Id */
                    rdWriter.WriteByte((Byte)(DeviceId & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceId >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceId >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceId >> 24) & 0xFF));

                    /* Write Driver Class */
                    rdWriter.WriteByte((Byte)(DeviceClass & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceClass >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceClass >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceClass >> 24) & 0xFF));

                    /* Write Driver SubClass */
                    rdWriter.WriteByte((Byte)(DeviceSubClass & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceSubClass >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceSubClass >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceSubClass >> 24) & 0xFF));

                    /* Write Driver Flags */
                    rdWriter.WriteByte((Byte)(DeviceFlags & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceFlags >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceFlags >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((DeviceFlags >> 24) & 0xFF));

                    /* Load file data */
                    Byte[] FileData = File.ReadAllBytes(mFile);

                    /* Write Length */
                    rdWriter.WriteByte((Byte)(FileData.Length & 0xFF));
                    rdWriter.WriteByte((Byte)((FileData.Length >> 8) & 0xFF));
                    rdWriter.WriteByte((Byte)((FileData.Length >> 16) & 0xFF));
                    rdWriter.WriteByte((Byte)((FileData.Length >> 24) & 0xFF));

                    /* Write Data */
                    rdWriter.Write(FileData, 0, FileData.Length);
                    rdWriter.Flush();

                    /* Update position */
                    fDataPos = rdWriter.Position;

                    /* Done */
                }

                /* Done, Cleanup */
                rdWriter.Flush();
                rdWriter.Close();
            }

            /* Info */
            Console.WriteLine("Done!");

            /* Done */
            while (true)
                Thread.Sleep(10 * 1000);
        }
    }
}
