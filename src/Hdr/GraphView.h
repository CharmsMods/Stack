#pragma once
struct RenderGraphSnapshot;
namespace Stack::Hdr {
// Configure a private render snapshot after resolving all edits and outputs.
// A false result means that this endpoint does not declare an HDR rendition.
bool ConfigureView(RenderGraphSnapshot& graph,float headroom,bool encodeSrgb);
bool NeedsSdrPhotoView(const RenderGraphSnapshot& graph);
}
