--[[----------------------------------------------------------------------------

ToolRunner.lua

Build and run the `uhdrtool` command line via LrTasks.execute.

Contract (design D8): all inputs and the output are plain CLI arguments —
    uhdrtool --hdr <tiff> --sdr <jpg> --out <final.jpg> [--cgamut N] [--sgamut N]

Quoting: every path is wrapped in double quotes. On Windows the WHOLE command is
additionally wrapped in an outer pair of quotes, because cmd.exe strips one layer
(the classic `""a" "b""` behaviour). This matters here — paths can contain spaces.

Dry-run: if the binary is missing, return the exact command we WOULD run instead
of executing, so the Lua flow is fully testable before uhdrtool exists.

------------------------------------------------------------------------------]]

local LrTasks = import 'LrTasks'

local ToolRunner = {}

local function quote(s)
	return '"' .. s .. '"'
end

-- Map a gamut name to the uhdrtool integer flag value.
ToolRunner.CGAMUT = { ["sRGB_hdr"] = 0, ["p3_hdr"] = 1, ["Rec2020_hdr"] = 2 }
ToolRunner.SGAMUT = { ["sRGB"] = 0, ["DisplayP3"] = 1, ["Rec2020"] = 2 }

--[[
Build the command string.
  spec = { binary=, hdr=, sdr=, out=, cgamut=<int?>, sgamut=<int?> }
]]
function ToolRunner.buildCommand(spec)
	local parts = {
		quote(spec.binary),
		"--hdr", quote(spec.hdr),
		"--sdr", quote(spec.sdr),
		"--out", quote(spec.out),
	}
	if spec.cgamut then parts[#parts + 1] = "--cgamut"; parts[#parts + 1] = tostring(spec.cgamut) end
	if spec.sgamut then parts[#parts + 1] = "--sgamut"; parts[#parts + 1] = tostring(spec.sgamut) end

	local cmd = table.concat(parts, " ")
	if WIN_ENV then
		cmd = '"' .. cmd .. '"'   -- outer quotes for cmd.exe
	end
	return cmd
end

--[[
Run uhdrtool.
Returns a result table:
  { dryRun = true,  command = <string> }                         -- binary missing
  { ok = true,      command = <string> }                         -- exit 0
  { ok = false,     command = <string>, status = <int> }         -- non-zero exit

`binErr` is the BinaryLocator error (non-nil when the binary is absent).
]]
function ToolRunner.run(spec, binErr)
	local command = ToolRunner.buildCommand(spec)

	if binErr then
		return { dryRun = true, command = command, binErr = binErr }
	end

	local status = LrTasks.execute(command)
	if status == 0 then
		return { ok = true, command = command }
	end
	return { ok = false, command = command, status = status }
end

return ToolRunner
