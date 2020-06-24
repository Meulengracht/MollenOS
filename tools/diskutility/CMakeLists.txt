project (Vali-Diskutility)

# Gather sources files for building the target
set (DU_SOURCES 
    mfstool/Program.cs
    mfstool/Properties/AssemblyInfo.cs

    mfstool/disk/disk.cs
    mfstool/disk/filesystems/ifilesystem.cs
    mfstool/disk/filesystems/mfs.cs
    mfstool/disk/schemes/idiskscheme.cs
    mfstool/disk/schemes/mbr.cs
)

if (MSVC)
    enable_language (CSharp)
    set (CMAKE_CSharp_FLAGS "/langversion:7")

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
    
    install(TARGETS diskutility EXPORT tools_diskutility DESTINATION bin)
    install(EXPORT tools_diskutility NAMESPACE du_ DESTINATION lib/tools_diskutility)    
    
    add_custom_command(TARGET diskutility POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/DiscUtils.Common.dll" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/DiscUtils.MSBuild.dll" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    )
else ()
    add_custom_target (diskutility ALL
        COMMAND mcs ${DU_SOURCES} /reference:System.Management.dll /reference:DiscUtils.dll -out:${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/diskutility
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        SOURCES ${DU_SOURCES}
    )
    file (GLOB DISCUTIL_BINARIES "*.dll")

    install(PROGRAMS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/diskutility DESTINATION bin)
    install(FILES ${DISCUTIL_BINARIES} DESTINATION bin)
 endif()
