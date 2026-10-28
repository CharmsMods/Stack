"""Keep note drafts out of topology-only visual invalidation.

The native NodeNotesTransitions test covers text/target lifecycle. This guard
covers its integration into the GPU-backed canvas owner without mocking GL.
"""
import argparse
from pathlib import Path
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
args, remaining = parser.parse_known_args()
root = args.repo_root


def body(path, signature):
    source = (root / path).read_text(encoding="utf-8")
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


class NoteCacheOwnership(unittest.TestCase):
    def test_topology_and_offscreen_cache_reset_preserve_notes(self):
        visual = body("src/Editor/NodeGraph/UI/EditorNodeGraphUIPreviews.cpp",
                      "void EditorNodeGraphUI::ResetPerGraphVisualCaches()")
        self.assertIn("m_NodeNotes.InvalidateLayouts();", visual)
        self.assertNotIn("m_NodeNotes.Reset();", visual,
                         "Visual invalidation must not discard pending authored text")
        layouts = body("src/Editor/NodeGraph/UI/NodeNotesView.cpp", "void NodeNotesView::InvalidateLayouts()")
        self.assertEqual(layouts.strip(), "layouts_.clear();",
                         "Layout invalidation must clear only its derived wrapping cache")
        reset = body("src/Editor/NodeGraph/UI/NodeNotesView.cpp", "void NodeNotesView::Reset()")
        for cleanup in ("InvalidateLayouts();", "edit_.reset();", "pending_.reset();", "next_.reset();"):
            self.assertIn(cleanup, reset)
        canvas = (root / "src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp").read_text(encoding="utf-8")
        self.assertIn("structureRevision != m_LastGraphStructureRevision", canvas)
        self.assertIn("ResetPerGraphVisualCaches();", canvas)
        self.assertIn("m_NodeNotes.BeginFrame", canvas)
        self.assertIn("target && target->instanceUuid == note.instanceUuid", canvas)
        for signature in ("void EditorNodeGraphUI::Render(EditorModule* editor)",
                          "void EditorNodeGraphUI::RenderGraphCapture(",
                          "void EditorNodeGraphUI::RenderStaticGraphPreview("):
            with self.subTest(signature=signature):
                self.assertIn("ResetPerGraphVisualCaches();",
                              body("src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp", signature))

    def test_async_placeholder_removal_is_a_same_owner_topology_change(self):
        imports = (root / "src/Editor/Internal/EditorModuleGraphImageNodes.cpp").read_text(encoding="utf-8")
        failure = imports[imports.index("auto postImportFailure ="):imports.index("auto postEmbeddingFailure =")]
        self.assertIn("ProjectTasks().PostToMain", failure)
        self.assertIn("node->instanceUuid == nodeUuid", failure)
        self.assertIn("node->image.importRequestId == requestId", failure)
        self.assertIn("m_Project->graph.RemoveNode(nodeId)", failure)
        removal = body("src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp", "bool Graph::RemoveNode(int nodeId)")
        self.assertIn("TouchStructure();", removal)

    def test_explicit_owner_reset_and_shutdown_discard_notes(self):
        for path, signature in [
            ("src/Editor/NodeGraph/UI/EditorNodeGraphUIPreviews.cpp",
             "void EditorNodeGraphUI::Shutdown()"),
            ("src/Editor/NodeGraph/UI/EditorNodeGraphUIProjectState.cpp",
             "void EditorNodeGraphUI::ResetProjectInteractionState()"),
        ]:
            with self.subTest(signature=signature):
                owner = body(path, signature)
                self.assertIn("m_NodeNotes.Reset();", owner)
                self.assertIn("ResetPerGraphVisualCaches();", owner)


if __name__ == "__main__":
    unittest.main(argv=[__file__, *remaining])
