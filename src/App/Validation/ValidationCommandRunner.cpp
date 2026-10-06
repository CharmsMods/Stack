#include "App/Validation/Suites/GraphRenderingValidation.h"
#include "App/Validation/ValidationCommandRunner.h"

#include "App/Validation/ValidationSuites.h"
#include "App/settings/AppearanceTheme.h"
#include "Editor/LayerRegistry.h"

#include <cstring>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

namespace Stack::Validation { bool BenchmarkBracketingPresentation(const std::filesystem::path&,const std::filesystem::path&); bool ValidateBracketing(); bool ValidateBracketingUi(const std::filesystem::path&); }
namespace Stack::Validation { bool ValidateBracketingWorkflow(int,char**,bool=false,unsigned=2); }
namespace Stack::Validation { bool ValidateProjectStageState(int,char**); }
namespace Stack::Validation { bool ValidateAutoBracket(int,char**); }
namespace Stack::Validation { bool ValidateAutoBracketReopen(const std::filesystem::path&); }
namespace Stack::Validation { bool DiagnoseBracketingProject(int,char**); }
namespace Stack::Validation { bool ValidateSuperResolution(int,char**); bool ValidateHdrDisplay(int,char**); }
namespace Stack::Validation { bool ValidateProcessingPresentation(const std::filesystem::path&,const std::filesystem::path&); }
namespace Stack::Validation { bool ValidateColorCalibration(int, char**); }
namespace Stack::Validation { bool ValidateProjectGallery(const std::filesystem::path&); }
namespace Stack::Validation { bool ValidateConcurrentBracketing(const std::filesystem::path&, const std::filesystem::path&); }
namespace Stack::Validation { bool ValidatePanorama(int,char**); }
namespace Stack::Validation { bool ValidateToneDetail(const std::filesystem::path&, const std::filesystem::path&); }
namespace Stack::Validation { bool ValidateRawLayers(const std::filesystem::path&); }

