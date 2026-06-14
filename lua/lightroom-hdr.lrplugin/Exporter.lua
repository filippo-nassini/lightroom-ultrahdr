--[[----------------------------------------------------------------------------

Exporter.lua

Export a single photo to a working file via LrExportSession.

Uses the single-iterator renditions{} pattern (calling doExportOnNewTask as well
trips the SDK's "cannot call twice on same session" guard). Must run inside an
async task (waitForRender yields).

HDR TIFF keys (LR_enableHDRDisplay / export_bitDepth=32 / maximumCompatibility=
false / the HDR colorSpace strings) are the verified set for LrC 15.3; the HDR
colour-space strings have inconsistent casing and are copied verbatim.

------------------------------------------------------------------------------]]

local LrExportSession = import 'LrExportSession'
local LrPathUtils     = import 'LrPathUtils'
local LrFileUtils     = import 'LrFileUtils'

local Exporter = {}

-- Name of the dedicated temp subfolder that holds the intermediate renditions.
-- A single fixed folder (reset at the start of each run by MergeMenuItem) keeps
-- the worst-case footprint bounded to one job's files even if end-cleanup fails.
Exporter.SCRATCH_FOLDER_NAME = "lightroom-hdr"

-- Verbatim HDR colour-space strings (casing matters; guessing fails silently).
Exporter.HDR_COLORSPACE = {
	["sRGB_hdr"]    = "sRGB_hdr",     -- HDR sRGB (Rec. 709)
	["p3_hdr"]      = "p3_hdr",       -- HDR Display P3
	["Rec2020_hdr"] = "Rec2020_hdr",  -- HDR Rec. 2020
}

-- Absolute path to the dedicated temp subfolder, created if absent. Both
-- intermediate renditions export here; MergeMenuItem wipes it at the start of
-- each run so leftovers from a failed cleanup never accumulate in the temp root.
function Exporter.scratchDir()
	local dir = LrPathUtils.child(LrPathUtils.getStandardFilePath('temp'), Exporter.SCRATCH_FOLDER_NAME)
	if not LrFileUtils.exists(dir) then
		LrFileUtils.createAllDirectories(dir)
	end
	return dir
end

-- Common export-and-wait. `settings` must include LR_format and destination.
-- Returns renderedPath, err.
local function exportOne(photo, settings)
	local session = LrExportSession {
		photosToExport = { photo },
		exportSettings = settings,
	}

	local renderedPath, err
	for _, rendition in session:renditions{ stopIfCanceled = true } do
		local ok, pathOrErr = rendition:waitForRender()
		if ok then renderedPath = pathOrErr else err = pathOrErr end
		break  -- single photo
	end
	return renderedPath, err
end

local function baseSettings(destDir, basename)
	return {
		LR_export_destinationType     = "specificFolder",
		LR_export_destinationPathPrefix = destDir,
		LR_export_useSubfolder        = false,
		LR_renamingTokensOn           = true,
		LR_tokens                     = "{{custom_token}}",
		LR_tokenCustomString          = basename,
		LR_collisionHandling          = "overwrite",
		LR_size_doConstrain           = false,
		LR_outputSharpeningOn         = false,
		LR_metadata_keywordOptions    = "lightroomHierarchical",
	}
end

-- Apply an optional short-edge resize (in pixels) to an export settings table.
-- When `shortEdgePx` is a positive number, both renditions of the same source are
-- constrained to the same short-edge length, so they come out the same size and
-- uhdrtool's dimension-match precondition still holds. The LR keys are verified
-- against a saved export preset: the short-edge length lives in LR_size_maxHeight;
-- LR_size_maxWidth is ignored in shortEdge mode but set defensively. When
-- `shortEdgePx` is nil/non-positive the settings keep baseSettings' no-resize state.
local function applyResize(settings, shortEdgePx)
	if type(shortEdgePx) == "number" and shortEdgePx > 0 then
		local px = math.floor(shortEdgePx)
		settings.LR_size_doConstrain  = true
		settings.LR_size_resizeType   = "shortEdge"
		settings.LR_size_units        = "pixels"
		settings.LR_size_maxHeight    = px
		settings.LR_size_maxWidth     = px
		settings.LR_size_doNotEnlarge = false
	end
	return settings
end

--[[
Export the HDR rendition as a 32-bit float HDR TIFF.
  hdrColorSpace : key into Exporter.HDR_COLORSPACE (default "sRGB_hdr")
  shortEdgePx   : optional positive short-edge resize in pixels (nil = full res)
Returns path, err.
]]
function Exporter.exportHdrTiff(photo, basename, hdrColorSpace, shortEdgePx)
	local dir = Exporter.scratchDir()
	local cs  = Exporter.HDR_COLORSPACE[hdrColorSpace or "sRGB_hdr"] or "sRGB_hdr"

	local settings = baseSettings(dir, basename)
	settings.LR_format                = "TIFF"
	settings.LR_export_bitDepth       = 32
	settings.LR_enableHDRDisplay      = true
	settings.LR_export_colorSpace     = cs
	settings.LR_maximumCompatibility  = false
	settings.LRtiff_compressionMethod = "compressionMethod_None"
	applyResize(settings, shortEdgePx)

	return exportOne(photo, settings)
end

--[[
Export the SDR rendition as a JPEG.
  sdrColorSpace : "sRGB" (default), "DisplayP3", or "Rec2020"
                  (verbatim Lightroom LR_export_colorSpace strings for 8-bit JPEG;
                  note Display P3 is "DisplayP3" here, NOT the HDR TIFF's "p3_hdr")
  shortEdgePx   : optional positive short-edge resize in pixels (nil = full res)
  quality       : 0.0–1.0 (default 0.92)
Returns path, err.
]]
function Exporter.exportSdrJpeg(photo, basename, sdrColorSpace, shortEdgePx, quality)
	local dir = Exporter.scratchDir()

	local settings = baseSettings(dir, basename)
	settings.LR_format            = "JPEG"
	settings.LR_export_colorSpace = sdrColorSpace or "sRGB"
	settings.LR_jpeg_quality      = quality or 0.92
	settings.LR_export_bitDepth   = 8
	applyResize(settings, shortEdgePx)

	return exportOne(photo, settings)
end

return Exporter
