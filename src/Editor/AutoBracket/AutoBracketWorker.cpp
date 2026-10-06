#include "AutoBracketCoordinator.h"
#include "Persistence/BracketingResultStore.h"
#include "Persistence/MultiFrameProjectCreation.h"
#include "Persistence/BracketingProject.h"
#include "Utils/PngEncodingUtils.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace Stack::AutoBracket {
namespace {
void Status(const std::shared_ptr<Work>& work,const std::string& text) {
    {std::lock_guard<std::mutex> lock(work->mutex);work->status=text;}
    work->notifier.UpdateActivity(work->activity,text);
}
void BuildCover(const Raw::Bracketing::Preview& preview,Work& work) {
    // Same camera conversion and view transform as the bracket inspection preview.
    const auto count=std::size_t(preview.width)*preview.height;
    if(!count||preview.resultRgb.size()!=count*3)throw std::runtime_error("The bracket preview is unavailable.");
    const int orientation=preview.metadata.orientation;
    const bool transpose=orientation>=5&&orientation<=8;
    work.coverWidth=transpose?preview.height:preview.width;work.coverHeight=transpose?preview.width:preview.height;
    work.coverPixels.resize(count*4);
    for(unsigned y=0;y<preview.height;++y)for(unsigned x=0;x<preview.width;++x) {
        unsigned ox=x,oy=y;
        switch(orientation) {
        case 2:ox=preview.width-1-x;break;
        case 3:ox=preview.width-1-x;oy=preview.height-1-y;break;
        case 4:oy=preview.height-1-y;break;
        case 5:ox=y;oy=x;break;
        case 6:ox=preview.height-1-y;oy=x;break;
        case 7:ox=preview.height-1-y;oy=preview.width-1-x;break;
        case 8:ox=y;oy=preview.width-1-x;break;
        }
        const auto input=std::size_t(y)*preview.width+x,output=std::size_t(oy)*work.coverWidth+ox;
        float camera[3]{};
        for(unsigned c=0;c<3;++c)camera[c]=preview.resultRgb[input*3+c]*preview.metadata.cameraWhiteBalance[c]/
            std::max(1e-6f,preview.metadata.cameraWhiteBalance[1]);
        for(unsigned c=0;c<3;++c) {
            float value=0;for(unsigned k=0;k<3;++k)value+=preview.metadata.cameraToSrgb[c*3+k]*camera[k];
            value=std::max(0.f,value);value/=1+value;
            value=value<=.0031308f?value*12.92f:1.055f*std::pow(value,1/2.4f)-.055f;
            work.coverPixels[output*4+c]=static_cast<unsigned char>(std::clamp(value,0.f,1.f)*255+.5f);
        }
        work.coverPixels[output*4+3]=255;
    }
}
}
void ExecuteWork(const std::shared_ptr<Work>& work,const Raw::OpenGlTaskExecutor& executor) {
    using namespace Project;
    bool committed=false;
    bool processingFailure=false;
    try {
        if(work->cancel)throw std::runtime_error("Canceled.");
        std::error_code ec;
        if(std::filesystem::exists(work->item.projectPath,ec)) {
            work->project=OpenProjectStore(work->item.projectPath);
            if(!work->project){work->storageFailure=true;throw std::runtime_error(work->project.message);}
            if(work->project.snapshot.projectId!=work->item.projectId)
                throw std::runtime_error("Another project occupies the reserved bracket location.");
        }
        if (!work->project || (work->project.snapshot.dirtyRevision==0 &&
            work->project.snapshot.sourceSets.empty() && work->project.snapshot.embeddedAssets.empty())) {
            work->project.store.reset();
            MultiFrameProjectCreation creation;
            creation.resumeEmptyProject=true;
            creation.path=work->item.projectPath;creation.projectId=work->item.projectId;
            creation.projectName=work->item.candidate.name;
            for(const auto& source:work->item.candidate.sources)creation.sources.push_back(source.path);
            creation.shouldCancel=[work]{return work->cancel.load();};
            ProjectCreationFailure failure;
            work->project=CreateMultiFrameProject(creation,&failure);
            if(!work->project) {
                work->storageFailure=failure==ProjectCreationFailure::Storage;
                throw std::runtime_error(work->project.message);
            }
        }
        auto& snapshot=work->project.snapshot;
        auto* set=FindSourceSet(snapshot,snapshot.activeSourceSetId);
        if(!set||!IsBracketing(*set))throw std::runtime_error("The saved project is not a bracket.");
        std::vector<Source> embedded;
        std::set<int> orientations;
        for(const auto& frame:set->frames) {
            const auto* asset=FindEmbeddedAsset(snapshot,frame.assetId);
            if(!asset)throw std::runtime_error("A saved capture is missing.");
            embedded.push_back({{},asset->sha256,asset->byteLength});
            RawCaptureCompatibilitySummary metadata;
            if(!DeserializeRawCaptureCompatibilitySummary(asset->captureMetadataSummary,metadata,nullptr)||!metadata.supported)
                throw std::runtime_error("A capture needs compatibility review.");
            orientations.insert(metadata.orientation);
        }
        if(SourceIdentity(embedded)!=work->item.candidate.identity)
            throw std::runtime_error("The captures changed since grouping. Rescan this folder before retrying.");
        Raw::Bracketing::BracketingRecipe recipe;std::string error;
        if(!Raw::Bracketing::Deserialize(set->settings.at("bracketing"),recipe,error))throw std::runtime_error(error);
        if(HasSavedBracketingResult(snapshot,*set)) {
            Raw::Bracketing::BracketingResult restored;
            if(RestoreBracketingResult(work->project.store,snapshot,set->sourceSetId,restored,error,[work]{return work->cancel.load();})) {
                BuildCover(restored.preview,*work);
                if(snapshot.coverThumbnailBytes.empty()) {
                    const auto cover=PngEncoding::EncodeInterleaved(work->coverPixels,work->coverWidth,work->coverHeight,4);
                    if(cover.empty())throw std::runtime_error("Could not encode the bracket preview.");
                    if(work->cancel)throw std::runtime_error("Canceled.");
                    const auto setId=set->sourceSetId;
                    if(!SaveBracketingResult(work->project.store,snapshot,setId,restored,cover,error)) {
                        work->storageFailure=true;throw std::runtime_error(error);
                    }
                }
                work->result=std::make_shared<Raw::Bracketing::BracketingResult>(std::move(restored));
                committed=true;
            }
        }
        if(!committed) {
            const auto provenance=set->settings.find("autoBracket");
            if(provenance!=set->settings.end()&&snapshot.persistedStorageRevision!=provenance->value("storageRevision",std::uint64_t{0})) {
                work->outcome=State::Completed;Status(work,"Project retained with its existing edits.");
                work->notifier.CompleteActivity(work->activity,"Existing bracket retained.",false);
                work->done=true;return;
            }
            if(set->settings.contains("bracketingDraft"))throw std::runtime_error("This bracket has saved input edits. Open it to continue.");
            for(const auto value:orientations)if(Raw::Bracketing::ResolveOrientation(recipe,value)!=
                Raw::Bracketing::ResolveOrientation(recipe,*orientations.begin()))
                    throw std::runtime_error("Review the capture orientations before processing.");
            if(recipe.reconstruction!=Raw::Bracketing::ReconstructionMode::Standard)
                throw std::runtime_error("This bracket has manual reconstruction settings. Open it to continue.");
            if(provenance==set->settings.end()) {
                const auto transaction=work->project.store->BeginTransaction(snapshot.persistedStorageRevision);
                set->settings["autoBracket"]={{"sources",work->item.candidate.identity},{"storageRevision",snapshot.persistedStorageRevision+1}};
                const auto saved=work->project.store->Commit(transaction,snapshot);
                if(!saved){work->project.store->Abort(transaction);work->storageFailure=true;throw std::runtime_error(saved.message);}
                snapshot.persistedStorageRevision=saved.committedStorageRevision;
            }
            Project::BracketingJobRequest request;
            request.store=work->project.store;request.snapshot=snapshot;request.setId=set->sourceSetId;
            request.cacheRoot=work->cacheRoot;
            request.recipe=recipe;request.executeOpenGlTask=executor;request.restoreSavedResult=false;
            request.shouldCancel=[work]{return work->cancel.load();};
            request.reportProgress=[work](double progress,const std::string& text){work->progress=progress;Status(work,text);};
            request.reportPresentation=[work](const Raw::Bracketing::ProcessingProgress& progress){
                if(progress.stage!=Raw::Bracketing::ProcessingStage::Completed)work->mailbox->Publish(progress);
                if(progress.evidenceOnly||progress.thumbnail)return;
                std::optional<Notifications::Progress> measured;
                if(progress.total>0)measured=Notifications::Progress{double(progress.completed),double(progress.total),{}};
                work->notifier.UpdateActivity(work->activity,Raw::Bracketing::ProcessingStageTitle(progress.stage),measured);
            };
            auto result=Project::RunBracketingJob(request);
            if(work->cancel||result.status==Raw::Bracketing::BracketingResult::Status::Canceled)throw std::runtime_error("Canceled.");
            if(result.status!=Raw::Bracketing::BracketingResult::Status::Completed){processingFailure=true;throw std::runtime_error(result.message);}
            BuildCover(result.preview,*work);
            const auto cover=PngEncoding::EncodeInterleaved(work->coverPixels,work->coverWidth,work->coverHeight,4);
            if(cover.empty())throw std::runtime_error("Could not encode the bracket preview.");
            if(work->cancel)throw std::runtime_error("Canceled.");
            Status(work,"Saving bracket...");
            if(!SaveBracketingResult(work->project.store,snapshot,request.setId,result,cover,error)) {
                work->storageFailure=true;throw std::runtime_error(error);
            }
            committed=true;work->imageProduced=true;
            work->result=std::make_shared<Raw::Bracketing::BracketingResult>(std::move(result));
        }
        work->outcome=State::Completed;Status(work,"Bracket saved.");
        Raw::Bracketing::ProcessingProgress terminal;terminal.stage=Raw::Bracketing::ProcessingStage::Completed;
        work->mailbox->Publish(terminal);
    }catch(const std::exception& error) {
        work->outcome=work->cancel&&!committed?State::Pending:(work->storageFailure||processingFailure)?State::Failed:State::Attention;
        Status(work,work->cancel&&!committed?"Waiting to resume.":error.what());
    }catch(...) {work->outcome=State::Failed;Status(work,"Automatic bracket processing failed.");}
    if(work->outcome==State::Completed) work->notifier.CompleteActivity(work->activity,
        work->imageProduced ? "Bracket RAW result saved." : "Saved bracket opened.",work->imageProduced);
    else if(work->cancel&&work->outcome==State::Pending) work->notifier.CancelActivity(work->activity,"Automatic bracketing cancelled.");
    else if(work->outcome==State::Attention&&work->project) work->notifier.FinishActivity(work->activity,
        Notifications::Outcome::Partial,"Bracket project available. Processing needs review.",work->status);
    else work->notifier.FailActivity(work->activity,"Automatic bracketing failed.",work->status);
    work->done.store(true);
}
}
