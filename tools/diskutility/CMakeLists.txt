project (Vali-Diskutility)
enable_language (CSharp)
set (CMAKE_CSharp_FLAGS "/langversion:7")

# Gather sources
set (DU_SOURCES 
	mfstool/Program.cs
	mfstool/Properties/AssemblyInfo.cs

	mfstool/disk/disk.cs
	mfstool/disk/filesystems/ifilesystem.cs
	mfstool/disk/filesystems/mfs.cs
	mfstool/disk/schemes/idiskscheme.cs
	mfstool/disk/schemes/mbr.cs)

# Set the target link executable
add_executable (diskutility ${DU_SOURCES})

# Set configuration flags
set_property (TARGET diskutility PROPERTY VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.6.1")
set_property (TARGET diskutility PROPERTY VS_DOTNET_REFERENCES
    "Microsoft.CSharp"
    "System"
    "System.Core"
    "System.Management"
    "System.Xml.Linq"
    "System.Data"
    "System.Data.DataSetExtensions")
set_property (TARGET diskutility PROPERTY VS_DOTNET_REFERENCE_DiscUtils DiscUtils.dll)
add_custom_command(TARGET diskutility POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/DiscUtils.Common.dll"
        $<TARGET_FILE_DIR:diskutility>)
add_custom_command(TARGET diskutility POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/DiscUtils.MSBuild.dll"
        $<TARGET_FILE_DIR:diskutility>)