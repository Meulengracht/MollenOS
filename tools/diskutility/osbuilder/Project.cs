using System.IO;
using System.Collections.Generic;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace OSBuilder
{
    public class ProjectPartition
    {
        public string Label { get; set; }
        public string Type { get; set; }
        public List<string> Attributes { get; set; }
        public string Size { get; set; }
        public List<string> Sources { get; set; }
    }

    public class ProjectConfiguration
    {
        public string Scheme { get; set; }
        public string Bootloader { get; set; }
        public string Size { get; set; }
        public List<ProjectPartition> Partitions { get; set; }

        public static ProjectConfiguration Parse(string path)
        {
            var data = File.ReadAllText(path);
            var deserializer = new DeserializerBuilder()
                .WithNamingConvention(HyphenatedNamingConvention.Instance)
                .Build();

            var projectConfiguration = deserializer.Deserialize<ProjectConfiguration>(data);
            return projectConfiguration;
        }
    }
}
