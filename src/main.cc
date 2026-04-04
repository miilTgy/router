#include <iostream>
#include <string>

#include "parser.h"

int main(int argc, char* argv[]) {
    const std::string input_file =
        argc > 1 ? argv[1] : std::string("samples/sample0.txt");

    SetParserDebug(true);
    const Problem problem = ParseInputFile(input_file);
    std::cout << "main.parse_ok file=" << input_file
              << " rows=" << problem.rows
              << " cols=" << problem.cols
              << " blocks=" << problem.blocks.size()
              << " nets=" << problem.nets.size() << "\n";
    return 0;
}
