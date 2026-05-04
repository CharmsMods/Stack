#include "BundlerBuild.h"
#include "BundlerInliner.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>

namespace fs = std::filesystem;

// ── Helper: copy a file verbatim ─────────────────────────────────────────────

static bool CopyFileVerbatim(const std::string& src, const std::string& dst) {
    std::error_code ec;
    fs::path dstPath(dst);
    if (dstPath.has_parent_path())
        fs::create_directories(dstPath.parent_path(), ec);

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-file minify build
// ─────────────────────────────────────────────────────────────────────────────

BuildResult RunPerFileBuild(const BundlerGraph& graph,
                            const BuildConfig& config,
                            const BundlerToolAdapter& tools,
                            BuildProgressFn progressFn)
{
    BuildResult result;

    // Determine output root
    std::string outRoot = config.outputDir;
    if (outRoot.empty()) {
        outRoot = (fs::path(config.projectRoot) / "dist").string();
    }

    // Ensure output dir exists
    {
        std::error_code ec;
        fs::create_directories(outRoot, ec);
        if (ec) {
            result.success = false;
            result.errorMessage = "Cannot create output directory: " + outRoot + " (" + ec.message() + ")";
            return result;
        }
    }

    const auto& nodes = graph.GetNodes();
    int total = static_cast<int>(nodes.size());
    int current = 0;

    for (const auto& node : nodes) {
        ++current;
        if (progressFn) {
            progressFn(current, total, node.relativePath);
        }

        // Compute output path: outRoot / relativePath
        fs::path outputPath = fs::path(outRoot) / node.relativePath;

        // Ensure parent directory
        {
            std::error_code ec;
            if (outputPath.has_parent_path())
                fs::create_directories(outputPath.parent_path(), ec);
        }

        FileBuildResult fr;
        fr.nodeId    = node.id;
        fr.inputPath = node.absolutePath;
        fr.outputPath = outputPath.string();
        fr.inputSize = node.fileSize;

        bool minified = false;

        // Try to minify based on file kind
        switch (node.kind) {
            case FileKind::JavaScript: {
                minified = tools.MinifyJS(node.absolutePath, fr.outputPath);
                break;
            }
            case FileKind::CSS: {
                minified = tools.MinifyCSS(node.absolutePath, fr.outputPath);
                break;
            }
            case FileKind::HTML: {
                minified = tools.MinifyHTML(node.absolutePath, fr.outputPath);
                break;
            }
            default:
                break;
        }

        if (minified) {
            fr.minified = true;
            std::error_code ec;
            fr.outputSize = static_cast<uint64_t>(fs::file_size(outputPath, ec));
            if (ec) fr.outputSize = 0;
            ++result.filesMinified;
        } else {
            // Fall back to plain copy
            if (CopyFileVerbatim(node.absolutePath, fr.outputPath)) {
                fr.minified = false;
                fr.outputSize = fr.inputSize;
                ++result.filesCopied;
            } else {
                fr.success = false;
                fr.errorMessage = "Failed to copy file.";
                ++result.filesFailed;
            }
        }

        result.totalInputSize  += fr.inputSize;
        result.totalOutputSize += fr.outputSize;
        ++result.filesProcessed;
        result.fileResults.push_back(std::move(fr));
    }

    result.success = (result.filesFailed == 0);
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Single HTML build
// ─────────────────────────────────────────────────────────────────────────────

BuildResult RunSingleHTMLBuild(const BundlerGraph& graph,
                               const BuildConfig& config,
                               const BundlerToolAdapter& tools,
                               BuildProgressFn progressFn)
{
    BuildResult result;

    // 1. Find entry point (prefer index.html)
    const GraphNode* entry = nullptr;
    for (const auto& node : graph.GetNodes()) {
        if (node.kind == FileKind::HTML) {
            if (node.relativePath == "index.html" || node.relativePath == "index.htm") {
                entry = &node;
                break;
            }
            if (!entry) entry = &node;
        }
    }

    if (!entry) {
        result.success = false;
        result.errorMessage = "No HTML entry point found.";
        return result;
    }

    if (progressFn) progressFn(0, 1, "Inlining resources into " + entry->relativePath);

    // 2. Perform inlining
    auto inlineRes = BundlerInliner::InlineHTML(*entry, graph);

    if (!inlineRes.success) {
        result.success = false;
        result.errorMessage = inlineRes.errorMessage;
        return result;
    }

    // 3. Optional: Minify the final HTML
    std::string finalHtml = inlineRes.content;
    std::string tempIn = (fs::temp_directory_path() / "temp_inline.html").string();
    std::string tempOut = (fs::temp_directory_path() / "temp_minified.html").string();

    {
        std::ofstream ofs(tempIn);
        ofs << finalHtml;
    }

    if (tools.MinifyHTML(tempIn, tempOut)) {
        std::ifstream ifs(tempOut);
        std::stringstream ss;
        ss << ifs.rdbuf();
        finalHtml = ss.str();
        result.filesMinified = 1;
    }

    // 4. Write to output
    std::string outRoot = config.outputDir;
    if (outRoot.empty()) outRoot = (fs::path(config.projectRoot) / "dist").string();
    fs::create_directories(outRoot);
    
    fs::path outPath = fs::path(outRoot) / "index.single.html";
    {
        std::ofstream ofs(outPath);
        ofs << finalHtml;
    }

    FileBuildResult fr;
    fr.nodeId = entry->id;
    fr.inputPath = entry->absolutePath;
    fr.outputPath = outPath.string();
    fr.inputSize = entry->fileSize; // approximate
    fr.outputSize = finalHtml.size();
    fr.success = true;

    result.fileResults.push_back(fr);
    result.totalInputSize = entry->fileSize;
    result.totalOutputSize = fr.outputSize;
    result.filesProcessed = 1;
    result.success = true;

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bundle by Type build
// ─────────────────────────────────────────────────────────────────────────────

BuildResult RunBundleByTypeBuild(const BundlerGraph& graph,
                                 const BuildConfig& config,
                                 const BundlerToolAdapter& tools,
                                 BuildProgressFn progressFn)
{
    BuildResult result;

    // 1. Find HTML entry point
    const GraphNode* entry = nullptr;
    for (const auto& node : graph.GetNodes()) {
        if (node.kind == FileKind::HTML) {
            if (!entry || node.relativePath == "index.html" || node.relativePath == "index.htm") {
                entry = &node;
            }
        }
    }

    if (!entry) {
        result.success = false;
        result.errorMessage = "No HTML entry point found.";
        return result;
    }

    if (progressFn) progressFn(0, 1, "Bundling resources by type...");

    std::string outRoot = config.outputDir;
    if (outRoot.empty()) outRoot = (fs::path(config.projectRoot) / "dist").string();
    fs::create_directories(outRoot);

    std::string tempDir = fs::temp_directory_path().string();

    // 2. Gather JS and CSS content
    std::string jsContent;
    std::string cssContent;
    std::vector<const GraphNode*> jsNodes;
    std::vector<const GraphNode*> cssNodes;
    
    for (const auto& node : graph.GetNodes()) {
        if (node.kind == FileKind::JavaScript) {
            jsNodes.push_back(&node);
            jsContent += "// --- " + node.relativePath + " ---\n";
            std::ifstream ifs(node.absolutePath);
            if (ifs) {
                std::stringstream ss;
                ss << ifs.rdbuf();
                jsContent += ss.str() + "\n";
            }
        } else if (node.kind == FileKind::CSS) {
            cssNodes.push_back(&node);
            cssContent += "/* --- " + node.relativePath + " --- */\n";
            std::ifstream ifs(node.absolutePath);
            if (ifs) {
                std::stringstream ss;
                ss << ifs.rdbuf();
                cssContent += ss.str() + "\n";
            }
        } else if (node.kind == FileKind::OpaqueAsset || node.kind == FileKind::StructuredText) {
            // Copy assets as-is
            fs::path outPath = fs::path(outRoot) / node.relativePath;
            if (outPath.has_parent_path()) fs::create_directories(outPath.parent_path());
            std::error_code ec;
            fs::copy_file(node.absolutePath, outPath, fs::copy_options::overwrite_existing, ec);
            
            FileBuildResult fr;
            fr.nodeId = node.id;
            fr.inputPath = node.absolutePath;
            fr.outputPath = outPath.string();
            fr.inputSize = node.fileSize;
            fr.outputSize = static_cast<uint64_t>(fs::file_size(outPath, ec));
            fr.success = !ec;
            if (!fr.success) {
                result.filesFailed++;
                fr.errorMessage = "Failed to copy asset.";
            } else {
                result.filesCopied++;
            }
            result.fileResults.push_back(fr);
            result.totalInputSize += fr.inputSize;
            result.totalOutputSize += fr.outputSize;
            result.filesProcessed++;
        }
    }

    // 3. Process and write JS bundle
    bool hasJs = !jsContent.empty();
    if (hasJs) {
        fs::path tempJs = fs::path(tempDir) / "temp_bundle.js";
        fs::path outJs = fs::path(outRoot) / "bundle.js";
        {
            std::ofstream ofs(tempJs);
            ofs << jsContent;
        }
        FileBuildResult fr;
        fr.inputPath = "virtual: bundle.js";
        fr.outputPath = outJs.string();
        fr.inputSize = jsContent.size();
        
        if (tools.MinifyJS(tempJs.string(), outJs.string())) {
            fr.minified = true;
            std::error_code ec;
            fr.outputSize = static_cast<uint64_t>(fs::file_size(outJs, ec));
            result.filesMinified++;
        } else {
            // fallback copy
            std::error_code ec;
            fs::copy_file(tempJs, outJs, fs::copy_options::overwrite_existing, ec);
            fr.outputSize = jsContent.size();
            result.filesCopied++;
        }
        fr.success = true;
        result.fileResults.push_back(fr);
        result.totalInputSize += fr.inputSize;
        result.totalOutputSize += fr.outputSize;
        result.filesProcessed++;
    }

    // 4. Process and write CSS bundle
    bool hasCss = !cssContent.empty();
    if (hasCss) {
        fs::path tempCss = fs::path(tempDir) / "temp_bundle.css";
        fs::path outCss = fs::path(outRoot) / "bundle.css";
        {
            std::ofstream ofs(tempCss);
            ofs << cssContent;
        }
        FileBuildResult fr;
        fr.inputPath = "virtual: bundle.css";
        fr.outputPath = outCss.string();
        fr.inputSize = cssContent.size();
        
        if (tools.MinifyCSS(tempCss.string(), outCss.string())) {
            fr.minified = true;
            std::error_code ec;
            fr.outputSize = static_cast<uint64_t>(fs::file_size(outCss, ec));
            result.filesMinified++;
        } else {
            std::error_code ec;
            fs::copy_file(tempCss, outCss, fs::copy_options::overwrite_existing, ec);
            fr.outputSize = cssContent.size();
            result.filesCopied++;
        }
        fr.success = true;
        result.fileResults.push_back(fr);
        result.totalInputSize += fr.inputSize;
        result.totalOutputSize += fr.outputSize;
        result.filesProcessed++;
    }

    // 5. Rewrite HTML
    std::string htmlContent;
    {
        std::ifstream ifs(entry->absolutePath);
        std::stringstream ss;
        ss << ifs.rdbuf();
        htmlContent = ss.str();
    }

    auto edges = graph.GetOutgoingEdges(entry->id);
    for (const auto* edge : edges) {
        if (!edge->resolved) continue;
        const GraphNode* target = graph.GetNode(edge->targetNodeId);
        if (!target) continue;

        if (target->kind == FileKind::JavaScript || target->kind == FileKind::CSS) {
            std::string escapedSpec = std::regex_replace(edge->originalSpecifier, std::regex(R"([-[\]{}()*+?.,\^$|#\s])"), R"(\$&)");
            if (target->kind == FileKind::JavaScript) {
                std::string tagPattern = "<script\\b[^>]*\\bsrc\\s*=\\s*[\"']" + escapedSpec + "[\"'][^>]*>\\s*</script>";
                std::regex re(tagPattern, std::regex::icase);
                htmlContent = std::regex_replace(htmlContent, re, "");
            } else if (target->kind == FileKind::CSS) {
                std::string tagPattern = "<link\\b[^>]*\\bhref\\s*=\\s*[\"']" + escapedSpec + "[\"'][^>]*>";
                std::regex re(tagPattern, std::regex::icase);
                htmlContent = std::regex_replace(htmlContent, re, "");
            }
        }
    }

    std::string newTags;
    if (hasCss) newTags += "<link rel=\"stylesheet\" href=\"bundle.css\">\n";
    if (hasJs)  newTags += "<script src=\"bundle.js\"></script>\n";

    size_t headEnd = htmlContent.find("</head>");
    if (headEnd != std::string::npos) {
        htmlContent.insert(headEnd, newTags);
    } else {
        htmlContent += newTags;
    }

    fs::path tempHtml = fs::path(tempDir) / "temp_bundle.html";
    fs::path outHtml = fs::path(outRoot) / entry->relativePath;
    if (outHtml.has_parent_path()) fs::create_directories(outHtml.parent_path());

    {
        std::ofstream ofs(tempHtml);
        ofs << htmlContent;
    }

    FileBuildResult frHtml;
    frHtml.nodeId = entry->id;
    frHtml.inputPath = entry->absolutePath;
    frHtml.outputPath = outHtml.string();
    frHtml.inputSize = entry->fileSize;

    if (tools.MinifyHTML(tempHtml.string(), outHtml.string())) {
        frHtml.minified = true;
        std::error_code ec;
        frHtml.outputSize = static_cast<uint64_t>(fs::file_size(outHtml, ec));
        result.filesMinified++;
    } else {
        std::error_code ec;
        fs::copy_file(tempHtml, outHtml, fs::copy_options::overwrite_existing, ec);
        frHtml.outputSize = htmlContent.size();
        result.filesCopied++;
    }
    frHtml.success = true;
    result.fileResults.push_back(frHtml);
    result.totalInputSize += frHtml.inputSize;
    result.totalOutputSize += frHtml.outputSize;
    result.filesProcessed++;

    result.success = (result.filesFailed == 0);
    return result;
}
