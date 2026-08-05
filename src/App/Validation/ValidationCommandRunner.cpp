#include "App/Validation/ValidationCommandRunner.h"

#include "App/Validation/ValidationSuites.h"
#include "Editor/LayerRegistry.h"

#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
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

    if (argc > 1 && std::strcmp(argv[1], "--validate-multi-source-projects") == 0) {
        exitCode = Stack::Validation::ValidateMultiSourceProjectFoundation() ? 0 : 28;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-project-foundation") == 0) {
        exitCode = Stack::Validation::ValidateMfdProjectFoundation() ? 0 : 29;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase0") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase0Contracts() ? 0 : 30;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase1") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase1Preparation() ? 0 : 31;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase2") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase2NoiseModel() ? 0 : 32;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase3") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase3SameCfaSampler() ? 0 : 33;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase4") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase4GlobalRegistration() ? 0 : 34;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase5") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase5LocalMotion() ? 0 : 35;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase6") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase6Reliability() ? 0 : 36;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase7") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase7Fusion() ? 0 : 37;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase8") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase8Streaming() ? 0 : 38;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-phase9") == 0) {
        exitCode = Stack::Validation::ValidateMfdPhase9Evaluation(
            argc - 2, argv + 2) ? 0 : 39;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-corpus-intake") == 0) {
        exitCode = Stack::Validation::ValidateMfdCorpusIntake() ? 0 : 40;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--write-mfd-corpus-template") == 0) {
        if (argc != 3) {
            std::cerr << "Usage: Stack.exe --write-mfd-corpus-template <absolute-output-json>" << std::endl;
            exitCode = 41;
            return true;
        }
        std::string error;
        if (!Stack::Validation::WriteMfdCorpusIntakeTemplate(argv[2], &error)) {
            std::cerr << "MFD corpus template generation failed: " << error << std::endl;
            exitCode = 41;
            return true;
        }
        std::cout << "MFD corpus template written to " << argv[2] << std::endl;
        exitCode = 0;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--lock-mfd-corpus") == 0) {
        if (argc != 5 || std::strcmp(argv[3], "--output") != 0) {
            std::cerr << "Usage: Stack.exe --lock-mfd-corpus <absolute-definition-json> --output <absolute-report-json>" << std::endl;
            exitCode = 42;
            return true;
        }
        std::string error;
        if (!Stack::Validation::LockMfdCorpusDefinition(
                argv[2], argv[4], &error)) {
            std::cerr << "MFD corpus locking failed: " << error << std::endl;
            exitCode = 42;
            return true;
        }
        std::cout << "MFD corpus locked; report written to " << argv[4] << std::endl;
        exitCode = 0;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-mfd-end-to-end") == 0) {
        exitCode = Stack::Validation::ValidateMfdEndToEndProcessor() ? 0 : 43;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--run-mfd-corpus-entry") == 0) {
        if (argc < 7 || std::strcmp(argv[3], "--sample") != 0 ||
            std::strcmp(argv[5], "--output") != 0) {
            std::cerr
                << "Usage: Stack.exe --run-mfd-corpus-entry <absolute-definition-json> --sample <sample-id> --output <absolute-directory> [--memory-mib <count>] [--workers <count>]"
                << std::endl;
            exitCode = 44;
            return true;
        }
        std::uint64_t memoryMib = 2048u;
        std::uint32_t workers = 1u;
        try {
            for (int index = 7; index < argc; index += 2) {
                if (index + 1 >= argc) throw std::invalid_argument("missing value");
                if (std::strcmp(argv[index], "--memory-mib") == 0) {
                    memoryMib = std::stoull(argv[index + 1]);
                } else if (std::strcmp(argv[index], "--workers") == 0) {
                    const std::uint64_t parsed = std::stoull(argv[index + 1]);
                    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
                        throw std::out_of_range("worker count");
                    }
                    workers = static_cast<std::uint32_t>(parsed);
                } else {
                    throw std::invalid_argument("unknown option");
                }
            }
            if (memoryMib == 0u || workers == 0u ||
                memoryMib > std::numeric_limits<std::uint64_t>::max() /
                    (1024u * 1024u)) {
                throw std::out_of_range("resource limit");
            }
        } catch (const std::exception&) {
            std::cerr << "MFD corpus runner resource options are invalid."
                      << std::endl;
            exitCode = 44;
            return true;
        }
        std::string error;
        if (!Stack::Validation::RunMfdCorpusEntry(
                argv[2],
                argv[4],
                argv[6],
                memoryMib * 1024u * 1024u,
                workers,
                &error)) {
            std::cerr << "MFD corpus processing failed: " << error << std::endl;
            exitCode = 44;
            return true;
        }
        std::cout << "Experimental MFD corpus result written atomically to "
                  << argv[6] << std::endl;
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
