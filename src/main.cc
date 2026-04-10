#include <iostream>
#include <string>

#include "initializer.h"
#include "parser.h"
#include "rrr.h"
#include "router.h"
#include "writer.h"

int main(int argc, char* argv[]) {
    const std::string input_file =
        argc > 1 ? argv[1] : std::string("samples/sample0.txt");

    // SetParserDebug(true);
    // SetInitializerDebug(true);
    // SetRouterDebug(true);
    // SetWriterDebug(true);
    const Problem problem = ParseInputFile(input_file);
    RoutingDB db = InitializeRoutingDB(problem);
    std::cout << "main.parse_ok file=" << input_file
              << " rows=" << problem.rows
              << " cols=" << problem.cols
              << " blocks=" << problem.blocks.size()
              << " nets=" << problem.nets.size() << "\n";
    std::cout << "main.init_ok horizontal_edges=" << db.horizontal_edges.size()
              << " vertical_edges=" << db.vertical_edges.size() << "\n";
    const RoutingResult initial_result = RunInitialRouting(db);
    std::cout << "main.route_ok routed_nets=" << initial_result.routed_nets.size()
              << " wirelength=" << initial_result.total_wirelength
              << " overflow=" << initial_result.total_overflow << "\n";
    const RoutingResult final_result = RunRRR(db, initial_result);
    // const RoutingResult final_result = initial_result;
    std::cout << "main.rrr_ok routed_nets=" << final_result.routed_nets.size()
              << " wirelength=" << final_result.total_wirelength
              << " overflow=" << final_result.total_overflow << "\n";
    WriteRoutingSolution(input_file, final_result);
    const std::string output_file = MakeSolutionFilePath(input_file);
    std::cout << "main.write_ok output=" << output_file << "\n";
    return 0;
}
