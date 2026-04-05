#pragma once

#include <string>
#include <utility>
#include <vector>

#include "common.h"

std::string MakeSolutionFilePath(const std::string& input_file_path);

std::vector<std::pair<Point, Point>> ExpandTreeToEdges(const RoutedTree& tree);

void WriteRoutingSolution(
    const std::string& input_file_path,
    const RoutingResult& result);

void WriteRoutingSolutionToPath(
    const std::string& output_file_path,
    const RoutingResult& result);

void SetWriterDebug(bool enabled);