bool TryRunValidationCommand(int argc, char** argv, int& exitCode) {
    if (argc == 4 && std::strcmp(argv[1], "--validate-tone-detail") == 0) {
        exitCode = Stack::Validation::ValidateToneDetail(argv[2], argv[3]) ? 0 : 90;
        return true;
    }
    if (argc == 3 && std::strcmp(argv[1], "--validate-raw-layers") == 0) {
        exitCode = Stack::Validation::ValidateRawLayers(argv[2]) ? 0 : 89;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-panorama") == 0) {
        exitCode = Stack::Validation::ValidatePanorama(argc - 2, argv + 2) ? 0 : 88;
        return true;
    }
    if (argc == 4 && std::strcmp(argv[1], "--validate-concurrent-bracketing") == 0) {
        exitCode = Stack::Validation::ValidateConcurrentBracketing(argv[2], argv[3]) ? 0 : 87;
        return true;
    }
    if (argc == 3 && std::strcmp(argv[1], "--validate-project-gallery") == 0) {
        exitCode = Stack::Validation::ValidateProjectGallery(argv[2]) ? 0 : 86;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-color-calibration") == 0) {
        exitCode = Stack::Validation::ValidateColorCalibration(argc - 2, argv + 2) ? 0 : 85;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-project-workspaces") == 0) {
        if (argc > 3) {
            std::cerr << "Usage: Stack.exe --validate-project-workspaces [existing-project.stack]\n";
            exitCode = 84;
        } else {
            exitCode = Stack::Validation::ValidateProjectWorkspaces(argc == 3 ? argv[2] : "") ? 0 : 84;
        }
        return true;
    }
    if(argc==3&&std::strcmp(argv[1],"--validate-auto-bracket-reopen")==0) {
        exitCode=Stack::Validation::ValidateAutoBracketReopen(argv[2])?0:83;return true;
    }
    if((argc==7||argc==8)&&std::strcmp(argv[1],"--validate-auto-bracket")==0) {
        exitCode=Stack::Validation::ValidateAutoBracket(argc-2,argv+2)?0:83;return true;
    }
    if(argc>1&&std::strcmp(argv[1],"--validate-hdr-display")==0) {
        exitCode=Stack::Validation::ValidateHdrDisplay(argc-2,argv+2)?0:82;return true;
    }
    if (argc == 5 && std::strcmp(argv[1], "--validate-project-stage-state") == 0) {
        exitCode = Stack::Validation::ValidateProjectStageState(argc - 2, argv + 2) ? 0 : 81;
        return true;
    }
    if(argc>3&&std::strcmp(argv[1],"--validate-processing-presentation")==0) {
        exitCode=Stack::Validation::ValidateProcessingPresentation(argv[2],argv[3])?0:80;return true;
    }
    if(argc>3 && std::strcmp(argv[1],"--benchmark-bracketing-presentation")==0) {
        exitCode=Stack::Validation::BenchmarkBracketingPresentation(argv[2],argv[3])?0:79;return true;
    }
    if(argc>3&&std::strcmp(argv[1],"--validate-bracketing-sr1-workflow")==0) {
        exitCode=Stack::Validation::ValidateBracketingWorkflow(argc-2,argv+2,true,1)?0:78;return true;
    }
    if(argc>3&&std::strcmp(argv[1],"--validate-bracketing-sr-workflow")==0) {
        exitCode=Stack::Validation::ValidateBracketingWorkflow(argc-2,argv+2,true)?0:78;return true;
    }
    if(argc>2&&std::strcmp(argv[1],"--validate-super-resolution")==0) {
        exitCode=Stack::Validation::ValidateSuperResolution(argc-2,argv+2)?0:77;return true;
    }
    if (argc > 3 && std::strcmp(argv[1], "--diagnose-bracketing-project") == 0) {
        exitCode = Stack::Validation::DiagnoseBracketingProject(argc-2,argv+2) ? 0 : 76;
        return true;
    }
    if (argc > 3 && std::strcmp(argv[1], "--validate-bracketing-workflow") == 0) {
        exitCode = Stack::Validation::ValidateBracketingWorkflow(argc-2,argv+2) ? 0 : 75;
        return true;
    }
    if (argc > 2 && std::strcmp(argv[1], "--validate-bracketing-ui") == 0) {
        exitCode = Stack::Validation::ValidateBracketingUi(argv[2]) ? 0 : 74;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-bracketing") == 0) {
        exitCode = Stack::Validation::ValidateBracketing() ? 0 : 73;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-zone-areas") == 0) {
        exitCode = Stack::Validation::ValidateRawZoneAreaPipeline() ? 0 : 72;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-graph-rendering") == 0) {
        const bool projectOwnershipOnly = argc == 3 &&
            std::strcmp(argv[2], "--project-ownership") == 0;
        if (argc != 2 && !projectOwnershipOnly) {
            std::cerr << "Usage: Stack.exe --validate-graph-rendering [--project-ownership]\n";
            exitCode = 71;
        } else {
            exitCode = Stack::Validation::ValidateGraphRendering(projectOwnershipOnly) ? 0 : 71;
        }
        return true;
    }
    if (argc > 2 && std::strcmp(argv[1], "--validate-raw-decode-cancellation") == 0) {
        exitCode = Stack::Validation::ValidateRawDecodeCancellation(argv[2]) ? 0 : 70;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-grading-scopes") == 0) {
        exitCode = Stack::Validation::ValidateRawGradingScopes() ? 0 : 69;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-appearance-settings") == 0) {
        std::string error;
        if (!StackAppearance::ValidateConnectionPresentationAppearancePersistence(&error)) {
            std::cerr << "Appearance settings validation failed: " << error << std::endl;
            exitCode = 45;
            return true;
        }
        std::cout << "Appearance settings validation passed." << std::endl;
        exitCode = 0;
        return true;
    }

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

    if (argc > 1 && std::strcmp(argv[1], "--validate-hdr-contracts") == 0) {
        exitCode = Stack::Validation::ValidateHdrContractsAndOverrange() ? 0 : 46;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-hdr-calibration") == 0) {
        exitCode = Stack::Validation::ValidateHdrCalibrationAndMixedIso() ? 0 : 47;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-hdr-translation") == 0) {
        exitCode = Stack::Validation::ValidateHdrTranslationAndFusion() ? 0 : 48;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-hdr-project-foundation") == 0) {
        exitCode = Stack::Validation::ValidateHdrProjectFoundation() ? 0 : 49;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-manual-hdr-fusion") == 0) {
        exitCode = Stack::Validation::ValidateManualHdrFusion() ? 0 : 68;
        return true;
    }
    if (argc > 1 && std::strcmp(argv[1], "--validate-hdr-end-to-end") == 0) {
        exitCode = Stack::Validation::ValidateHdrEndToEndProcessor() ? 0 : 50;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-hdr-gpu") == 0) {
        exitCode = Stack::Validation::ValidateHdrGpuFusion() ? 0 : 66;
        return true;
    }

    if (argc > 1 &&
        std::strcmp(argv[1], "--validate-shared-burst-gpu") == 0) {
        exitCode = Stack::Validation::ValidateSharedBurstGpuFusion() ? 0 : 67;
        return true;
    }

    if (argc > 1 &&
        std::strcmp(argv[1], "--validate-raw-rgb-denoise-gpu") == 0) {
        exitCode = Stack::Validation::ValidateRawRgbDenoiseGpuShaders() ? 0 : 68;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-multiframe-contracts") == 0) {
        exitCode = Stack::Validation::ValidateUnifiedMultiFrameContracts() ? 0 : 51;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-multiframe-phase0") == 0) {
        exitCode = Stack::Validation::ValidateUnifiedMultiFramePhase0() ? 0 : 52;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-multiframe-estimator") == 0) {
        exitCode = Stack::Validation::ValidateUnifiedMultiFrameEstimator() ? 0 : 53;
        return true;
    }

    if (argc > 1 &&
        std::strcmp(argv[1], "--validate-multiframe-prepared-tiles") == 0) {
        exitCode =
            Stack::Validation::ValidateUnifiedMultiFramePreparedTiles() ? 0 : 54;
        return true;
    }

    if (argc > 1 && std::strcmp(
            argv[1], "--validate-shared-burst-v1") == 0) {
        exitCode = Stack::Validation::ValidateSharedBurstV1() ? 0 : 55;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--run-mfd-corpus-entry") == 0) {
        if (argc < 7 || std::strcmp(argv[3], "--sample") != 0 ||
            std::strcmp(argv[5], "--output") != 0) {
            std::cerr
                << "Usage: Stack.exe --run-mfd-corpus-entry <absolute-definition-json> --sample <sample-id> --output <absolute-directory> [--memory-mib <count>] [--workers <count>] [--backend shared-burst|legacy-ra-cfa] [--alignment full|translation-only|identity]"
                << std::endl;
            exitCode = 44;
            return true;
        }
        std::uint64_t memoryMib = 2048u;
        std::uint32_t workers = 1u;
        std::string backendId = "legacy-ra-cfa";
        std::string alignmentModeId = "full";
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
                } else if (std::strcmp(argv[index], "--backend") == 0) {
                    backendId = argv[index + 1];
                } else if (std::strcmp(argv[index], "--alignment") == 0) {
                    alignmentModeId = argv[index + 1];
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
                backendId,
                alignmentModeId,
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

    if (argc > 1 && std::strcmp(argv[1], "--validate-raw-viewport-calibration") == 0) {
        exitCode = Stack::Validation::ValidateNodeMathPhase6Integration(true) ? 0 : 27;
        return true;
    }

    if (argc > 1 && std::strcmp(argv[1], "--validate-node-math-phase6") == 0) {
        const bool transactionsOnly = argc == 3 && std::strcmp(argv[2], "--transactions-only") == 0;
        if (argc != 2 && !transactionsOnly) { std::cerr << "Usage: Stack.exe --validate-node-math-phase6 [--transactions-only]\n"; exitCode = 27; return true; }
        exitCode = Stack::Validation::ValidateNodeMathPhase6Integration(false, transactionsOnly) ? 0 : 27;
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
