#pragma once
namespace Stack::Hdr {
// UI-thread metadata only. Texture ownership stays with the published leases.
unsigned RegisterPhotoPair(unsigned sdr,unsigned hdr);
unsigned PhotoBaseForFrame(unsigned hdr);
float CurrentTargetHeadroom();
void SetPhotoHeadroomUniform(int location);
}
