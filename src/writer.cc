#include "writer.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

bool g_writer_debug_enabled = false;

std::string FormatPoint(const Point& p) {
    std::ostringstream oss;
    oss << "(" << p.r << ", " << p.c << ")";
    return oss.str();
}

bool AreManhattanAdjacent(const Point& a, const Point& b) {
    const int dr = std::abs(a.r - b.r);
    const int dc = std::abs(a.c - b.c);
    return dr + dc == 1;
}

void EmitWriteNetDebug(const RoutedTree& tree, std::size_t edge_count) {
    if (!g_writer_debug_enabled) {
        return;
    }

    std::cout << "[writer] write net name=" << tree.net_name
              << " edge_count=" << edge_count << "\n";
}

std::vector<RoutedTree> SortTreesForOutput(const RoutingResult& result) {
    std::vector<RoutedTree> trees = result.routed_nets;
    std::sort(
        trees.begin(),
        trees.end(),
        [](const RoutedTree& lhs, const RoutedTree& rhs) {
            return lhs.net_id < rhs.net_id;
        });

    for (std::size_t i = 1; i < trees.size(); ++i) {
        if (trees[i - 1].net_id == trees[i].net_id) {
            throw std::runtime_error(
                "WriteRoutingSolutionToPath: duplicate net_id " +
                std::to_string(trees[i].net_id));
        }
    }

    return trees;
}

}  // namespace

void SetWriterDebug(bool enabled) {
    g_writer_debug_enabled = enabled;
}

std::string MakeSolutionFilePath(const std::string& input_file_path) {
    const std::filesystem::path input_path(input_file_path);
    const std::filesystem::path output_name =
        input_path.stem().string() + "_solution.txt";
    return (std::filesystem::path("results") / output_name).string();
}

std::vector<std::pair<Point, Point>> ExpandTreeToEdges(const RoutedTree& tree) {
    std::vector<std::pair<Point, Point>> edges;
    for (const std::vector<Point>& branch : tree.branches) {
        if (branch.size() < 2) {
            continue;
        }

        for (std::size_t i = 1; i < branch.size(); ++i) {
            const Point& from = branch[i - 1];
            const Point& to = branch[i];
            if (!AreManhattanAdjacent(from, to)) {
                throw std::runtime_error(
                    "ExpandTreeToEdges: non-Manhattan adjacent points in net " +
                    tree.net_name + ": " + FormatPoint(from) + " and " +
                    FormatPoint(to));
            }
            edges.push_back({from, to});
        }
    }
    return edges;
}

void WriteRoutingSolution(
    const std::string& input_file_path,
    const RoutingResult& result) {
    const std::string output_file_path = MakeSolutionFilePath(input_file_path);
    WriteRoutingSolutionToPath(output_file_path, result);
}

void WriteRoutingSolutionToPath(
    const std::string& output_file_path,
    const RoutingResult& result) {
    if (g_writer_debug_enabled) {
        std::cout << "[writer] output path=" << output_file_path << "\n";
    }

    const std::filesystem::path output_path(output_file_path);
    const std::filesystem::path output_dir = output_path.parent_path();
    if (!output_dir.empty()) {
        std::error_code error;
        std::filesystem::create_directories(output_dir, error);
        if (error) {
            throw std::runtime_error(
                "WriteRoutingSolutionToPath: failed to create output directory: " +
                output_dir.string() + ": " + error.message());
        }
    }

    std::ofstream output(output_file_path);
    if (!output.is_open()) {
        throw std::runtime_error(
            "WriteRoutingSolutionToPath: failed to open output file: " +
            output_file_path);
    }

    const std::vector<RoutedTree> trees = SortTreesForOutput(result);
    for (const RoutedTree& tree : trees) {
        if (tree.net_name.empty()) {
            throw std::runtime_error(
                "WriteRoutingSolutionToPath: net_name must not be empty");
        }

        const std::vector<std::pair<Point, Point>> edges = ExpandTreeToEdges(tree);
        output << tree.net_name << "\n";
        for (const auto& edge : edges) {
            output << FormatPoint(edge.first)
                   << "-"
                   << FormatPoint(edge.second)
                   << "\n";
        }
        output << "!\n";

        if (!output.good()) {
            throw std::runtime_error(
                "WriteRoutingSolutionToPath: failed while writing output file: " +
                output_file_path);
        }

        EmitWriteNetDebug(tree, edges.size());
    }

    if (!output.good()) {
        throw std::runtime_error(
            "WriteRoutingSolutionToPath: failed while finalizing output file: " +
            output_file_path);
    }

    if (g_writer_debug_enabled) {
        std::cout << "[writer] solution written nets=" << trees.size() << "\n";
    }
}
