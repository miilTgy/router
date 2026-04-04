#pragma once

#include <istream>
#include <string>

#include "common.h"

Problem ParseInputFile(const std::string& filename);
Problem ParseInputStream(std::istream& is);
void SetParserDebug(bool enabled);
