--[[----------------------------------------------------------------------------

RenditionClassifier.lua

Given exactly two selected photos, decide which is the HDR rendition and which
is the SDR rendition, using the `HDREditMode` develop setting introduced in
LrC SDK 13.0.

Field-note caveats applied here:
  * Sliders sitting at their default value are omitted from the
    getDevelopSettings() table entirely, so an SDR photo is expected to have NO
    `HDREditMode` key rather than `HDREditMode = 0`. We therefore treat
    "present and non-zero" as HDR, and "absent or zero" as SDR.
  * getDevelopSettings() reads the persisted catalog record; for two
    already-edited photos this is accurate.

NOTE: the exact numeric semantics of HDREditMode (boolean-ish vs enum) are still
to be confirmed in Lightroom. `isHdr` only assumes "present and ~= 0", which
holds for either interpretation.

------------------------------------------------------------------------------]]

local RenditionClassifier = {}

-- Returns true if a photo's develop settings indicate HDR editing is active.
local function isHdr(photo)
	local settings = photo:getDevelopSettings()
	local mode = settings and settings.HDREditMode
	return mode ~= nil and mode ~= 0
end

RenditionClassifier.isHdr = isHdr

--[[
Classify a list of selected photos.

Returns one of:
  { ok = true,  hdr = <photo>, sdr = <photo> }
  { ok = false, reason = "COUNT",      message = ... }   -- not exactly 2
  { ok = false, reason = "AMBIGUOUS",  message = ...,     -- 0 or 2 HDR photos
                photos = { <photo>, <photo> } }           -- caller may ask user
]]
function RenditionClassifier.classify(photos)
	if not photos or #photos ~= 2 then
		return {
			ok = false,
			reason = "COUNT",
			message = "Select exactly two photos: your SDR rendition and its HDR "
				.. "rendition (e.g. an HDR virtual copy). "
				.. (photos and #photos or 0) .. " selected.",
		}
	end

	local a, b = photos[1], photos[2]
	local aHdr, bHdr = isHdr(a), isHdr(b)

	if aHdr and not bHdr then
		return { ok = true, hdr = a, sdr = b }
	elseif bHdr and not aHdr then
		return { ok = true, hdr = b, sdr = a }
	end

	-- Neither or both look like HDR — cannot decide automatically.
	local detail = (aHdr and bHdr)
		and "Both selected photos have HDR editing active."
		or  "Neither selected photo has HDR editing active."
	return {
		ok = false,
		reason = "AMBIGUOUS",
		message = detail .. " Designate which one is the HDR rendition, or enable "
			.. "HDR on one of them.",
		photos = { a, b },
	}
end

return RenditionClassifier
