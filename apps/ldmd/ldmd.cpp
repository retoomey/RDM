/**
 * @file ldmd.cpp
 * @brief Modernized LDM server mainline program module
 * @author Robert Toomey
 * @date May 2026
 */
#include "RdmEngine.h"

int main(int argc, char* argv[]) {
    rdm::RdmEngine engine;
    return engine.StartEngine(argc, argv);
}
