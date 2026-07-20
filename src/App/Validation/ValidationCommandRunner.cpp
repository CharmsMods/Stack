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

    if (argc > 1 && std::strcmp(argv[1], "--validate-node-math-phase4") == 0) {
        exitCode = Stack::Validation::ValidateNodeMathPhase4Integration() ? 0 : 24;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-node-math-phase5") == 0) {
        exitCode = Stack::Validation::ValidateNodeMathPhase5Integration() ? 0 : 25;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-node-math-phase6") == 0) {
        exitCode = Stack::Validation::ValidateNodeMathPhase6Integration() ? 0 : 27;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--write-node-socket-catalog") == 0) {
        if (argc != 3) {
            std::cerr << "Usage: Stack.exe --write-node-socket-catalog <absolute-output-path>" << std::endl;
            exitCode = 26;
            return true;
        }
        std::string error;
        if (!Stack::Validation::WriteNodeSocketCatalog(argv[2], &error)) {
            std::cerr << "Socket catalog generation failed: " << error << std::endl;
            exitCode = 26;
            return true;
        }
        std::cout << "Socket catalog written to " << argv[2] << std::endl;
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

    if (argc > 1 && std::strcmp(argv[1], "--validate-ffmpeg-provider") == 0) {
        exitCode = Stack::Validation::ValidateFFmpegProvider() ? 0 : 16;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-workspace-loading-smoke") == 0) {
        exitCode = Stack::Validation::ValidateRawWorkspaceLoadingSmoke(argc - 2, argv + 2) ? 0 : 8;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-starting-point-records") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointRecords(argc - 2, argv + 2) ? 0 : 9;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-starting-point-editor-state") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointEditorStateHandoff() ? 0 : 17;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--check-raw-starting-point-annotations") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointAnnotationCheck(argc - 2, argv + 2) ? 0 : 11;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--check-raw-starting-point-stage-evidence") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointStageEvidenceCheck(argc - 2, argv + 2) ? 0 : 12;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--summarize-raw-starting-point-records") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointRecordSummary(argc - 2, argv + 2) ? 0 : 10;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--check-raw-starting-point-constant-review") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointConstantReviewCheck(argc - 2, argv + 2) ? 0 : 13;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--check-raw-starting-point-validation-gates") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointValidationGates(argc - 2, argv + 2) ? 0 : 15;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--raw-starting-point-validation-workflow") == 0) {
        exitCode = Stack::Validation::ValidateRawStartingPointValidationWorkflow(argc - 2, argv + 2) ? 0 : 14;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-evidence-foundation") == 0) {
        exitCode = Stack::Validation::ValidateRawEvidenceFoundation(argc - 2, argv + 2) ? 0 : 18;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-rendered-feature-foundation") == 0) {
        exitCode = Stack::Validation::ValidateRenderedFeatureFoundation(argc - 2, argv + 2) ? 0 : 19;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-candidate-surfaces") == 0) {
        exitCode = Stack::Validation::ValidatePreciseCandidateSurfaces(argc - 2, argv + 2) ? 0 : 20;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-optimizer-benchmarks") == 0) {
        exitCode = Stack::Validation::ValidateRawOptimizerBenchmarks(argc - 2, argv + 2) ? 0 : 21;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-precise-dry-run") == 0) {
        exitCode = Stack::Validation::ValidatePreciseDryRun(argc - 2, argv + 2) ? 0 : 22;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-precise-integration") == 0) {
        exitCode = Stack::Validation::ValidatePreciseIntegration(argc - 2, argv + 2) ? 0 : 23;
        return true;
    }

    return false;
}
