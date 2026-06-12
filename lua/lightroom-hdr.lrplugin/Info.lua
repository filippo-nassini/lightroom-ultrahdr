--[[----------------------------------------------------------------------------

Info.lua
Manifest for the Lightroom → UltraHDR gain-map plugin.

Registers a Library context-menu / Plug-in Extras item that fuses a selected
SDR rendition and its HDR rendition into a single gain-map JPEG (UltraHDR /
ISO 21496-1) by shelling out to the bundled `uhdrtool` binary.

------------------------------------------------------------------------------]]

return {

	LrSdkVersion = 6.0,
	LrSdkMinimumVersion = 6.0,

	LrToolkitIdentifier = 'com.github.lightroomhdr.ultrahdr',
	LrPluginName = LOC "$$$/LightroomHdr/PluginName=Lightroom UltraHDR",

	-- Shown as a clickable link on the plugin's page in the Plug-in Manager.
	LrPluginInfoUrl = "https://github.com/filippo-nassini/lightroom-ultrahdr",

	-- Appears under File > Plug-in Extras (LrExportMenuItems is the entry that
	-- populates that submenu; it works on the current photo selection).
	LrExportMenuItems = {
		{
			title = LOC "$$$/LightroomHdr/MergeTitle=Merge SDR + HDR to UltraHDR…",
			file  = "MergeMenuItem.lua",
		},
	},

	VERSION = { major = 1, minor = 0, revision = 0, build = 0 },
}
