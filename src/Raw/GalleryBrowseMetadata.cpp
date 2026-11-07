#include "GalleryBrowseMetadata.h"
#include "Async/ForegroundIO.h"
namespace Stack::RawWorkspace {
void RefreshGalleryBrowseMetadata(const ManagedLayout& layout,SourceRecord& source,const CancellationPredicate& canceled) {
    const auto yield=[&]{return !Async::ForegroundIO::WaitForIdle(canceled);};
    if(yield())return;
    ClassifyThumbnailMetadata(layout,source);
    source.browsePreview=true;
    if(source.thumbnail.similarityDescriptor.IsValid() || !source.thumbnail.camera.bytes || yield())return;
    std::vector<unsigned char> pixels;int width=0,height=0;
    if(DecodeCameraThumbnail(source.absolutePath,source.thumbnail.camera,128,pixels,width,height,yield) && !yield())
        source.transientThumbnail.similarityDescriptor=BuildRawGallerySimilarityDescriptor(pixels.data(),width,height);
}
}
