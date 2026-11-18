#include "App/Validation/ValidationSuites.h"

// The same production-suite entry point used by --validate-mfd-phase5, without
// the desktop, decoder, model runtime or private test access to the solver.
int main() {
    return Stack::Validation::ValidateMfdPhase5LocalMotion() ? 0 : 1;
}
