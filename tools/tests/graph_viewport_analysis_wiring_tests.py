#!/usr/bin/env python3
"""Source integration checks complementing the standalone scheduling policy test.

No application/GPU runtime or private-state access is used. These guard the
producer/dispatcher wiring that originally starved unified-view readbacks.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


class GraphViewportAnalysisWiring(unittest.TestCase):
    def test_pixel_signature_borrows_tile_metadata(self):
        source = read("src/Editor/UI/EditorViewport.cpp")
        self.assertIn("const EditorRenderWorker::SharedTextureTileSet noTiles;", source)
        call = source[source.index("const EditorRenderWorker::SharedTextureTileSet noTiles;"):]
        call = call[:call.index("int readbackOriginX")]
        self.assertIn("hasViewportTiles ? viewportTiles : noTiles,", call)
        self.assertNotIn("SharedTextureTileSet{}", call)
        signature = source[source.index("std::uint64_t PixelInspectionTextureSignature("):]
        signature = signature[:signature.index("struct PixelInspectionBounds")]
        self.assertIn("const EditorRenderWorker::SharedTextureTileSet& tiles,", signature)

    def test_graph_dispatch_drains_optional_analysis(self):
        source = read("src/Editor/Internal/EditorModuleRenderScheduling.cpp")
        self.assertRegex(source, re.compile(
            r"if \(UsesGraphViewportPreview\(\)\)\s*\{\s*"
            r"UpdateViewportPreviewRefinement\(now\);\s*"
            r"if \(TrySubmitGraphViewportAnalysis\(now\)\) return;", re.S))

    def test_layer_selection_queues_instead_of_claiming_in_flight(self):
        source = read("src/Editor/Internal/RawLab/EditorModuleRawLayers.cpp")
        selection = source[source.index("void EditorModule::SelectRawAdjustmentLayer("):]
        self.assertIn("m_RawWorkspaceAnalysisPending = true;", selection)
        self.assertIn("m_RawWorkspaceAnalysisRequested = false;", selection)
        self.assertIn("m_RawWorkspaceAnalysisScopeRetryCount = 0;", selection)
        self.assertNotIn("m_RawWorkspaceAnalysisRequested = true;", selection)

    def test_auxiliary_does_not_manufacture_main_revisions(self):
        source = read("src/Editor/Internal/EditorModuleViewportPreview.cpp")
        self.assertIn("bool EditorModule::TrySubmitGraphViewportAnalysis(", source)
        submission = source.split("bool EditorModule::TrySubmitGraphViewportAnalysis(", 1)[1]
        submission = submission.split("void EditorModule::ObserveViewportPreviewTiming(", 1)[0]
        self.assertIn("RawRenderPurpose::AnalysisScopes", submission)
        self.assertIn("ContentIdentity(snapshot) == state.acceptedContentIdentity", submission)
        self.assertNotIn("MarkRenderRefreshDirty()", submission)
        adoption = read("src/Editor/Internal/EditorModuleRenderAdoption.cpp")
        self.assertNotRegex(adoption, re.compile(
            r"m_RawWorkspaceAnalysisRequested && !result\.rawWorkspace\.analysisCaptured\)\s*\{"
            r"[^}]*MarkRenderRefreshDirty\(\)", re.S))


if __name__ == "__main__":
    unittest.main()
