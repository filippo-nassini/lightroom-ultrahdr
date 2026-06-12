--[[----------------------------------------------------------------------------

BinaryLocator.lua

Resolve the path to the bundled `uhdrtool` binary for the current platform.

Layout inside the .lrplugin bundle (assembled by the packaging step, §9.1):
    <plugin>/bin/win/uhdrtool.exe
    <plugin>/bin/mac/uhdrtool

WIN_ENV / MAC_ENV are globals injected by the Lightroom Lua runtime.

------------------------------------------------------------------------------]]

local LrPathUtils = import 'LrPathUtils'
local LrFileUtils = import 'LrFileUtils'

local BinaryLocator = {}

-- Relative location and filename per platform.
local function platformRelativePath()
	if WIN_ENV then
		return { "bin", "win", "uhdrtool.exe" }
	else
		return { "bin", "mac", "uhdrtool" }
	end
end

--[[
Returns path, err.
  path : absolute path to the binary (whether or not it exists on disk)
  err  : nil if the file exists, otherwise a human-readable "not found" message

_PLUGIN.path is the absolute path to the .lrplugin bundle.
]]
function BinaryLocator.resolve()
	local parts = platformRelativePath()
	local path = _PLUGIN.path
	for _, p in ipairs(parts) do
		path = LrPathUtils.child(path, p)
	end

	if LrFileUtils.exists(path) then
		return path, nil
	end

	return path, "uhdrtool binary not found at:\n" .. path
		.. "\n\nThis build of the plugin does not include the native tool yet."
end

return BinaryLocator
