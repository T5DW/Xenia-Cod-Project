// .ipak image packs. The streamed camos live here; this build's layout isn't
// confirmed yet, so for now this only writes a report about the file.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "images.h"

std::string inspect_ipak(const std::filesystem::path& path, const std::vector<CamoImage>& images);
