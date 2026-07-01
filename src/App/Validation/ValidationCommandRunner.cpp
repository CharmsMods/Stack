#include "App/Validation/ValidationCommandRunner.h"

#include "App/Validation/ValidationSuites.h"
#include "Editor/LayerRegistry.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

bool TryRunValidationCommand(int argc, char** argv, int& exitCode) {
    if (argc > 1 && std::strcmp(argv[1], "--validate-layer-registry") == 0) {
        std::vector<std::string> errors;
        if (!LayerRegistry::ValidateRegistry(&errors)) {
            for (const std::string& error : errors) {
                std::cerr << "LayerRegistry validation failed: " << error << std::endl;
            }
            exitCode = 2;
            return true;
        }

        std::cout << "LayerRegistry validation passed." << std::endl;
        exitCode = 0;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-tone-curve-auto") == 0) {
        exitCode = Stack::Validation::ValidateToneCurveAutoIntegration() ? 0 : 4;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-develop-auto-solve") == 0) {
        exitCode = Stack::Validation::ValidateDevelopAutoSolveBehavior() ? 0 : 7;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-develop-node-smoke") == 0) {
        exitCode = Stack::Validation::ValidateDevelopNodeSmoke() ? 0 : 5;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-develop-real-raw-smoke") == 0) {
        exitCode = Stack::Validation::ValidateDevelopRealRawSmoke(argc - 2, argv + 2) ? 0 : 6;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-workspace-loading-smoke") == 0) {
        exitCode = Stack::Validation::ValidateRawWorkspaceLoadingSmoke(argc - 2, argv + 2) ? 0 : 8;
        return true;
    }

    return false;
}
