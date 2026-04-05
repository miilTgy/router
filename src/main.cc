#include <iostream>
#include <string>

#include "initializer.h"
#include "parser.h"
#include "router.h"

int main(int argc, char* argv[]) {
    const std::string input_file =
        argc > 1 ? argv[1] : std::string("samples/sample0.txt");

    SetParserDebug(true);
    SetInitializerDebug(true);
    SetRouterDebug(true);
    const Problem problem = ParseInputFile(input_file);
    RoutingDB db = InitializeRoutingDB(problem);
    std::cout << "main.parse_ok file=" << input_file
              << " rows=" << problem.rows
              << " cols=" << problem.cols
              << " blocks=" << problem.blocks.size()
              << " nets=" << problem.nets.size() << "\n";
    std::cout << "main.init_ok horizontal_edges=" << db.horizontal_edges.size()
              << " vertical_edges=" << db.vertical_edges.size() << "\n";
    const RoutingResult result = RunInitialRouting(db);
    std::cout << "main.route_ok routed_nets=" << result.routed_nets.size()
              << " wirelength=" << result.total_wirelength
              << " overflow=" << result.total_overflow << "\n";
    return 0;
}
