--[[----------------------------------------------------------------------------

MergeMenuItem.lua

Entry point for the "Merge SDR + HDR to UltraHDR…" menu item.

Flow:
  1. Read the current photo selection (must be exactly two).
  2. Classify which is HDR and which is SDR (HDREditMode).
  3. Show the settings window (output destination, colour spaces).
  4. Export HDR -> 32-bit float TIFF and SDR -> JPEG to the system temp dir.
  5. Run uhdrtool with the three paths (or show the dry-run command if the
     binary isn't bundled yet).
  6. Report success/error, then clean up the intermediate files.

Everything runs in an async task because LrExportSession:waitForRender and
LrTasks.execute yield (the C-frame rule). Errors are trapped with
pcallWithContext (the yield-safe pcall).

------------------------------------------------------------------------------]]

local LrApplication     = import 'LrApplication'
local LrFunctionContext = import 'LrFunctionContext'
local LrTasks           = import 'LrTasks'
local LrDialogs         = import 'LrDialogs'
local LrFileUtils       = import 'LrFileUtils'
local LrPathUtils       = import 'LrPathUtils'

local RenditionClassifier = require 'RenditionClassifier'
local Exporter            = require 'Exporter'
local SettingsDialog      = require 'SettingsDialog'
local BinaryLocator       = require 'BinaryLocator'
local ToolRunner          = require 'ToolRunner'

-- Best-effort delete; never raises.
local function cleanup(paths)
	for _, p in ipairs(paths) do
		if p and LrFileUtils.exists(p) then
			pcall(function() LrFileUtils.delete(p) end)
		end
	end
end

-- Wipe the scratch subfolder at the start of a run, so any intermediates a prior
-- run's end-cleanup failed to remove are reclaimed now. This is what bounds the
-- worst-case temp footprint to a single job's files. Best-effort per child: a
-- locked/unremovable leftover is skipped while the rest still go, and the run
-- continues regardless. Exporter.scratchDir() (re)creates the folder itself.
local function resetScratch()
	local dir = Exporter.scratchDir()
	for entry in LrFileUtils.directoryEntries(dir) do
		pcall(function() LrFileUtils.delete(entry) end)
	end
end

-- Suggest an output path next to where the user likely wants it.
local function defaultOutPath(sdrPhoto)
	local name = sdrPhoto:getFormattedMetadata('fileName') or "ultrahdr"
	local stem = LrPathUtils.removeExtension(name)
	local dir  = LrPathUtils.getStandardFilePath('pictures')
	return LrPathUtils.child(dir, stem .. "_ultrahdr.jpg")
end

local function doMerge(context)
	local catalog = LrApplication.activeCatalog()
	local photos  = catalog and catalog:getTargetPhotos() or {}

	-- 1 + 2: classify selection.
	local cls = RenditionClassifier.classify(photos)
	if not cls.ok then
		LrDialogs.message("Cannot merge", cls.message, "warning")
		return
	end
	local hdrPhoto, sdrPhoto = cls.hdr, cls.sdr

	-- 3: settings window.
	local opts = SettingsDialog.present(context, hdrPhoto, sdrPhoto, defaultOutPath(sdrPhoto))
	if not opts.run then return end
	if opts.swap then
		hdrPhoto, sdrPhoto = sdrPhoto, hdrPhoto
	end
	if not opts.outPath or opts.outPath == "" then
		LrDialogs.message("Cannot merge", "No output destination chosen.", "warning")
		return
	end

	-- 4: reset the scratch folder, then export both renditions into it.
	-- The optional short-edge resize is applied identically to both so they stay
	-- dimensionally matched for uhdrtool.
	resetScratch()
	local hdrTiff, hdrErr = Exporter.exportHdrTiff(hdrPhoto, "uhdr_hdr_src", opts.hdrColorSpace, opts.shortEdge)
	if not hdrTiff then
		LrDialogs.message("HDR export failed", tostring(hdrErr), "critical")
		return
	end
	local sdrJpeg, sdrErr = Exporter.exportSdrJpeg(sdrPhoto, "uhdr_sdr_src", opts.sdrColorSpace, opts.shortEdge)
	if not sdrJpeg then
		cleanup({ hdrTiff })
		LrDialogs.message("SDR export failed", tostring(sdrErr), "critical")
		return
	end

	-- 5: run the tool (or dry-run).
	local binary, binErr = BinaryLocator.resolve()
	local result = ToolRunner.run({
		binary = binary,
		hdr    = hdrTiff,
		sdr    = sdrJpeg,
		out    = opts.outPath,
		cgamut = ToolRunner.CGAMUT[opts.hdrColorSpace],
		sgamut = ToolRunner.SGAMUT[opts.sdrColorSpace],
	}, binErr)

	-- 6: report + cleanup.
	if result.dryRun then
		LrDialogs.message(
			"Dry run (uhdrtool not bundled yet)",
			"The plugin would run:\n\n" .. result.command
				.. "\n\nExported inputs:\n" .. hdrTiff .. "\n" .. sdrJpeg,
			"info")
		-- Leave the exported inputs in place so they can be inspected / fed to a
		-- hand-built uhdrtool during development.
		return
	end

	cleanup({ hdrTiff, sdrJpeg })

	if result.ok then
		LrDialogs.message("UltraHDR created", "Saved:\n" .. opts.outPath, "info")
	else
		LrDialogs.message(
			"uhdrtool failed (exit " .. tostring(result.status) .. ")",
			"Command:\n" .. result.command,
			"critical")
	end
end

-- Run on an async task; trap errors yield-safely.
LrTasks.startAsyncTask(function()
	LrFunctionContext.callWithContext("mergeUltraHdr", function(context)
		local ok, err = LrFunctionContext.pcallWithContext("doMerge", function(_)
			doMerge(context)
		end)
		if not ok then
			LrDialogs.message("Unexpected error", tostring(err), "critical")
		end
	end)
end)
