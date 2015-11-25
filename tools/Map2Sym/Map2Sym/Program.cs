using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Map2Sym
{
    class Program
    {
        static void Main(string[] args)
        {
            /* Print Header */
            Console.WriteLine("Map2Sym Tool for MollenOS - Version 0.1 Alpha");
            Console.WriteLine("Written by Philip Meulengracht");
            Console.WriteLine("");

            /* Load all *.map */
            String[] Files = Directory.GetFiles(AppDomain.CurrentDomain.BaseDirectory, "*.map");

            /* Match VS map */
            Regex VsMatch = new Regex(@"[0-9]+[:][0-9a-zA-Z]+[\s]+[_a-zA-Z0-9]+[\s]+[0-9a-zA-Z]+[\s]+[a-zA-Z0-9:\.]+");

            /* Iterate */
            foreach (String mFile in Files)
            {
                /* Open as streamreader */
                using (StreamReader mReader = new StreamReader(mFile))
                using (StreamWriter sWriter = new StreamWriter(Path.GetFileNameWithoutExtension(mFile) + ".sym", false))
                {
                    String Line = "";
                    Boolean First = true;
                    UInt64 BaseAddress = 0;
                    String BaseStr = "Preferred load address is ";

                    /* Read untill end */
                    while ((Line = mReader.ReadLine()) != null)
                    {
                        /* Base Addr? */
                        if (Line.Contains(BaseStr))
                        {
                            /* Parse */
                            String bAddrStr = Line.Substring(BaseStr.Length);
                            BaseAddress = Convert.ToUInt64(bAddrStr.Trim().TrimStart(new Char[] { '0' }), 16);
                        }

                        if (VsMatch.IsMatch(Line))
                        {
                            /* Parse */
                            String[] lTokens = Line.Split(new Char[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);

                            /* Should be 4 entries */
                            if (lTokens.Length == 4)
                            {
                                if (First)
                                    First = false;
                                else
                                    sWriter.Write("|");

                                /* Modify Addr */
                                UInt64 Addr = Convert.ToUInt64(lTokens[2].TrimStart(new Char[] { '0' }), 16);
                                UInt64 RelAddr = Addr - BaseAddress;

                                /* Write data */
                                sWriter.Write(lTokens[1].Substring(1) + "|" + String.Format("{0:x2}", RelAddr) + "|" + lTokens[3].Replace(".obj", ""));
                            }
                        }
                    }
                }
            }
        }
    }
}
