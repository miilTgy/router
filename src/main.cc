#include <iostream>
#include <string>

#include "initializer.h"
#include "parser.h"

int main(int argc, char* argv[]) {
    const std::string input_file =
        argc > 1 ? argv[1] : std::string("samples/sample0.txt");

    SetParserDebug(true);
    SetInitializerDebug(true);
    const Problem problem = ParseInputFile(input_file);
    const RoutingDB db = InitializeRoutingDB(problem);
    std::cout << "main.parse_ok file=" << input_file
              << " rows=" << problem.rows
              << " cols=" << problem.cols
              << " blocks=" << problem.blocks.size()
              << " nets=" << problem.nets.size() << "\n";
    std::cout << "main.init_ok horizontal_edges=" << db.horizontal_edges.size()
              << " vertical_edges=" << db.vertical_edges.size() << "\n";
    return 0;
}
