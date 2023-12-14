using UnrealBuildTool;
using System.IO;

public class OnlineSubsystemUTcp : ModuleRules
{
	public OnlineSubsystemUTcp(ReadOnlyTargetRules Target) : base(Target)
	{
		CStandard = CStandardVersion.Latest;
		PrivatePCHHeaderFile = "Private/PrivatePCH.h";
		PublicDefinitions.Add("ONLINESUBSYSTEMUTILS_PACKAGE=1");
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
			new string[] {
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core", 
				"CoreUObject", 
				"NetCore",
				"Engine", 
				"EngineSettings",
				"ImageCore",
				"Sockets", 
				"Voice",
				"PacketHandler",
				"Json",
				"OnlineSubsystemUtils",
				"DeveloperSettings",
				"Networking",
			}
		);

		if (Target.Type == TargetType.Server || Target.Type == TargetType.Editor)
        {
            PrivateDependencyModuleNames.Add("PerfCounters");
        }
	}
}
