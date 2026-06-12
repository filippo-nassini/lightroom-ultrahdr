--[[----------------------------------------------------------------------------

SettingsDialog.lua

The modal window shown when the user runs the plugin: guidance text, the
detected HDR/SDR assignment, a colour-space choice, an output-destination
picker, and Run / Cancel.

Returns a result table:
  { run = true, outPath=, hdrColorSpace=, sdrColorSpace= }
  { run = false }                                            -- cancelled
  { run = true, swap = true, ... }                           -- user flipped HDR/SDR

------------------------------------------------------------------------------]]

local LrView          = import 'LrView'
local LrBinding       = import 'LrBinding'
local LrDialogs       = import 'LrDialogs'
local LrFunctionContext = import 'LrFunctionContext'
local LrPathUtils     = import 'LrPathUtils'

local SettingsDialog = {}

local HDR_SPACES = {
	{ title = "HDR sRGB (Rec. 709)", value = "sRGB_hdr" },
	{ title = "HDR Display P3",      value = "p3_hdr" },
	{ title = "HDR Rec. 2020",       value = "Rec2020_hdr" },
}
local SDR_SPACES = {
	{ title = "sRGB",       value = "sRGB" },
	{ title = "Display P3", value = "p3" },
}

local GUIDANCE =
	"Create an HDR virtual copy (HDR enabled in Develop), then select it together "
	.. "with your SDR rendition. The HDR photo is exported as a 32-bit float TIFF "
	.. "(HDR Output on, Maximize Compatibility off); the SDR photo as a JPEG. "
	.. "The two are fused into one gain-map JPEG (UltraHDR)."

-- name a photo for display (copy name if any, else filename).
local function label(photo)
	local copyName = photo:getFormattedMetadata('copyName')
	local fileName = photo:getFormattedMetadata('fileName')
	if copyName and copyName ~= "" then
		return fileName .. "  [" .. copyName .. "]"
	end
	return fileName
end

--[[
present the dialog.
  ctx           : caller's LrFunctionContext (dialogs need a binding table)
  hdrPhoto/sdrPhoto : the classified renditions
  defaultOut    : suggested output file path
]]
function SettingsDialog.present(ctx, hdrPhoto, sdrPhoto, defaultOut)
	local f = LrView.osFactory()
	local props = LrBinding.makePropertyTable(ctx)
	props.hdrColorSpace = "sRGB_hdr"
	props.sdrColorSpace = "sRGB"
	props.outPath       = defaultOut
	props.swap          = false

	local contents = f:column {
		bind_to_object = props,
		spacing = f:control_spacing(),
		fill_horizontal = 1,

		f:static_text {
			title = GUIDANCE,
			width_in_chars = 50,
			height_in_lines = 4,
		},

		f:separator { fill_horizontal = 1 },

		f:row {
			f:static_text { title = "HDR rendition:", width_in_chars = 12 },
			f:static_text { title = label(hdrPhoto), fill_horizontal = 1 },
		},
		f:row {
			f:static_text { title = "SDR rendition:", width_in_chars = 12 },
			f:static_text { title = label(sdrPhoto), fill_horizontal = 1 },
		},
		f:checkbox {
			title = "These are swapped — use the other one as HDR",
			value = LrView.bind("swap"),
		},

		f:separator { fill_horizontal = 1 },

		f:row {
			f:static_text { title = "HDR color space:", width_in_chars = 16 },
			f:popup_menu { value = LrView.bind("hdrColorSpace"), items = HDR_SPACES },
		},
		f:row {
			f:static_text { title = "SDR color space:", width_in_chars = 16 },
			f:popup_menu { value = LrView.bind("sdrColorSpace"), items = SDR_SPACES },
		},

		f:separator { fill_horizontal = 1 },

		f:row {
			f:static_text { title = "Output:", width_in_chars = 8 },
			f:edit_field { value = LrView.bind("outPath"), fill_horizontal = 1, width_in_chars = 30 },
			f:push_button {
				title = "Choose…",
				action = function()
					local picked = LrDialogs.runSavePanel {
						title = "Save UltraHDR JPEG as",
						requiredFileType = "jpg",
						canCreateDirectories = true,
					}
					if picked then props.outPath = picked end
				end,
			},
		},
	}

	local result = LrDialogs.presentModalDialog {
		title = "Merge SDR + HDR to UltraHDR",
		contents = contents,
		actionVerb = "Run",
	}

	if result ~= "ok" then
		return { run = false }
	end
	return {
		run           = true,
		swap          = props.swap,
		outPath       = props.outPath,
		hdrColorSpace = props.hdrColorSpace,
		sdrColorSpace = props.sdrColorSpace,
	}
end

return SettingsDialog
